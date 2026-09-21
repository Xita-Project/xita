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
