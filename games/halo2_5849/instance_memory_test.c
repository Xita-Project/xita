#include "instance_memory.h"
#include "kernel/xk.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

uint8_t *g_xram;
static jmp_buf rejected;
void xk_os_log(const char *format, ...) { (void)format; }
void xv_logf(const char *format, ...) { (void)format; }
void h2_graphics_stop(xctx *c, uint32_t ip, uint32_t address, uint32_t value, int write, int reason)
{ (void)c; (void)ip; (void)address; (void)value; (void)write; (void)reason; longjmp(rejected, 1); }

int main(void)
{
    uint32_t padding = 0xDEADBEEF;
    assert(h2_instance_claim(0x5000, &padding) == 0 && padding == 0xDEADBEEF);
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram); xk_mem_bind_arena();
    assert(h2_instance_memory_init() == 0 && h2_instance_bytes() == 0x10000);
    assert(h2_instance_memory_init() == -1);
    assert(h2_instance_claim(UINT32_MAX, &padding) == 0x83FF0000 && padding == 0x10000);
    assert(h2_instance_bytes() == 0x10000);
    /* Real export stack ABI: two parameters, output stored before return. */
    uint32_t stack = xk_kalloc(64); assert(stack);
    xctx c = {0}; c.r[4] = stack; X_M32(stack) = 0xABCDEF01;
    X_M32(stack + 4) = 0x5000; X_M32(stack + 8) = stack + 32;
    X_M32(stack + 32) = 0xDEADBEEF;
    __wrap_xk_MmClaimGpuInstanceMemory(&c);
    assert(c.r[0] == 0x83FF0000 && c.r[4] == stack + 12);
    assert(X_M32(stack + 32) == 0x10000 && h2_instance_bytes() == 0x5000);
    assert(h2_instance_claim(0x4FFF, &padding) == 0x83FF0000 && h2_instance_bytes() == 0x5000);
    assert(h2_instance_claim(0x5000, &padding) == 0x83FF0000); /* repeat claim */
    c.r[4] = stack; c.r[0] = 0x12345678; X_M32(stack + 4) = 0x5001;
    if (!setjmp(rejected)) { __wrap_xk_MmClaimGpuInstanceMemory(&c); abort(); }
    assert(h2_instance_bytes() == 0x5000 && c.r[0] == 0x12345678 && c.r[4] == stack);
    assert(X_M32(stack + 32) == 0x10000);
    padding = 0xDEADBEEF;
    assert(h2_instance_claim(0xFFFFFFFE, &padding) == 0 && padding == 0xDEADBEEF);
    assert(h2_instance_claim(0x1000, NULL) == 0 && h2_instance_bytes() == 0x5000);
    xk_kfree(stack);
    /* Allocation searches may consume the freed prefix, never live GPU bytes. */
    uint32_t prefix = xk_kalloc(0x2EB000), suffix = xk_kalloc(0x10000);
    assert(prefix == 0x03D00000 && suffix == 0x03FF0000 && xk_kalloc(64) == 0);
    assert(h2_instance_claim(0, &padding) == 0x83FF0000 && h2_instance_bytes() == 0);
    uint32_t released = xk_kalloc(0x5000); assert(released == 0x03FEB000);
    xk_kfree(prefix); xk_kfree(suffix); xk_kfree(released);
    free(g_xram); free(g_xpt);
    puts("Halo 2 instance RAM: retail bounds/padding, stack ABI, shrink/query/repeat and allocator isolation passed");
    return 0;
}
