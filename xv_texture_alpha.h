/* Conservative proofs over uploaded samples, never over a sampled source hash.
 * BC block ordering does not affect opacity. Include every uploaded mip block,
 * even the unused texels in partial blocks. Cubes are initially unsupported. */
#pragma once
#include <stddef.h>
#include <stdint.h>

static inline int xv_alpha_bc_opaque(const uint8_t *p, size_t bytes, unsigned fmt)
{
    unsigned block = fmt == 0x0c ? 8 : 16;
    if (!p || !bytes || bytes % block || (fmt != 0x0c && fmt != 0x0e && fmt != 0x0f)) return 0;
    for (size_t off = 0; off < bytes; off += block) {
        const uint8_t *b = p + off;
        if (fmt == 0x0c) {
            unsigned a = b[0] | b[1] << 8, z = b[2] | b[3] << 8;
            if (a <= z) for (unsigned i = 0; i < 16; i++)
                if (((b[4 + i / 4] >> ((i % 4) * 2)) & 3) == 3) return 0;
        } else if (fmt == 0x0e) {
            for (unsigned i = 0; i < 8; i++) if (b[i] != 255) return 0;
        } else {
            unsigned a[8] = {b[0], b[1]};
            for (unsigned i = 2; i < 8; i++)
                a[i] = a[0] > a[1] ? ((8-i)*a[0] + (i-1)*a[1])/7 :
                    i == 6 ? 0 : i == 7 ? 255 : ((6-i)*a[0] + (i-1)*a[1])/5;
            uint64_t bits = 0;
            for (unsigned i = 0; i < 6; i++) bits |= (uint64_t)b[i+2] << (8*i);
            for (unsigned i = 0; i < 16; i++) if (a[(bits >> (3*i)) & 7] != 255) return 0;
        }
    }
    return 1;
}

static inline int xv_alpha_rgba_opaque(const uint32_t *p, unsigned w, unsigned h, unsigned levels)
{
    if (!p || !w || !h || !levels || w > 4096 || h > 4096 || levels > 12) return 0;
    for (unsigned l = 0; l < levels; l++) {
        unsigned stride = (w + 7u) & ~7u;
        for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++)
            if (p[(size_t)y * stride + x] >> 24 != 255) return 0;
        p += (size_t)stride * h;
        w = w > 1 ? w >> 1 : 1; h = h > 1 ? h >> 1 : 1;
    }
    return 1;
}

/* Exact acceptance of alpha 1 by the current shader's 8-bit reference test.
 * Its equality tolerance .002 is less than one reference step (1/255). */
static inline int xv_alpha_accepts_opaque(uint32_t atest)
{
    unsigned ref = atest & 255, func = (atest >> 8) & 7;
    if (!(atest & (1u << 16))) return 1;
    switch (func) {
    case 2: case 3: return ref == 255;
    case 4: case 5: return ref < 255;
    case 6: case 7: return 1;
    default: return 0;
    }
}
