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
    unsigned remaining = count, maximum = 0, groups = 0;
    xv_vertex_refs_clear(refs);
    while (remaining) {
        unsigned n = remaining < 256 ? remaining : 256;
        memcpy(chunk, src, n * sizeof *chunk);
#if defined(__ARM_NEON)
        /* For longer lists, count the completed mask once. Short lists retain
         * incremental counting: even one index can address the last mask word. */
        if (count >= 256) {
            for (unsigned i = 0; i < n; i++) {
                unsigned index = chunk[i], group = index >> 3;
                refs->bits[group >> 5] |= 1u << (group & 31);
                if (index > maximum) maximum = index;
            }
        } else
#endif
        {
            for (unsigned i = 0; i < n; i++) {
                unsigned index = chunk[i], group = index >> 3, word = group >> 5;
                uint32_t bit = 1u << (group & 31);
                if (!(refs->bits[word] & bit)) { refs->bits[word] |= bit; groups++; }
                if (index > maximum) maximum = index;
            }
        }
        memcpy(dst, chunk, n * sizeof *chunk);
        src += n * sizeof *chunk; dst += n * sizeof *chunk; remaining -= n;
    }
    refs->vertices = count ? maximum + 1 : 0;
#if defined(__ARM_NEON)
    if (count >= 256) {
        uint32x4_t sum = vdupq_n_u32(0);
        unsigned words = (maximum >> 8) + 1;
        /* The 256-word mask was fully cleared above. Rounding this scan to
         * four words stays inside it, including the maximum uint16_t index. */
        for (unsigned i = 0; i < words; i += 4)
            sum = vaddq_u32(sum, vpaddlq_u16(vpaddlq_u8(vcntq_u8(
                vld1q_u8((const uint8_t *)(refs->bits + i))))));
        uint32x2_t pair = vadd_u32(vget_low_u32(sum), vget_high_u32(sum));
        groups = vget_lane_u32(vpadd_u32(pair, pair), 0);
    }
#endif
    refs->groups = groups;
    return refs->vertices;
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
