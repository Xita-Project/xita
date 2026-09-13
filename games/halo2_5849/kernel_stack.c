/* Halo 2 retail kernel stacks. The shared allocator owns the backing pages;
 * guest system mappings add a lower, physically unallocated guard page. */
#include "kernel_stack.h"
#include "kernel/xk.h"
#include <string.h>

typedef struct { uint32_t backing, guard, requested, mapped; } stack_record;
static stack_record stacks[64];
extern void xv_logf(const char *, ...);

static uint32_t unmapped_offset(void) { return xk_mem_arena_size() - XK_PAGE; }
int h2_kernel_stack_unmapped(uint32_t address)
{
    return address >= H2_STACK_WINDOW_BEGIN && address < H2_STACK_WINDOW_END &&
           g_xpt[address >> 12] == unmapped_offset();
}
static int mapped_arguments(uint32_t address)
{
    if (!address || (uint64_t)address + 12 > UINT32_MAX + 1ull) return 0;
    uint32_t last = (address + 11) >> 12, limit = unmapped_offset();
    for (uint32_t p = address >> 12; p <= last; ++p)
        if ((g_xpt[p] & (XK_PAGE - 1)) || (uint64_t)g_xpt[p] + XK_PAGE > limit) return 0;
    return 1;
}
static uint32_t reserve_candidate(uint32_t bytes)
{
    uint32_t candidate = H2_STACK_WINDOW_BEGIN, trash = unmapped_offset();
    while ((uint64_t)candidate + bytes <= H2_STACK_WINDOW_END) {
        uint32_t end = candidate + bytes, next = candidate;
        for (unsigned i = 0; i < 64; ++i)
            if (stacks[i].backing && candidate < stacks[i].guard + XK_PAGE + stacks[i].mapped &&
                stacks[i].guard < end && next < stacks[i].guard + XK_PAGE + stacks[i].mapped)
                next = stacks[i].guard + XK_PAGE + stacks[i].mapped;
        if (next != candidate) { candidate = next; continue; }
        for (uint32_t p = candidate; p < end; p += XK_PAGE)
            if (g_xpt[p >> 12] != trash) { next = p + XK_PAGE; break; }
        if (next == candidate) return candidate;
        candidate = next;
    }
    return 0;
}
void __wrap_xk_MmCreateKernelStack(xctx *c)
{
    if (!mapped_arguments(c->r[4])) { h2_kernel_stack_fault(c, "unmapped arguments", c->r[4], 12); return; }
    uint32_t bytes = X_ARG(0), debugger = X_ARG(1) & 0xFFu;
    if (debugger) { h2_kernel_stack_fault(c, "debugger stack unsupported", bytes, debugger); return; }
    uint32_t top = 0;
    unsigned slot = 0;
    while (slot < 64 && stacks[slot].backing) ++slot;
    if (bytes && bytes <= H2_STACK_WINDOW_END - H2_STACK_WINDOW_BEGIN - XK_PAGE && slot < 64) {
        uint32_t rounded = (bytes + XK_PAGE - 1) & ~(XK_PAGE - 1);
        uint32_t guard = reserve_candidate(rounded + XK_PAGE);
        uint32_t backing = guard ? xk_mem_alloc(rounded, XK_PAGE, 0, 0, 0) : 0;
        if (backing) {
            /* Each page is copied independently: physical contiguity is not
             * required. The guard owns no physical page. */
            for (uint32_t off = 0; off < rounded; off += XK_PAGE)
                g_xpt[(guard + XK_PAGE + off) >> 12] = g_xpt[(backing + off) >> 12];
            stacks[slot] = (stack_record){backing, guard, bytes, rounded};
            top = guard + XK_PAGE + bytes;
        }
    }
    xv_logf("[h2/stack] create bytes=%08X debugger=0 top=%08X\n", bytes, top);
    c->r[0] = top; X_RET(2);
}
void __wrap_xk_MmDeleteKernelStack(xctx *c)
{
    if (!mapped_arguments(c->r[4])) { h2_kernel_stack_fault(c, "unmapped arguments", c->r[4], 12); return; }
    uint32_t top = X_ARG(0), limit = X_ARG(1);
    for (unsigned i = 0; i < 64; ++i) {
        stack_record record = stacks[i];
        if (!record.backing || top != record.guard + XK_PAGE + record.requested || limit != record.guard + XK_PAGE) continue;
        if (xk_mem_size(record.backing) != record.mapped) {
            h2_kernel_stack_fault(c, "backing ownership changed", top, limit); return;
        }
        for (uint32_t off = 0; off < record.mapped; off += XK_PAGE)
            if (g_xpt[(limit + off) >> 12] != g_xpt[(record.backing + off) >> 12]) {
                h2_kernel_stack_fault(c, "stack mapping changed", top, limit); return;
            }
        if (xk_mem_free(record.backing)) {
            h2_kernel_stack_fault(c, "backing deletion failed", top, limit); return;
        }
        for (uint32_t off = 0; off < record.mapped; off += XK_PAGE)
            g_xpt[(limit + off) >> 12] = unmapped_offset();
        memset(&stacks[i], 0, sizeof stacks[i]);
        xv_logf("[h2/stack] delete top=%08X limit=%08X\n", top, limit);
        X_RET(2);
    }
    h2_kernel_stack_fault(c, "unowned stack deletion", top, limit);
}
