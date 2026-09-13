#include "kernel_stack.h"
#include "kernel/xk.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_xram;
static xctx cpu;
static uint32_t arguments;
static jmp_buf fault;
void xv_logf(const char *format, ...) { (void)format; }
void h2_kernel_stack_fault(xctx *c, const char *reason, uint32_t a, uint32_t b)
{ (void)c; (void)reason; (void)a; (void)b; longjmp(fault, 1); }
static void args(uint32_t a, uint32_t b)
{
    memset(&cpu, 0xA6, sizeof cpu); cpu.r[4] = arguments;
    X_M32(arguments) = 0x12345678; X_M32(arguments + 4) = a; X_M32(arguments + 8) = b;
}
static uint32_t create(uint32_t size, uint32_t debugger)
{
    args(size, debugger); xctx expected = cpu;
    __wrap_xk_MmCreateKernelStack(&cpu);
    expected.r[4] += 12; expected.r[0] = cpu.r[0];
    assert(!memcmp(&cpu, &expected, sizeof cpu));
    assert(X_M32(arguments) == 0x12345678 && X_M32(arguments + 4) == size && X_M32(arguments + 8) == debugger);
    return cpu.r[0];
}
static void destroy(uint32_t top, uint32_t bytes)
{
    args(top, top - bytes); xctx expected = cpu; expected.r[4] += 12;
    __wrap_xk_MmDeleteKernelStack(&cpu);
    assert(!memcmp(&cpu, &expected, sizeof cpu));
}
static void reject(void (*function)(xctx *))
{
    xctx expected = cpu;
    uint32_t available = xk_mem_available();
    uint32_t *pages = malloc((1u << 20) * sizeof *pages); assert(pages);
    memcpy(pages, g_xpt, (1u << 20) * sizeof *pages);
    if (!setjmp(fault)) { function(&cpu); assert(!"expected rejection"); }
    assert(!memcmp(&cpu, &expected, sizeof cpu));
    assert(available == xk_mem_available());
    assert(!memcmp(pages, g_xpt, (1u << 20) * sizeof *pages)); free(pages);
}
int main(void)
{
    xk_mem_setup(0x10000, 0x20000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena();
    arguments = xk_mem_alloc(XK_PAGE, 0, 0, 0, 0); assert(arguments);
    uint32_t available = xk_mem_available();
    uint32_t top = create(0x6000, 0), limit = top - 0x6000;
    assert(top == H2_STACK_WINDOW_BEGIN + 0x7000);
    assert(xk_mem_available() == available - 0x6000); /* no physical guard page */
    assert(h2_kernel_stack_unmapped(limit - 1) && !h2_kernel_stack_unmapped(limit));
    assert(!h2_kernel_stack_unmapped(top - 1) && h2_kernel_stack_unmapped(top));
    for (uint32_t p = limit; p < top; p += XK_PAGE) {
        assert(X_M32(p) == 0 && X_M32(p + XK_PAGE - 4) == 0);
        X_M32(p) = p; X_M32(p + XK_PAGE - 4) = ~p;
    }
    args(top, limit + 1); reject(__wrap_xk_MmDeleteKernelStack);
    args(0, 0); reject(__wrap_xk_MmDeleteKernelStack);
    args(0x6000, 1); reject(__wrap_xk_MmCreateKernelStack);
    args(0x6000, 0); cpu.r[4] = limit - 8; reject(__wrap_xk_MmCreateKernelStack);
    args(0x6000, 0); cpu.r[4] = UINT32_MAX - 3; reject(__wrap_xk_MmCreateKernelStack);
    uint32_t mapping = g_xpt[limit >> 12];
    g_xpt[limit >> 12] = xk_mem_arena_size() - XK_PAGE;
    args(top, limit); reject(__wrap_xk_MmDeleteKernelStack);
    g_xpt[limit >> 12] = mapping;
    for (uint32_t p = limit; p < top; p += XK_PAGE)
        assert(X_M32(p) == p && X_M32(p + XK_PAGE - 4) == ~p);
    destroy(top, 0x6000); assert(xk_mem_available() == available);
    assert(h2_kernel_stack_unmapped(limit) && h2_kernel_stack_unmapped(top - 1));
    args(top, limit); reject(__wrap_xk_MmDeleteKernelStack);

    /* Preserve unrelated mappings, byte-sized requests and BOOLEAN low byte. */
    uint32_t occupied = H2_STACK_WINDOW_BEGIN + XK_PAGE;
    g_xpt[occupied >> 12] = g_xpt[arguments >> 12];
    top = create(1, 0x100); assert(top > occupied + XK_PAGE && (top & 4095) == 1);
    assert(g_xpt[occupied >> 12] == g_xpt[arguments >> 12]);
    destroy(top, 1); g_xpt[occupied >> 12] = xk_mem_arena_size() - XK_PAGE;
    assert(create(0, 0) == 0 && create(UINT32_MAX, 0) == 0);
    uint32_t small[64];
    for (unsigned i = 0; i < 64; ++i) { small[i] = create(1, 0); assert(small[i]); }
    uint32_t full_available = xk_mem_available();
    assert(create(1, 0) == 0 && xk_mem_available() == full_available);
    for (unsigned i = 0; i < 64; ++i) destroy(small[i], 1);
    assert(xk_mem_available() == available);

    /* Force physical exhaustion, then three nonadjacent free pages. */
    uint32_t big = xk_phys_alloc(available - 5 * XK_PAGE, XK_PAGE, 0, 0, 0), pieces[5];
    assert(xk_mem_available() == 5 * XK_PAGE);
    for (unsigned i = 0; i < 5; ++i) pieces[i] = xk_phys_alloc(XK_PAGE, XK_PAGE, 0, 0, 0);
    assert(xk_mem_available() == 0 && create(0x6000, 0) == 0);
    for (unsigned i = 0; i < 5; i += 2) assert(xk_phys_free(pieces[i]) == 0);
    top = create(3 * XK_PAGE, 0); assert(top);
    limit = top - 3 * XK_PAGE;
    assert(g_xpt[(limit + XK_PAGE) >> 12] != g_xpt[limit >> 12] + XK_PAGE);
    for (unsigned i = 0; i < 3; ++i) X_M32(limit + i * XK_PAGE) = 0x12340000 + i;
    for (unsigned i = 0; i < 3; ++i) assert(X_M32(limit + i * XK_PAGE) == 0x12340000 + i);
    destroy(top, 3 * XK_PAGE);
    for (unsigned i = 1; i < 5; i += 2) assert(xk_phys_free(pieces[i]) == 0);
    assert(xk_phys_free(big) == 0 && xk_mem_available() == available);
    assert(xk_mem_free(arguments) == 0);
    free(g_xpt); free(g_xram);
    puts("Halo 2 kernel stacks: guards, ABI, ownership, fragmentation, exhaustion and deletion passed");
    return 0;
}
