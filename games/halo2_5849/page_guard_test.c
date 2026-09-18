/* xv_guard_physical / xv_guarded_physical: guarding a physical page must trash exactly
 * the virtual aliases that map to it (found through the kernel's live page table), never
 * the virtual page that merely shares its number, and must restore them precisely. */
#include "kernel/xk.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *g_xram;
void xv_logf(const char *format, ...) { (void)format; }
void xk_os_log(const char *format, ...) { (void)format; }
extern void xv_guard_physical(uint32_t physical, uint32_t bytes, int guard);
extern uint32_t xv_guarded_physical(uint32_t address);

int main(void)
{
    xk_mem_setup(0x10000, 0x20000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena();
    uint32_t trash = xk_mem_arena_size() - XK_PAGE;
    uint32_t *snapshot = malloc((1u << 20) * 4); assert(snapshot);

    /* An allocation: virtual page v backed by physical page q, q != v (the allocator hands
     * out physical pages from the top of RAM); virtual page q still holds its identity map. */
    uint32_t va = xk_mem_alloc(XK_PAGE, 0, 0, 0, 0); assert(va);
    uint32_t v = va >> 12, phys = g_xpt[v], q = phys >> 12;
    assert(phys < trash && q != v && g_xpt[q] == phys && g_xpt[0x80000u + q] == phys && g_xpt[0xF0000u + q] == phys);
    memcpy(snapshot, g_xpt, (1u << 20) * 4);

    /* Guarding q trashes every alias of q: the allocation, the identity page, both fixed aliases. */
    xv_guard_physical(phys, XK_PAGE, 1);
    assert(g_xpt[v] == trash && g_xpt[q] == trash && g_xpt[0x80000u + q] == trash && g_xpt[0xF0000u + q] == trash);
    assert(xv_guarded_physical(va + 0x123) == phys + 0x123);
    assert(xv_guarded_physical((q << 12) + 4) == phys + 4);
    assert(xv_guarded_physical(0x80000000u + phys + 8) == phys + 8);
    assert(xv_guarded_physical(va + XK_PAGE) == 0xFFFFFFFFu);      /* the next page is not guarded */
    /* Restoring puts back exactly the previous table. */
    xv_guard_physical(phys, XK_PAGE, 0);
    assert(!memcmp(snapshot, g_xpt, (1u << 20) * 4));
    assert(xv_guarded_physical(va) == 0xFFFFFFFFu);

    /* The regression: guarding the physical page numbered v (some other buffer) must leave
     * the allocation's virtual page alone - it maps to q, not to v. */
    uint32_t other = v << 12;
    assert(g_xpt[v] == phys);
    xv_guard_physical(other, XK_PAGE, 1);
    assert(g_xpt[v] == phys && g_xpt[0x80000u + v] == trash);
    assert(xv_guarded_physical(va) == 0xFFFFFFFFu && xv_guarded_physical(0x80000000u + other) == other);
    xv_guard_physical(other, XK_PAGE, 0);
    assert(!memcmp(snapshot, g_xpt, (1u << 20) * 4));

    /* A range: every page's aliases, then a full restore. */
    uint32_t big = xk_mem_alloc(4 * XK_PAGE, 0, 0, 0, 0); assert(big);
    uint32_t p0 = g_xpt[big >> 12];
    memcpy(snapshot, g_xpt, (1u << 20) * 4);
    uint32_t lo = p0, hi = p0;
    for (unsigned i = 0; i < 4; ++i) { uint32_t p = g_xpt[(big >> 12) + i]; if (p < lo) lo = p; if (p > hi) hi = p; }
    xv_guard_physical(lo, hi - lo + XK_PAGE, 1);
    for (unsigned i = 0; i < 4; ++i) assert(g_xpt[(big >> 12) + i] == trash && xv_guarded_physical(big + i * XK_PAGE) == snapshot[(big >> 12) + i]);
    xv_guard_physical(lo, hi - lo + XK_PAGE, 0);
    assert(!memcmp(snapshot, g_xpt, (1u << 20) * 4));

    /* Remapping a guarded virtual page voids its guard: freeing the allocation returns the
     * page to its identity mapping, and the later restore must not clobber that. */
    xv_guard_physical(phys, XK_PAGE, 1);
    assert(g_xpt[v] == trash);
    assert(xk_mem_free(va) == 0);
    assert(g_xpt[v] == (v << 12) && xv_guarded_physical(va) == 0xFFFFFFFFu);
    xv_guard_physical(phys, XK_PAGE, 0);
    assert(g_xpt[v] == (v << 12) && g_xpt[q] == phys && g_xpt[0x80000u + q] == phys);
    xv_guard_physical(phys, XK_PAGE, 0);                             /* idempotent */
    assert(g_xpt[v] == (v << 12));

    /* A page with three virtual aliases (identity plus two allocations mapped by hand) is
     * resolved by the scan: all of them are guarded and restored. */
    uint32_t a1 = xk_mem_alloc(XK_PAGE, 0, 0, 0, 0), a2 = xk_mem_alloc(XK_PAGE, 0, 0, 0, 0); assert(a1 && a2);
    uint32_t shared = g_xpt[a1 >> 12];
    xk_mem_map_alias(a2, shared);
    assert(g_xpt[a2 >> 12] == shared && g_xpt[shared >> 12] == shared);
    memcpy(snapshot, g_xpt, (1u << 20) * 4);
    xv_guard_physical(shared, XK_PAGE, 1);
    assert(g_xpt[a1 >> 12] == trash && g_xpt[a2 >> 12] == trash && g_xpt[shared >> 12] == trash);
    assert(xv_guarded_physical(a2 + 1) == shared + 1);
    xv_guard_physical(shared, XK_PAGE, 0);
    assert(!memcmp(snapshot, g_xpt, (1u << 20) * 4));

    printf("page_guard_test: all assertions passed\n");
    return 0;
}
