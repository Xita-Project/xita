/* neon_x4_compat.h - force-included into --runtime units of 32-bit ARM Linux host builds (tools/host_build.py).
 * GCC < 14 has no AArch32 vld1q_u8_x4/vst1q_u8_x4 (the Vita's GCC 15 does); the runtime's NEON compares use them.
 * These are the same 64-byte loads/stores as four 16-byte ones, so results are identical. */
#pragma once
#if defined(__arm__) && !defined(__aarch64__) && defined(__ARM_NEON) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 14
#include <arm_neon.h>
static inline uint8x16x4_t xv_compat_vld1q_u8_x4(const uint8_t *p)
{ uint8x16x4_t r; r.val[0] = vld1q_u8(p); r.val[1] = vld1q_u8(p + 16); r.val[2] = vld1q_u8(p + 32); r.val[3] = vld1q_u8(p + 48); return r; }
static inline void xv_compat_vst1q_u8_x4(uint8_t *p, uint8x16x4_t v)
{ vst1q_u8(p, v.val[0]); vst1q_u8(p + 16, v.val[1]); vst1q_u8(p + 32, v.val[2]); vst1q_u8(p + 48, v.val[3]); }
#define vld1q_u8_x4 xv_compat_vld1q_u8_x4
#define vst1q_u8_x4 xv_compat_vst1q_u8_x4
#endif
