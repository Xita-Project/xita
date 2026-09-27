/* Halo CE 3925's consecutive default sampler writes in 70110.
 * Precondition: stage is 0..3; opt-in caller validates owner/diagnostic admission.
 * Caller must retain worker/diagnostic dispatch or decline this path there.
 * This is an ordered operation, not a cache: guest stack stores are observable.
 */
#pragma once
#include "../xv_x86rt.h"

static inline void xv_material_sampler_defaults(xctx *c, unsigned stage)
{
    static const uint32_t first_return[4] = {
        0x7044Eu, 0x704A9u, 0x70510u, 0x70577u
    };
    static const uint32_t states[5] = {10, 11, 13, 14, 15};
    unsigned spacing = stage ? 17 : 14;
#if defined(XV_MATERIAL_SAMPLER_POINTERS) && XV_MATERIAL_SAMPLER_POINTERS && !defined(XV_CHECK_GUEST_ADDRESS)
    /* Call-local translations only: this sequence has no calls, yields or
     * mapping mutations. Keep every guest write/read in its original order,
     * including physical aliases between stack and texture state. Decline
     * split stack spans and guest aliases of the host context. Checked builds
     * retain per-access diagnostics and write-epoch stamping below. */
    uint32_t sp = c->r[4];
    if (sp >= 8u && ((sp - 8u) & 0xfffu) <= 4088u) {
        unsigned char *stack = (unsigned char *)X_GW(sp - 8u);
        unsigned char *table = (unsigned char *)X_GW(0x18F180u + stage * 128u);
        uintptr_t cp = (uintptr_t)c, st = (uintptr_t)stack, tb = (uintptr_t)table;
        if (!(st < cp + sizeof(*c) && cp < st + 8u) &&
            !(tb < cp + sizeof(*c) && cp < tb + 128u)) {
            for (unsigned i = 0; i < (stage == 3 ? 6u : 5u); ++i) {
                c->r[4] -= 4u;
                *(xu32_u *)(stack + 4) = stage == 3 ? (i < 3 ? 3u : 2u) : (i < 2 ? 1u : 2u);
                c->r[2] = stage == 3 ? 10u + i : states[i];
                c->r[1] = stage;
                c->r[4] -= 4u;
                *(xu32_u *)stack = first_return[stage] + i * spacing;
                *(xu32_u *)(table + c->r[2] * 4u) = *(xu32_u *)(stack + 4);
                c->r[4] += 8u;
            }
            return;
        }
    }
#endif
    for (unsigned i = 0; i < (stage == 3 ? 6u : 5u); ++i) {
        X_PUSH32(stage == 3 ? (i < 3 ? 3u : 2u) : (i < 2 ? 1u : 2u));
        c->r[2] = stage == 3 ? 10u + i : states[i];
        c->r[1] = stage;
        X_PUSH32(first_return[stage] + i * spacing);
        /* Match the original HLE reads and writes, including stack/table alias.
         * Do not replace these with cached pointers or final-only stores. */
        if ((c->r[1] & 3u) == c->r[1] && c->r[2] < 32u)
            X_W32(0x18F180u + ((c->r[1] << 5) + c->r[2]) * 4u) = X_ARG(0);
        c->r[4] += 8u;  /* X_RET(1), but continue to the next fixed write. */
    }
}
