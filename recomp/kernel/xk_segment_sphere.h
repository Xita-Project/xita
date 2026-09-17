#pragma once
/* Optional native body for Halo 3925 0xB0CB0, a segment/sphere test.
 *
 * ECX, EAX and EDX point to a segment start, sphere center and direction; the
 * radius is the stack argument (`ret 4`). The helper computes the original x87
 * program with C doubles in the original operation order, then publishes the
 * complete state the lifted body leaves:
 *
 *  - every guest store: float spills at [S] (twice) and [S+14h], and the three
 *    direction words at [S+4]/[S+8]/[S+0Ch], where S = ESP-10h;
 *  - x87 slots below TOP, the status word (each comparison clears C0-C3 and
 *    ORs its TOP), EAX/ECX/EDX, and the final lazy flags;
 *  - the taken back edge 0xB0D99 -> 0xB0CF2: context is published before
 *    X_PREEMPT and the original resume instructions then run on that context.
 *
 * Stack reloads are computed natively. Admission therefore requires S..S+18h
 * to lie in one guest page, so the original unguarded word stores and
 * page-aware float reloads read back the same bytes, and requires the
 * comparison constant's physical page to differ from that stack page.
 * Loaded NaNs decline: with finite or infinite inputs every NaN is a default
 * NaN, so operand-order differences cannot change a payload.
 *
 * Return values: 0 = run the lifted body from its entry (no state changed),
 * 1 = returned, 2 = continue the lifted body at 0xB0CFC (state published).
 * X_M32 uses the generated unit's captured roots through `xram_`/`xpt_`;
 * float loads and stores retain the global-root helpers the lifted body uses. */
#include "../xv_x86rt.h"
#ifdef XV_NATIVE_SEGMENT_SPHERE

int xv_segment_sphere_enabled(void);
void xv_segment_sphere_override(int enabled);
unsigned xv_segment_sphere_calls(void);
extern unsigned xv_segment_sphere_mode, xv_segment_sphere_count;

/* x87_compare's condition codes with the same quiet comparison and operand
 * order, so native FPSCR NZCV and exception state match the lifted body. */
static inline __attribute__((always_inline)) uint16_t xv_ss_cc(double a, double b)
{
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    uint32_t result;
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"
                     "mov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\t"
                     "it vs\n\tmovvs %0, #17664"
                     : "=r"(result) : "w"(a), "w"(b) : "cc");
    return (uint16_t)result;
#else
    if (isnan(a) || isnan(b)) return 0x4500;
    if (a < b) return 0x0100;
    if (a == b) return 0x4000;
    return 0;
#endif
}

/* The lifted body never fuses a product into an add or subtract. ARM VFP
 * multiply-accumulate instructions (VMLA/VMLS/VNML*) apply FPNeg internally,
 * which changes the sign of a NaN result, so GCC must not contract these. */
static inline __attribute__((always_inline)) double xv_ss_add(double a, double b)
{
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    double r;
    __asm__ volatile("vadd.f64 %P0, %P1, %P2" : "=w"(r) : "w"(a), "w"(b) :);
    return r;
#else
    return a + b;
#endif
}

static inline __attribute__((always_inline)) double xv_ss_sub(double a, double b)
{
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    double r;
    __asm__ volatile("vsub.f64 %P0, %P1, %P2" : "=w"(r) : "w"(a), "w"(b) :);
    return r;
#else
    return a - b;
#endif
}

static inline __attribute__((always_inline)) int xv_ss_nan(uint32_t word)
{
    return (word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu) != 0;
}

static inline __attribute__((always_inline)) void xv_ss_flags(xctx *c, uint32_t result)
{
    X_FLAGS(XK_LOGIC, 0, 0, result, 8);
}

static inline __attribute__((always_inline)) uint32_t
xv_segment_sphere(xctx *c, uint8_t *const xram_, const uint32_t *const xpt_)
{
    (void)xram_; (void)xpt_;
    if (!(__atomic_load_n(&xv_segment_sphere_mode, __ATOMIC_RELAXED) & 1u)) return 0;
#if defined(__arm__)
    { uint32_t fpscr; __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
      if (fpscr & 0x00009f00u) return 0; }
#endif
    const uint32_t entry = c->r[4], s = entry - 0x10u;
    if ((s & 4095u) > 4096u - 0x18u) return 0;
    if (xpt_[0x1F0A68u >> 12] == xpt_[s >> 12]) return 0;
    const uint32_t start = c->r[1], centre = c->r[0], direction = c->r[2];
    uint32_t w[8];
    x_guest_read(w, start, 12);
    x_guest_read(w + 3, centre, 12);
    x_guest_read(w + 6, s + 0x14u, 4);
    x_guest_read(w + 7, 0x1F0A68u, 4);
    for (unsigned i = 0; i < 8; i++)
        if (xv_ss_nan(w[i])) return 0;
    float f[8];
    memcpy(f, w, sizeof f);
    __atomic_add_fetch(&xv_segment_sphere_count, 1u, __ATOMIC_RELAXED);
    const double k = f[7];
    const uint32_t top = c->fsp;
    uint16_t fsw = c->fsw;
#define XV_SS_COMPARE(a, b, depth) \
    (fsw = (uint16_t)((fsw & ~0x4700u) | xv_ss_cc((a), (b)) | (((top - (depth)) & 7u) << 11)))

    /* B0CB3..B0CE1 */
    double x = (double)f[0] - (double)f[3];
    double y = (double)f[1] - (double)f[4];
    double z = (double)f[2] - (double)f[5];
    double s1 = z * z;
    s1 = xv_ss_add(s1, y * y);
    double q = xv_ss_add(s1, x * x);
    const double rr = (double)f[6] * (double)f[6];
    q = xv_ss_sub(q, rr);
    const float q32 = (float)q;
    x_guest_write(s, &q32, 4);
    XV_SS_COMPARE(q, k, 4u);
    uint32_t eax = (centre & ~0xFFFFu) | fsw;
    if (((fsw >> 8) & 5u) == 1u) {
        /* B0CEE: inside, AL = 1. */
        c->st[(top - 1u) & 7u] = x;
        c->st[(top - 2u) & 7u] = y;
        c->st[(top - 3u) & 7u] = z;
        c->st[(top - 4u) & 7u] = q;
        c->st[(top - 5u) & 7u] = rr;
        c->fsw = fsw;
        c->r[0] = (eax & ~0xFFu) | 1u;
        c->r[4] = entry + 8u;
        xv_ss_flags(c, 1u);
        return 1;
    }
    /* B0CFC */
    const uint32_t dx = X_M32(direction), dy = X_M32(direction + 4u), dz = X_M32(direction + 8u);
    if (xv_ss_nan(dx) || xv_ss_nan(dy) || xv_ss_nan(dz)) {
        c->st[(top - 1u) & 7u] = x;
        c->st[(top - 2u) & 7u] = y;
        c->st[(top - 3u) & 7u] = z;
        c->st[(top - 4u) & 7u] = q;
        c->st[(top - 5u) & 7u] = rr;
        c->fsp = (top - 3u) & 7u;
        c->fsw = fsw;
        c->r[0] = eax;
        c->r[4] = s;
        xv_ss_flags(c, (fsw >> 8) & 5u);
        return 2;
    }
    X_M32(s + 0xCu) = dz;
    X_M32(s + 0x8u) = dy;
    X_M32(s + 0x4u) = dx;
    float fu, fv, fw;
    memcpy(&fu, &dz, 4); memcpy(&fv, &dy, 4); memcpy(&fw, &dx, 4);
    const double u = fu, v = fv, wx_in = fw;
    double t = u * z;
    t = xv_ss_add(t, v * y);
    const double wx = wx_in * x;
    t = xv_ss_add(t, wx);
    const float t32f = (float)t;
    x_guest_write(s + 0x14u, &t32f, 4);
    const double t32 = t32f;
    XV_SS_COMPARE(t32, k, 1u);
    eax = (dx & ~0xFFFFu) | fsw;
    c->r[1] = dy;
    c->r[2] = dz;
    c->st[(top - 4u) & 7u] = t;
    c->st[(top - 5u) & 7u] = wx;
    if (!((fsw >> 8) & 1u)) {
        /* B0D80: not closing, AL = 0. */
        c->st[(top - 1u) & 7u] = t32;
        c->st[(top - 2u) & 7u] = y;
        c->st[(top - 3u) & 7u] = z;
        c->fsw = fsw;
        c->r[0] = eax & ~0xFFu;
        c->r[4] = entry + 8u;
        xv_ss_flags(c, 0u);
        return 1;
    }
    /* B0D41 */
    double l = u * u;
    l = xv_ss_add(l, v * v);
    l = xv_ss_add(l, wx_in * wx_in);
    const double tt = t32 * t32;
    const double lq = l * (double)q32;
    const double disc = xv_ss_sub(tt, lq);
    const float disc32 = (float)disc;
    x_guest_write(s, &disc32, 4);
    XV_SS_COMPARE(disc, k, 2u);
    eax = (eax & ~0xFFFFu) | fsw;
    c->st[(top - 2u) & 7u] = disc;
    c->st[(top - 3u) & 7u] = lq;
    uint32_t parity = (fsw >> 8) & 0x41u;
    if (parity == 0x01u || parity == 0x40u) {
        /* B0D7E: no real intersection, AL = 0. */
        c->st[(top - 1u) & 7u] = l;
        c->fsw = fsw;
        c->r[0] = eax & ~0xFFu;
        c->r[4] = entry + 8u;
        xv_ss_flags(c, parity);
        return 1;
    }
    /* B0D88 */
    double m = -l;
    m = xv_ss_sub(m, t32);
    XV_SS_COMPARE(m, k, 1u);
    eax = (eax & ~0xFFFFu) | fsw;
    c->st[(top - 1u) & 7u] = m;
    uint32_t result = (fsw >> 8) & 5u;
    if (result == 1u) {
        /* B0D99 taken: publish, yield, then original B0CF2. */
        c->fsp = (top - 1u) & 7u;
        c->fsw = fsw;
        c->r[0] = eax;
        c->r[4] = s;
        xv_ss_flags(c, result);
        X_PREEMPT();
        x87_pop(c);
        X_R8L(0) = 0x1u;
        c->r[4] += 0x18u;
        return 1;
    }
    /* B0D9F */
    const double mm = m * m;
    XV_SS_COMPARE(mm, (double)disc32, 2u);
    c->st[(top - 2u) & 7u] = mm;
    result = (fsw >> 8) & 5u;
    c->fsw = fsw;
    c->r[0] = result == 1u ? 1u : 0u;
    c->r[4] = entry + 8u;
    xv_ss_flags(c, result);
    return 1;
#undef XV_SS_COMPARE
}
#endif
