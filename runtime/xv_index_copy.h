#ifndef XV_INDEX_COPY_H
#define XV_INDEX_COPY_H
#include <stdint.h>
#include <string.h>
#include "xv_vertex_refs.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Build reference coverage from the exact retained values, never a later read
 * of guest indices or the uncached destination. Only the opt-in path uses it. */
static inline unsigned xv_index_copy_reference_bounds(void *destination, const void *source,
                                                        unsigned count, xv_vertex_refs *refs)
{
    uint16_t chunk[256];
    const uint8_t *src = source;
    uint8_t *dst = destination;
    xv_vertex_refs_clear(refs);
    while (count) {
        unsigned n = count < 256 ? count : 256;
        memcpy(chunk, src, n * sizeof *chunk);
        for (unsigned i = 0; i < n; i++) xv_vertex_refs_add(refs, chunk[i]);
        memcpy(dst, chunk, n * sizeof *chunk);
        src += n * sizeof *chunk; dst += n * sizeof *chunk; count -= n;
    }
    return refs->vertices;
}

/* Keep the exact coverage mask while separating maximum/count reductions from
 * the per-index bit insertion. Repeated triangle indices need no conditional
 * group-count update. The bitmap is only 1 KiB; NEON counts its occupied bits
 * once after capture. Small draws retain the original short path. */
static inline unsigned xv_index_copy_reference_bounds_neon(void *destination,
    const void *source, unsigned count, xv_vertex_refs *refs)
{
#if defined(__ARM_NEON)
    if (count < 256) return xv_index_copy_reference_bounds(destination, source, count, refs);
    uint16_t chunk[256];
    const uint8_t *src = source;
    uint8_t *dst = destination;
    unsigned maximum = 0;
    uint16x8_t maxima = vdupq_n_u16(0);
    xv_vertex_refs_clear(refs);
    while (count) {
        unsigned n = count < 256 ? count : 256;
        memcpy(chunk, src, n * sizeof *chunk);
        for (unsigned i = 0; i < n; i++) {
            unsigned group = chunk[i] >> 3;
            refs->bits[group >> 5] |= 1u << (group & 31);
        }
        unsigned i = 0;
        for (; i + 8 <= n; i += 8)
            maxima = vmaxq_u16(maxima, vld1q_u16(chunk + i));
        for (; i < n; i++) if (chunk[i] > maximum) maximum = chunk[i];
        memcpy(dst, chunk, n * sizeof *chunk);
        src += n * sizeof *chunk; dst += n * sizeof *chunk; count -= n;
    }
    uint16x4_t reduced = vmax_u16(vget_low_u16(maxima), vget_high_u16(maxima));
    reduced = vpmax_u16(reduced, reduced);
    reduced = vpmax_u16(reduced, reduced);
    if (vget_lane_u16(reduced, 0) > maximum) maximum = vget_lane_u16(reduced, 0);
    refs->vertices = maximum + 1;
    uint16x8_t counts = vdupq_n_u16(0);
    /* Round to four words inside the fully initialized 256-word bitmap. Extra
     * words contain zero and no access crosses its end, even for index 65535. */
    for (unsigned i = 0; i <= maximum / 256; i += 4)
        counts = vaddq_u16(counts, vpaddlq_u8(vcntq_u8(
            vld1q_u8((const uint8_t *)(refs->bits + i)))));
    uint32x4_t words = vpaddlq_u16(counts);
    uint32x2_t sum = vadd_u32(vget_low_u32(words), vget_high_u32(words));
    sum = vpadd_u32(sum, sum);
    refs->groups = vget_lane_u32(sum, 0);
    return refs->vertices;
#else
    return xv_index_copy_reference_bounds(destination, source, count, refs);
#endif
}

/* GPU scratch is uncached on Vita. Derive the vertex range from a small cached
 * snapshot, then publish those exact indices with bulk writes. Never read back
 * the GPU destination or scan the live guest list separately from its copy.
 * Byte pointers/memcpy also accept an unaligned guest source. */
static inline unsigned xv_index_copy_bounds(void *destination, const void *source,
                                             unsigned count)
{
    uint16_t chunk[256];
    const uint8_t *src = source;
    uint8_t *dst = destination;
    unsigned maximum = 0, remaining = count;
    while (remaining) {
        unsigned n = remaining < 256 ? remaining : 256;
        memcpy(chunk, src, n * sizeof *chunk);
        for (unsigned i = 0; i < n; i++)
            if (chunk[i] > maximum) maximum = chunk[i];
        memcpy(dst, chunk, n * sizeof *chunk);
        src += n * sizeof *chunk;
        dst += n * sizeof *chunk;
        remaining -= n;
    }
    return count ? maximum + 1 : 0;
}

/* ARMv7 NEON: retain and bound the very same loaded indices. In particular,
 * never scan a second time through guest memory or read the uncached GPU copy.
 * Byte loads/stores accept odd guest addresses; every access stays in range.
 * The scalar helper remains available for the controlled hardware comparison. */
static inline unsigned xv_index_copy_bounds_neon(void *destination, const void *source,
                                                 unsigned count)
{
#if defined(__ARM_NEON)
    const uint8_t *src = source;
    uint8_t *dst = destination;
    unsigned remaining = count;
    uint16x8_t max0 = vdupq_n_u16(0), max1 = max0;
    while (remaining >= 16) {
        uint8x16_t a = vld1q_u8(src), b = vld1q_u8(src + 16);
        max0 = vmaxq_u16(max0, vreinterpretq_u16_u8(a));
        max1 = vmaxq_u16(max1, vreinterpretq_u16_u8(b));
        vst1q_u8(dst, a); vst1q_u8(dst + 16, b);
        src += 32; dst += 32; remaining -= 16;
    }
    if (remaining >= 8) {
        uint8x16_t a = vld1q_u8(src);
        max0 = vmaxq_u16(max0, vreinterpretq_u16_u8(a));
        vst1q_u8(dst, a);
        src += 16; dst += 16; remaining -= 8;
    }
    uint16x8_t lanes = vmaxq_u16(max0, max1);
    uint16x4_t reduced = vmax_u16(vget_low_u16(lanes), vget_high_u16(lanes));
    reduced = vpmax_u16(reduced, reduced);
    reduced = vpmax_u16(reduced, reduced);
    unsigned maximum = vget_lane_u16(reduced, 0);
    while (remaining--) {
        uint16_t value;
        memcpy(&value, src, sizeof value);
        memcpy(dst, &value, sizeof value);
        if (value > maximum) maximum = value;
        src += 2; dst += 2;
    }
    return count ? maximum + 1 : 0;
#else
    return xv_index_copy_bounds(destination, source, count);
#endif
}
/* ---- XV_REC_INDEX: single-read variants -----------------------------------------------------------------------
 * Every guest index is loaded once; that value is the source of the GPU copy, the optional cached mirror (the
 * index cache's exact snapshot), the maximum and the coverage bits. Results equal the functions above: identical
 * destination and mirror bytes, vertices, coverage bitmap and group count. */

static inline unsigned xv_index_max(const uint16_t *chunk, unsigned n)
{
    unsigned maximum = 0, i = 0;
#if defined(__ARM_NEON)
    if (n >= 16) {
        uint16x8_t m0 = vld1q_u16(chunk), m1 = vld1q_u16(chunk + 8);
        for (i = 16; i + 16 <= n; i += 16) {
            m0 = vmaxq_u16(m0, vld1q_u16(chunk + i));
            m1 = vmaxq_u16(m1, vld1q_u16(chunk + i + 8));
        }
        uint16x8_t m = vmaxq_u16(m0, m1);
        uint16x4_t r = vmax_u16(vget_low_u16(m), vget_high_u16(m));
        r = vpmax_u16(r, r); r = vpmax_u16(r, r);
        maximum = vget_lane_u16(r, 0);
    }
#endif
    for (; i < n; i++) if (chunk[i] > maximum) maximum = chunk[i];
    return maximum;
}

/* Set bits among the words that can hold them (index <= maximum). */
static inline unsigned xv_index_refs_groups(const uint32_t *bits, unsigned maximum)
{
    unsigned words = maximum / 256u + 1u, groups = 0, i = 0;
#if defined(__ARM_NEON)
    uint16x8_t counts = vdupq_n_u16(0);
    for (; i + 4 <= words; i += 4)
        counts = vaddq_u16(counts, vpaddlq_u8(vcntq_u8(vld1q_u8((const uint8_t *)(bits + i)))));
    uint32x4_t w4 = vpaddlq_u16(counts);
    uint32x2_t sum = vadd_u32(vget_low_u32(w4), vget_high_u32(w4));
    sum = vpadd_u32(sum, sum);
    groups = vget_lane_u32(sum, 0);
#endif
    for (; i < words; i++) groups += (unsigned)__builtin_popcount(bits[i]);
    return groups;
}

/* Referenced 8-vertex groups as a byte map: one independent byte store per index (no read-modify-write chain
 * through the bitmap word), packed into the bitmap once per call. Recording owner only; all zero between calls. */
static uint8_t xv_index_group_map[8192];
static inline void xv_index_pack_groups(uint32_t *bits, unsigned words)
{
    const uint8_t *g = xv_index_group_map;
    unsigned w = 0;
#if defined(__ARM_NEON)
    static const uint8_t weights_[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t weights = vld1q_u8(weights_);
    for (; w < words; w++, g += 32) {
        uint8x16_t a = vld1q_u8(g), b = vld1q_u8(g + 16);
        a = vandq_u8(vtstq_u8(a, a), weights); b = vandq_u8(vtstq_u8(b, b), weights);
        uint8x8_t x = vpadd_u8(vget_low_u8(a), vget_high_u8(a)), y = vpadd_u8(vget_low_u8(b), vget_high_u8(b));
        uint8x8_t z = vpadd_u8(x, y);
        z = vpadd_u8(z, z);
        vst1_lane_u32(&bits[w], vreinterpret_u32_u8(z), 0);
    }
#endif
    for (; w < words; w++, g += 32) {
        uint32_t word = 0;
        for (unsigned b = 0; b < 32; b++) word |= (uint32_t)(g[b] != 0) << b;
        bits[w] = word;
    }
}

/* xv_index_copy_reference_bounds(_neon) with an optional mirror written in the same pass (count >= 1). */
static inline unsigned xv_index_copy2_reference_bounds(void *destination, void *mirror, const void *source,
    unsigned count, xv_vertex_refs *refs)
{
    uint16_t chunk[256];
    const uint8_t *src = source;
    uint8_t *dst = destination, *mir = mirror;
    uint8_t *map = xv_index_group_map;
    unsigned maximum = 0;
    while (count) {
        unsigned n = count < 256 ? count : 256;
        uint16_t *c = mir ? (uint16_t *)(void *)mir : chunk;   /* the mirror is cached, 2-byte aligned */
        memcpy(c, src, n * sizeof *c);
        for (unsigned i = 0; i < n; i++) map[c[i] >> 3] = 1;
        unsigned m = xv_index_max(c, n);
        if (m > maximum) maximum = m;
        memcpy(dst, c, n * sizeof *c);
        src += n * sizeof *c; dst += n * sizeof *c; if (mir) mir += n * sizeof *c; count -= n;
    }
    unsigned words = maximum / 256u + 1u;
    xv_index_pack_groups(refs->bits, words);
    memset(refs->bits + words, 0, (256u - words) * sizeof refs->bits[0]);
    memset(map, 0, words * 32u);
    refs->vertices = maximum + 1;
    refs->groups = xv_index_refs_groups(refs->bits, maximum);
    return refs->vertices;
}

/* xv_index_copy_bounds(_neon) with an optional mirror written from the same loads (count >= 1). */
static inline unsigned xv_index_copy2_bounds(void *destination, void *mirror, const void *source, unsigned count)
{
    const uint8_t *src = source;
    uint8_t *dst = destination, *mir = mirror;
    unsigned remaining = count, maximum = 0;
#if defined(__ARM_NEON)
    uint16x8_t max0 = vdupq_n_u16(0), max1 = max0;
    while (remaining >= 16) {
        uint8x16_t a = vld1q_u8(src), b = vld1q_u8(src + 16);
        max0 = vmaxq_u16(max0, vreinterpretq_u16_u8(a));
        max1 = vmaxq_u16(max1, vreinterpretq_u16_u8(b));
        if (mir) { vst1q_u8(mir, a); vst1q_u8(mir + 16, b); mir += 32; }
        vst1q_u8(dst, a); vst1q_u8(dst + 16, b);
        src += 32; dst += 32; remaining -= 16;
    }
    uint16x8_t lanes = vmaxq_u16(max0, max1);
    uint16x4_t reduced = vmax_u16(vget_low_u16(lanes), vget_high_u16(lanes));
    reduced = vpmax_u16(reduced, reduced); reduced = vpmax_u16(reduced, reduced);
    maximum = vget_lane_u16(reduced, 0);
#endif
    while (remaining--) {
        uint16_t value;
        memcpy(&value, src, sizeof value);
        if (mir) { memcpy(mir, &value, sizeof value); mir += 2; }
        memcpy(dst, &value, sizeof value);
        if (value > maximum) maximum = value;
        src += 2; dst += 2;
    }
    return count ? maximum + 1 : 0;
}

/* Index-cache hit: copy the guest indices to the GPU slot while checking them against the exact mirror, one read
 * of each. 1 = equal and fully copied (the same bytes the old mirror copy wrote); 0 = a difference was found and the
 * copy stopped (the caller takes the miss path, which rewrites the same destination). */
static inline int xv_index_copy_if_equal(void *destination, const void *source, const void *mirror, unsigned count)
{
    const uint8_t *src = source, *mir = mirror;
    uint8_t *dst = destination;
    unsigned bytes = count * 2u;
#if defined(__ARM_NEON)
    while (bytes >= 64) {
        uint8x16_t a0 = vld1q_u8(src), a1 = vld1q_u8(src + 16), a2 = vld1q_u8(src + 32), a3 = vld1q_u8(src + 48);
        uint8x16_t d = vorrq_u8(vorrq_u8(veorq_u8(a0, vld1q_u8(mir)), veorq_u8(a1, vld1q_u8(mir + 16))),
                                vorrq_u8(veorq_u8(a2, vld1q_u8(mir + 32)), veorq_u8(a3, vld1q_u8(mir + 48))));
        uint32x2_t r = vreinterpret_u32_u8(vorr_u8(vget_low_u8(d), vget_high_u8(d)));
        if (vget_lane_u32(vpmax_u32(r, r), 0)) return 0;
        vst1q_u8(dst, a0); vst1q_u8(dst + 16, a1); vst1q_u8(dst + 32, a2); vst1q_u8(dst + 48, a3);
        src += 64; mir += 64; dst += 64; bytes -= 64;
    }
#endif
    while (bytes >= 4) {
        uint32_t a, m; memcpy(&a, src, 4); memcpy(&m, mir, 4);
        if (a != m) return 0;
        memcpy(dst, &a, 4); src += 4; mir += 4; dst += 4; bytes -= 4;
    }
    if (bytes) {
        uint16_t a, m; memcpy(&a, src, 2); memcpy(&m, mir, 2);
        if (a != m) return 0;
        memcpy(dst, &a, 2);
    }
    return 1;
}
#endif
