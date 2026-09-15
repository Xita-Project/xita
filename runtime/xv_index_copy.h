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
#endif
