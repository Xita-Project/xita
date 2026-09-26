/* Proofs over the actual decoded upload, not texture names or dimensions. */
#ifndef XV_MATERIAL_SPECIALIZE_H
#define XV_MATERIAL_SPECIALIZE_H
#include <stdint.h>
#include <stddef.h>

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
#endif
