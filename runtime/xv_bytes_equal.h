#ifndef XV_BYTES_EQUAL_H
#define XV_BYTES_EQUAL_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Exact equality for cached vertex bytes. Unlike memcmp, ordering is not
 * needed. Every vector load stays inside the requested ranges, including
 * unaligned inputs and short tails. This does not hash or transform data. */
static inline int xv_bytes_equal(const void *left, const void *right, size_t bytes)
{
#if defined(__ARM_NEON)
    const uint8_t *a = left, *b = right;
    if (a == b) return 1;
    while (bytes >= 64) {
        uint8x16_t d0 = veorq_u8(vld1q_u8(a), vld1q_u8(b));
        uint8x16_t d1 = veorq_u8(vld1q_u8(a+16), vld1q_u8(b+16));
        uint8x16_t d2 = veorq_u8(vld1q_u8(a+32), vld1q_u8(b+32));
        uint8x16_t d3 = veorq_u8(vld1q_u8(a+48), vld1q_u8(b+48));
        uint8x16_t d = vorrq_u8(vorrq_u8(d0,d1), vorrq_u8(d2,d3));
        uint32x2_t reduced = vreinterpret_u32_u8(vorr_u8(vget_low_u8(d), vget_high_u8(d)));
        if (vget_lane_u32(vpmax_u32(reduced,reduced),0)) return 0;
        a += 64; b += 64; bytes -= 64;
    }
    while (bytes >= 16) {
        uint8x16_t d = veorq_u8(vld1q_u8(a), vld1q_u8(b));
        uint32x2_t reduced = vreinterpret_u32_u8(vorr_u8(vget_low_u8(d), vget_high_u8(d)));
        if (vget_lane_u32(vpmax_u32(reduced,reduced),0)) return 0;
        a += 16; b += 16; bytes -= 16;
    }
    while (bytes--) if (*a++ != *b++) return 0;
    return 1;
#else
    return !bytes || !memcmp(left, right, bytes);
#endif
}

/* Optional vertex comparator: the same 64-byte checks and tail as above,
 * expressed as multi-vector loads to reduce scalar address preparation.
 * No alignment assumption, prefetch, hash, or read beyond the span. */
static inline int xv_bytes_equal_blocks(const void *left, const void *right, size_t bytes)
{
#if defined(__ARM_NEON)
    const uint8_t *a = left, *b = right;
    if (a == b) return 1;
    while (bytes >= 64) {
        uint8x16x4_t av = vld1q_u8_x4(a), bv = vld1q_u8_x4(b);
        uint8x16_t d0 = veorq_u8(av.val[0], bv.val[0]);
        uint8x16_t d1 = veorq_u8(av.val[1], bv.val[1]);
        uint8x16_t d2 = veorq_u8(av.val[2], bv.val[2]);
        uint8x16_t d3 = veorq_u8(av.val[3], bv.val[3]);
        uint8x16_t d = vorrq_u8(vorrq_u8(d0,d1), vorrq_u8(d2,d3));
        uint32x2_t reduced = vreinterpret_u32_u8(vorr_u8(vget_low_u8(d), vget_high_u8(d)));
        if (vget_lane_u32(vpmax_u32(reduced,reduced),0)) return 0;
        a += 64; b += 64; bytes -= 64;
    }
    return xv_bytes_equal(a,b,bytes);
#else
    return xv_bytes_equal(left,right,bytes);
#endif
}
#endif
