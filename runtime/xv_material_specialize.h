/* Proofs over the actual decoded upload, not texture names or dimensions. */
#ifndef XV_MATERIAL_SPECIALIZE_H
#define XV_MATERIAL_SPECIALIZE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline int xv_material_black_rgba(const uint32_t *pixels, unsigned w,
                                       unsigned h, unsigned levels)
{
    /* Initial scope is tiny decoded placeholders. BC/cube uploads are not
     * covered. All uploaded mip samples must agree; alpha is intentionally
     * irrelevant to the two supported programs' stage-3 RGB use. */
    if (!pixels || !w || !h || w > 4 || h > 4 || !levels || levels > 3) return 0;
    for (unsigned l = 0; l < levels; ++l) {
        unsigned stride = (w + 7u) & ~7u;
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x)
                if (pixels[(size_t)y * stride + x] & 0x00ffffffu) return 0;
        pixels += (size_t)stride * h;
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
    }
    return 1;
}

static inline int xv_material_axis_constants(const float psc[18][4])
{
    return psc[0][0] == 1.0f && psc[0][1] == 0.0f && psc[0][2] == 0.0f &&
           psc[8][0] == 0.0f && psc[8][1] == 1.0f && psc[8][2] == 0.0f &&
           psc[5][0] == 0.0f && psc[5][1] == 0.0f && psc[5][2] == 0.0f;
}
/* Mode 6 combines the existing RGB proof with the captured GREATER alpha
 * policy. Reference remains a uniform; no alpha threshold is guessed. */
static inline int xv_material_black_alpha_mode(int proven, int mode,
                                               uint32_t key, uint32_t atest,
                                               int greater_enabled)
{
    if (!proven || (mode != 0 && mode != 1 && mode != 2)) return mode;
    if (mode == 1) return 4;
    if (greater_enabled && key == 0x154066FDu &&
        (atest & (1u << 16)) && ((atest >> 8) & 7u) == 4u) return 6;
    return mode == 0 ? 5 : mode;
}
/* Only extend the already admitted GREATER axis/black material. Signed zero
 * is excluded so literal +0 substitution preserves even the captured bits. */
static inline int xv_material_nocolor_mode(int mode, const float psc[18][4])
{
    if (mode != 6) return mode;
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t zero, one;
        memcpy(&zero, &psc[1][i], sizeof zero);
        memcpy(&one, &psc[2][i], sizeof one);
        if (zero != 0u || one != 0x3f800000u) return mode;
    }
    return 8;
}
#endif
