#ifndef XV_PACKED_VERTEX_H
#define XV_PACKED_VERTEX_H
#ifndef XV_PACKED_VERTEX_LAYOUT
#define XV_PACKED_VERTEX_LAYOUT 0
#endif
#if XV_PACKED_VERTEX_LAYOUT != 0 && XV_PACKED_VERTEX_LAYOUT != 1
#error XV_PACKED_VERTEX_LAYOUT must be 0 or 1
#endif
#if XV_PACKED_VERTEX_LAYOUT
#include <stdint.h>
#include <string.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
/* The sole admitted representation: exact bytes [0,16) of each 32-byte
 * record. No float conversion, overlapping destination, or guest pointer
 * publication. The caller owns the source loan and destination span. */
#define XV_PACKED_PREFIX16 1u
static inline int xv_packed_equal(const void *source, const void *packed, unsigned vertices)
{
    const uint8_t *a=source,*b=packed;
#if defined(__ARM_NEON)
    while(vertices>=4) {
        uint8x16x4_t bv=vld1q_u8_x4(b);
        uint8x16_t d0=veorq_u8(vld1q_u8(a),bv.val[0]);
        uint8x16_t d1=veorq_u8(vld1q_u8(a+32),bv.val[1]);
        uint8x16_t d2=veorq_u8(vld1q_u8(a+64),bv.val[2]);
        uint8x16_t d3=veorq_u8(vld1q_u8(a+96),bv.val[3]);
        uint8x16_t d=vorrq_u8(vorrq_u8(d0,d1),vorrq_u8(d2,d3));
        uint32x2_t r=vreinterpret_u32_u8(vorr_u8(vget_low_u8(d),vget_high_u8(d)));
        if(vget_lane_u32(vpmax_u32(r,r),0))return 0;
        a+=128;b+=64;vertices-=4;
    }
    while(vertices--) {
        uint8x16_t d=veorq_u8(vld1q_u8(a),vld1q_u8(b));
        uint32x2_t r=vreinterpret_u32_u8(vorr_u8(vget_low_u8(d),vget_high_u8(d)));
        if(vget_lane_u32(vpmax_u32(r,r),0))return 0;
        a+=32;b+=16;
    }
#else
    while(vertices--) { if(memcmp(a,b,16))return 0;a+=32;b+=16; }
#endif
    return 1;
}
static inline void xv_packed_copy(void *packed,const void *source,unsigned vertices)
{
    uint8_t *b=packed;const uint8_t *a=source;
#if defined(__ARM_NEON)
    while(vertices>=4) {
        uint8x16x4_t v={{vld1q_u8(a),vld1q_u8(a+32),vld1q_u8(a+64),vld1q_u8(a+96)}};
        vst1q_u8_x4(b,v);a+=128;b+=64;vertices-=4;
    }
#endif
    while(vertices--) {
#if defined(__ARM_NEON)
        vst1q_u8(b,vld1q_u8(a));
#else
        memcpy(b,a,16);
#endif
        a+=32;b+=16;
    }
}
#endif
#endif
