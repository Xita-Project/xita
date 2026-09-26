#pragma once
#include "../xv_x86rt.h"

/* Experimental prefix of the ordered scene index loop at 54132. Opt-in hook.
 * Caller must own ordinary scene RAM for this interval. There are no guest
 * writes or callbacks inside the prefix. Stop before a page boundary, loop
 * exit or scheduler handoff; the original loop executes all of those.
 * Return zero without changing context when a useful prefix is unavailable.
 */
static inline unsigned xv_scene_index_run(xctx *c, uint8_t *arena,
                                         const uint32_t *pages, size_t bytes)
{
    uint32_t cursor = c->r[3], sp = c->r[4];
    if (!arena || !pages || bytes < 4096 || c->preempt <= 2 ||
        (cursor & 3u) || cursor >= 0xfcfffffcu || sp >= 0xfcffffecu ||
        ((sp + 0x14u) & 4095u) > 4092u) return 0;
    /* The native context must not alias the guest arena being inspected. */
    uintptr_t cp = (uintptr_t)c, ap = (uintptr_t)arena;
    if (cp >= ap ? cp - ap < bytes : ap - cp < sizeof *c) return 0;
    uint32_t bound_slot = sp + 0x14u;
    uint32_t off = pages[bound_slot >> 12];
    if ((off & 4095u) || off > bytes - 4096) return 0;
    uint32_t bound;
    memcpy(&bound, arena + off + (bound_slot & 4095u), 4);
    uint32_t next = cursor + 4u;
    if ((bound & 3u) || next >= bound) return 0;
    unsigned limit = (bound - next) / 4u;
    unsigned page_limit = (4096u - (next & 4095u)) / 4u;
    if (limit > page_limit) limit = page_limit;
    if (limit > (unsigned)c->preempt - 1u) limit = (unsigned)c->preempt - 1u;
    if (limit < 4) return 0;
    off = pages[next >> 12];
    if ((off & 4095u) || off > bytes - 4096) return 0;
    const uint8_t *p = arena + off + (next & 4095u);
    uint32_t threshold = c->r[1], last = 0;
    unsigned n = 0;
    while (n < limit) {
        uint32_t value;
        memcpy(&value, p + n * 4u, 4);
        if ((int32_t)value >= (int32_t)threshold) break;
        last = value;
        ++n;
    }
    if (n < 4) return 0;
    c->r[3] = cursor + n * 4u;
    c->r[6] = bound;
    c->preempt -= (int32_t)n;
    X_FLAGS(XK_SUB, last, threshold, last - threshold, 32);
    return n;
}
