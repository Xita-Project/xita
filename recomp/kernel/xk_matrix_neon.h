/* Optional matrix SIMD path, owned by the serialized guest thread.
 * Include only from xk_math.c with XV_NATIVE_MATRIX_NEON enabled.
 * Layout/overlap guards in the caller run before this helper. All numeric
 * checks precede writes. Left rotation vectors are loaded before output;
 * right rows are consumed before their same-position output is written, so
 * the caller's exact in-place aliases remain valid. Partial overlap declines.
 *
 * NEON does not accumulate VFP exception flags. Require masked exceptions,
 * nearest rounding and preexisting IXC. Bounded finite inputs prevent any
 * new overflow/underflow/invalid/input-denormal exception. The smallest
 * possible nonzero translation intermediate stays above 2^-126, and the
 * largest stays below 2^128. Signed zeros and every guest SSE lane are kept.
 */
#pragma once
#include <arm_neon.h>

static unsigned matrix_neon_accepted, matrix_neon_disabled;
static unsigned matrix_neon_fp, matrix_neon_numeric;
static int matrix_neon_override = -1;
void xv_matrix_neon_override(int value)
{
    matrix_neon_override = value < 0 ? -1 : !!value;
}
static int matrix_neon_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("XV_NATIVE_MATRIX_NEON");
        enabled = value && atoi(value) != 0;
    }
    return matrix_neon_override < 0 ? enabled : matrix_neon_override;
}
static void matrix_neon_report(unsigned frames)
{
    XK_LOG("[matrix-neon] %u frames accepted %u disabled %u fp %u numeric %u\n",
           frames, matrix_neon_accepted, matrix_neon_disabled, matrix_neon_fp, matrix_neon_numeric);
    matrix_neon_accepted = matrix_neon_disabled = matrix_neon_fp = matrix_neon_numeric = 0;
}
static int matrix_neon_try(const float l[13], const float r[13], float op[13], float v[8][4])
{
    if (!matrix_neon_enabled()) {
        matrix_neon_disabled++;
        return 0;
    }
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    if ((fpscr & 0x00c09f10u) != 0x10u) {
        matrix_neon_fp++;
        return 0;
    }
    /* 2^-30 <= abs(value) <= 2^30, or signed zero. Products and sums stay
     * finite and normal (or exact zero), so NEON cannot hide under/overflow. */
    uint32x4_t bad = vdupq_n_u32(0), mask = vdupq_n_u32(0x7fffffffu);
    uint32x4_t minimum = vdupq_n_u32(0x30800000u);
    uint32x4_t span = vdupq_n_u32(0x1e000000u), zero = vdupq_n_u32(0);
    for (unsigned i = 0; i < 12; i += 4) {
        uint32x4_t x = vandq_u32(vreinterpretq_u32_f32(vld1q_f32(l + i)), mask);
        uint32x4_t y = vandq_u32(vreinterpretq_u32_f32(vld1q_f32(r + i)), mask);
        uint32x4_t xok = vorrq_u32(vcleq_u32(vsubq_u32(x, minimum), span),
                                 vceqq_u32(x, zero));
        uint32x4_t yok = vorrq_u32(vcleq_u32(vsubq_u32(y, minimum), span),
                                 vceqq_u32(y, zero));
        bad = vorrq_u32(bad, vmvnq_u32(vandq_u32(xok, yok)));
    }
    uint32x2_t half = vorr_u32(vget_low_u32(bad), vget_high_u32(bad));
    uint32_t a, b;
    memcpy(&a, l + 12, 4);
    memcpy(&b, r + 12, 4);
    a &= 0x7fffffffu;
    b &= 0x7fffffffu;
    if ((vget_lane_u32(half, 0) | vget_lane_u32(half, 1)) ||
        (a && (a < 0x30800000u || a > 0x4e800000u)) ||
        (b && (b < 0x30800000u || b > 0x4e800000u))) {
        matrix_neon_numeric++;
        return 0;
    }
    float32x4_t left[3];
    for (unsigned i = 0; i < 3; i++) {
        float32x4_t raw = vld1q_f32(l + 1 + i * 3);
        float32x4_t q = vextq_f32(raw, raw, 3);
        q = vsetq_lane_f32(vgetq_lane_f32(raw, 0), q, 0);
        q = vsetq_lane_f32(0, q, 1);
        left[i] = q;
        vst1q_f32(v[i], q);
    }
    for (unsigned row = 0; row < 3; row++) {
        float32x4_t q = vmulq_n_f32(left[0], r[1 + row * 3]);
        q = vmlaq_n_f32(q, left[1], r[2 + row * 3]);
        q = vmlaq_n_f32(q, left[2], r[3 + row * 3]);
        op[1 + row * 3] = vgetq_lane_f32(q, 0);
        op[2 + row * 3] = vgetq_lane_f32(q, 2);
        op[3 + row * 3] = vgetq_lane_f32(q, 3);
        if (row == 2) {
            v[7][0] = vgetq_lane_f32(q, 3);
            v[7][1] = vgetq_lane_f32(q, 3);
            v[7][2] = vgetq_lane_f32(q, 0);
            v[7][3] = vgetq_lane_f32(q, 2);
        }
    }
    float temp[4] = {l[10], 0, l[11], l[12]};
    float32x4_t offset = vld1q_f32(temp);
    float32x4_t middle = vmulq_n_f32(left[1], r[11]);
    float32x4_t sum = vmlaq_n_f32(middle, left[0], r[10]);
    sum = vmlaq_n_f32(sum, left[2], r[12]);
    float32x4_t translated = vmlaq_n_f32(offset, sum, l[0]);
    vst1q_f32(v[3], translated);
    vst1q_f32(v[4], middle);
    vst1q_f32(v[5], vdupq_n_f32(l[0]));
    vst1q_f32(v[6], offset);
    op[10] = vgetq_lane_f32(translated, 0);
    op[11] = vgetq_lane_f32(translated, 2);
    op[12] = vgetq_lane_f32(translated, 3);
    matrix_neon_accepted++;
    return 1;
}
