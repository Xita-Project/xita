/* xk_native_606b0.c - native Halo CE (Xbox 3925) lens-flare scene work, exact to the lifted guest code:
 *
 *  1. f_000606B0 (render lens flares, once per frame on the scene thread): the PURE PER-FLARE REGION. For every entry
 *     of the frame's flare list (0x2C76D0, 40-byte rows, count at 0x2E34E0) the guest decodes the packed normal
 *     (f_00060E90), checks the view stage / area / brightness / reflection count, builds the camera-relative
 *     position and its reflection about the view axis, reads the brightness byte (f_0005FE30), fades it by the
 *     definition's distance range, computes the rotation (f_00060000: one of four modes, fpatan), the screen angle
 *     (fpatan of two camera-matrix dots), normalizes the offset (f_00011120), clamps three edge fades to [0,1] and then
 *     walks the definition's reflections (0x80-byte records): per reflection a brightness product; reflections with
 *     brightness <= 0 are skipped. Only a reflection that is drawn leaves the pure code: the draw path (texture and
 *     render state, 173F20/1260F0 color animation, 11B60 color pack, 64480/61FE0/7F210 D3D setters, 63E80 the quad)
 *     stays guest code. On the Vita's a10 (perf181t) ~183 flares/frame enter the loop, ~174 reach the reflections and
 *     only ~15 reflections/frame are drawn: the region holds 606B0's 3.6 ms self time and 60E90/5FE30/60000/11120
 *     (~1.4 ms incl. timer overhead).
 *     Boundary: label hooks in the guest body. Entry labels L_000606FC (first flare), L_00060700 (next flare),
 *     L_00060AC0 (next reflection after a drawn one), L_00060DA6 (next flare after a drawn one); the native runs from
 *     there until the guest would reach L_00060B13 (a reflection to draw) or L_00060DC0 (the flare loop is done) and
 *     the hook jumps there. Verify mode snapshots, runs the native, restores and lets the guest run the same region;
 *     probes at the two exit labels compare the guest's state with the native's.
 *
 *  2. f_000602F0 (collect a cluster's BSP lens-flare markers into the flare list, 5 calls/frame from f_00092890):
 *     the whole function with its callees B1260 (perpendicular), 11120 x2 (normalize), 61270 x2 (11/11/10 pack with the
 *     CRT floor f_00019E7B -> f_0001EC1F/f_0001EABA), 5FE80 (append to the flare list). Entry hook.
 *
 * Exact by construction (both):
 *  - registers and the lazy-flag record exactly as the lifted code leaves them (flag writes the recompiler dropped as
 *    dead are not made; the stale carry/overflow cells of inc/dec, shifts and imul included);
 *  - x87: the slots below the entry top are depth-indexed locals updated like the emulator's st[] (pushes, fxch swaps,
 *    dead slots included); fsw from every compare with the TOP field the emulator stores; float math in doubles with
 *    the guest's operand order; loads widened from float, stores rounded to float; the unit is compiled
 *    -ffp-contract=off (no fused multiply-add);
 *  - the guest stack: the native runs on a host shadow of a window around esp (read once) and performs every stack
 *    store the guest performs (locals, return addresses, callee frames), then writes the window back, so every dead
 *    stack byte ends as the guest leaves it;
 *  - every other guest access translates like the shards (the thread's page table, X_IMG rules of the build): integer
 *    loads single-translation like X_M8/16/32, float loads page-split like x87_load_f32; an access whose bytes fall in
 *    the shadowed window (by guest address, or by host address for a single-translation access straddling a page) is
 *    served from the shadow, so data aliasing the stack behaves like the guest;
 *  - the back-edge budget (c->preempt) drops by exactly the guest's back-edge count and xv_preempt() is called the same
 *    number of times, at the end of the region/call (a scheduling point only; the scene helper never yields there).
 * Not reproduced: which NaN payload a two-NaN operation propagates (host compiler operand order; the guest body built
 * -O0 and -O2 already disagrees); the scene phase timers around the inlined callees (host timing only).
 *
 * XV_NATIVE_606B0 build flag (hooks: tools/patch_native_606b0_hooks.py). Env XV_NATIVE_606B0 (region of f_000606B0)
 * and XV_NATIVE_602F0 (f_000602F0): 0 off (default XV_NATIVE_606B0_DEFAULT / XV_NATIVE_602F0_DEFAULT), 1 verify
 * (native, then the guest on the same state, compare everything, keep the guest result), 2 native. _TIME=1 times the
 * native and/or guest. Counters: [native-606b0] / [native-602f0] every 60 frames. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef XV_NATIVE_606B0_DEFAULT
#define XV_NATIVE_606B0_DEFAULT 0
#endif
#ifndef XV_NATIVE_602F0_DEFAULT
#define XV_NATIVE_602F0_DEFAULT 0
#endif

/* ---------------------------------------------------------------------------------------------------------------
 * Guest memory with a shadowed stack window.
 * --------------------------------------------------------------------------------------------------------------- */
typedef struct {
    uint8_t *ram; const uint32_t *pt; uint8_t *img;
    uint32_t wlo, win;              /* the window [wlo, wlo + win) lives in S */
    uint8_t *S;
    const uint8_t *h0, *h1;         /* host bytes of the window's first / second page part (single-translation straddles) */
    uint32_t n0, n1;
} nm;
#define NM_HP(m, a) ((m)->ram + (m)->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define NM_IP(m, a) NM_HP(m, a)
#else
#define NM_IP(m, a) ((m)->img + (uint32_t)(a))
#endif
static void nm_init(nm *m, uint8_t *S, uint32_t wlo, uint32_t win)
{
    m->ram = g_xram; m->pt = X_PT; m->img = X_IMG_BASE; m->S = S; m->wlo = wlo; m->win = win;
    uint32_t first = 0x1000u - (wlo & 0xFFFu); if (first > win) first = win;
    m->h0 = NM_HP(m, wlo); m->n0 = first;
    m->h1 = first < win ? NM_HP(m, wlo + first) : m->h0; m->n1 = win - first;
    x_guest_read_pages(S, wlo, win);
}
static inline int nm_in_window(const nm *m, uint32_t a, uint32_t n)   /* guest range [a, a+n) meets the window */
{
    return (uint32_t)(a - m->wlo) < m->win || (uint32_t)(m->wlo - a) < n;
}
/* One host byte of a single-translation access: the shadow when it is a window byte. */
static uint8_t nm_hbyte(const nm *m, const uint8_t *p)
{
    if ((uintptr_t)(p - m->h0) < m->n0) return m->S[p - m->h0];
    if (m->n1 && (uintptr_t)(p - m->h1) < m->n1) return m->S[m->n0 + (p - m->h1)];
    return *p;
}
static void nm_hbyte_w(const nm *m, uint8_t *p, uint8_t v)
{
    if ((uintptr_t)(p - m->h0) < m->n0) { m->S[p - m->h0] = v; return; }
    if (m->n1 && (uintptr_t)(p - m->h1) < m->n1) { m->S[m->n0 + (p - m->h1)] = v; return; }
    *p = v;
}
static __attribute__((noinline)) uint32_t nm_rd_slow(const nm *m, uint32_t a, unsigned n)
{
    const uint8_t *p = NM_HP(m, a); uint32_t v = 0;
    for (unsigned j = 0; j < n; ++j) v |= (uint32_t)nm_hbyte(m, p + j) << (8 * j);
    return v;
}
/* X_M8/X_M16/X_M32: one translation of the first byte (a straddling access reads the host-adjacent bytes). */
static inline __attribute__((always_inline)) uint32_t nm_rd(const nm *m, uint32_t a, unsigned n)
{
    if (__builtin_expect(!nm_in_window(m, a, n) && (a & 0xFFFu) <= 0x1000u - n, 1)) {
        const uint8_t *p = NM_HP(m, a);
        if (n == 1) return *p;
        if (n == 2) { uint16_t v; memcpy(&v, p, 2); return v; }
        uint32_t v; memcpy(&v, p, 4); return v;
    }
    return nm_rd_slow(m, a, n);
}
static __attribute__((noinline)) void nm_wr_slow(const nm *m, uint32_t a, unsigned n, uint32_t v)
{
    uint8_t *p = NM_HP(m, a);
    for (unsigned j = 0; j < n; ++j) nm_hbyte_w(m, p + j, (uint8_t)(v >> (8 * j)));
}
static inline __attribute__((always_inline)) void nm_wr(const nm *m, uint32_t a, unsigned n, uint32_t v)
{
    if (__builtin_expect(!nm_in_window(m, a, n) && (a & 0xFFFu) <= 0x1000u - n, 1)) {
        uint8_t *p = NM_HP(m, a);
        if (n == 1) *p = (uint8_t)v;
        else if (n == 2) { uint16_t w = (uint16_t)v; memcpy(p, &w, 2); }
        else memcpy(p, &v, 4);
        return;
    }
    nm_wr_slow(m, a, n, v);
}
/* x87_load_f32 / x_guest_read / x_guest_write (ld/st of the string instructions): page-split, i.e. every byte from
 * its own guest address. */
static __attribute__((noinline)) void nm_gread_slow(const nm *m, void *dst, uint32_t a, unsigned n)
{
    uint8_t *d = dst;
    for (unsigned j = 0; j < n; ++j) {
        uint32_t b = a + j, o = b - m->wlo;
        d[j] = o < m->win ? m->S[o] : *NM_HP(m, b);
    }
}
static inline __attribute__((always_inline)) void nm_gread(const nm *m, void *dst, uint32_t a, unsigned n)
{
    if (__builtin_expect(!nm_in_window(m, a, n) && (a & 0xFFFu) <= 0x1000u - n, 1)) { memcpy(dst, NM_HP(m, a), n); return; }
    nm_gread_slow(m, dst, a, n);
}
static __attribute__((noinline)) void nm_gwrite_slow(const nm *m, uint32_t a, const void *src, unsigned n)
{
    const uint8_t *s = src;
    for (unsigned j = 0; j < n; ++j) {
        uint32_t b = a + j, o = b - m->wlo;
        if (o < m->win) m->S[o] = s[j]; else *NM_HP(m, b) = s[j];
    }
}
static inline __attribute__((always_inline)) void nm_gwrite(const nm *m, uint32_t a, const void *src, unsigned n)
{
    if (__builtin_expect(!nm_in_window(m, a, n) && (a & 0xFFFu) <= 0x1000u - n, 1)) { memcpy(NM_HP(m, a), src, n); return; }
    nm_gwrite_slow(m, a, src, n);
}
static inline __attribute__((always_inline)) double nm_f32(const nm *m, uint32_t a) { float v; nm_gread(m, &v, a, 4); return (double)v; }
static inline __attribute__((always_inline)) double nm_f64(const nm *m, uint32_t a) { double v; nm_gread(m, &v, a, 8); return v; }
/* X_IMG8/16/32 of constant image addresses (never the stack: the window is checked against them at entry). */
static inline uint32_t nm_i32(const nm *m, uint32_t a) { uint32_t v; memcpy(&v, NM_IP(m, a), 4); return v; }
static inline uint16_t nm_i16(const nm *m, uint32_t a) { uint16_t v; memcpy(&v, NM_IP(m, a), 2); return v; }
static inline uint8_t nm_i8(const nm *m, uint32_t a) { return *NM_IP(m, a); }
static inline void nm_wi32(const nm *m, uint32_t a, uint32_t v) { memcpy(NM_IP(m, a), &v, 4); }
static inline void nm_wi8(const nm *m, uint32_t a, uint8_t v) { *NM_IP(m, a) = v; }
static inline int nm_overlap(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen)
{
    return alen && blen && ((uint32_t)(a - b) < blen || (uint32_t)(b - a) < alen);
}

/* ---------------------------------------------------------------------------------------------------------------
 * Lazy flags (the xctx fields), evaluated like xv_x86rt.h.
 * --------------------------------------------------------------------------------------------------------------- */
typedef struct { uint32_t kind, op1, op2, res, bits, cfo, cf, ofo, of; } nfl;
#define FLG(K, A, B, R, N) do { fl->kind = (K); fl->op1 = (uint32_t)(A); fl->op2 = (uint32_t)(B); fl->res = (uint32_t)(R); \
                                fl->bits = (N); fl->cfo = 0; fl->ofo = 0; } while (0)
static inline uint32_t nf_mask(const nfl *f) { return f->bits == 32 ? 0xFFFFFFFFu : ((1u << f->bits) - 1u); }
static inline uint32_t nf_msb(const nfl *f, uint32_t v) { return (v >> (f->bits - 1)) & 1u; }
static inline uint32_t nf_parity_even(uint32_t v) { v &= 0xFFu; v ^= v >> 4; return ((0x6996u >> (v & 0xFu)) & 1u) ^ 1u; }
static inline uint32_t nf_z(const nfl *f) { return f->kind == XK_EXPLICIT ? (f->res >> 6) & 1u : (f->res & nf_mask(f)) == 0; }
static inline uint32_t nf_s(const nfl *f) { return f->kind == XK_EXPLICIT ? (f->res >> 7) & 1u : nf_msb(f, f->res); }
static inline uint32_t nf_p(const nfl *f) { return f->kind == XK_EXPLICIT ? (f->res >> 2) & 1u : nf_parity_even(f->res); }
static inline uint32_t nf_c(const nfl *f)
{
    if (f->kind == XK_EXPLICIT) return f->res & 1u;
    if (f->cfo) return f->cf;
    uint32_t m = nf_mask(f), a = f->op1 & m, b = f->op2 & m, r = f->res & m;
    switch (f->kind) {
    case XK_ADD: return r < a;
    case XK_ADC: return f->cf ? (r <= a) : (r < a);
    case XK_SUB: return a < b;
    case XK_SBB: return f->cf ? (a <= b) : (a < b);
    default: return 0;
    }
}
static inline uint32_t nf_o(const nfl *f)
{
    if (f->kind == XK_EXPLICIT) return (f->res >> 11) & 1u;
    if (f->ofo) return f->of;
    uint32_t a = f->op1, b = f->op2, r = f->res;
    switch (f->kind) {
    case XK_ADD: case XK_ADC: return nf_msb(f, (a ^ r) & (b ^ r));
    case XK_SUB: case XK_SBB: return nf_msb(f, (a ^ b) & (a ^ r));
    default: return 0;
    }
}
/* inc/dec as emitted: cf_ = XF_C; f_cf_override = 1; f_cf = cf_ (the rest of the record untouched) */
#define FL_INCDEC() do { uint32_t cf_ = nf_c(fl); fl->cfo = 1; fl->cf = cf_; } while (0)
static inline uint32_t nf_shl32(nfl *fl, uint32_t v, unsigned n)      /* x_shl32, 1 <= n <= 31 */
{
    uint32_t r = v << n; FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = 1; fl->cf = (v >> (32 - n)) & 1u;
    fl->ofo = 1; fl->of = (r >> 31) ^ fl->cf; return r;
}
static inline uint32_t nf_shr32(nfl *fl, uint32_t v, unsigned n)      /* x_shr32, 1 <= n <= 31 */
{
    uint32_t r = v >> n; FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = 1; fl->cf = (v >> (n - 1)) & 1u;
    fl->ofo = 1; fl->of = v >> 31; return r;
}
static inline uint32_t nf_sar32(nfl *fl, uint32_t v, unsigned n)      /* x_sar32, 1 <= n <= 31 */
{
    int32_t sv = (int32_t)v; uint32_t r = (uint32_t)(sv >> n); FLG(XK_LOGIC, 0, 0, r, 32);
    fl->cfo = 1; fl->cf = (uint32_t)(sv >> (n - 1)) & 1u; fl->ofo = 1; fl->of = 0; return r;
}
static inline uint32_t nf_imul32(nfl *fl, uint32_t a, uint32_t b)     /* x_imul32 */
{
    int64_t p = (int64_t)(int32_t)a * (int32_t)b; uint32_t r = (uint32_t)p;
    FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = fl->ofo = 1; fl->cf = fl->of = (p != (int64_t)(int32_t)r); return r;
}
/* jl / jle / jg ... on the record */
#define F_JL()  (nf_s(fl) != nf_o(fl))
#define F_JLE() (nf_z(fl) || nf_s(fl) != nf_o(fl))

/* x87 condition codes (x87_compare): unordered C3|C2|C0, less C0, equal C3. One quiet VFP compare, as xv_x86rt.h's
 * Thumb-2 path does (the portable expression emits up to three compare/transfer pairs). */
static inline uint16_t nx_cc(double a, double b)
{
#if defined(__arm__) && defined(__ARM_FP) && (__ARM_FP & 8)
    uint32_t r;
#if defined(__thumb2__)
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\tmov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\tit vs\n\tmovvs %0, #17664" : "=r"(r) : "w"(a), "w"(b) : "cc");
#else
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\tmov %0, #0\n\tmovmi %0, #256\n\t"
                     "moveq %0, #16384\n\tmovvs %0, #17664" : "=r"(r) : "w"(a), "w"(b) : "cc");
#endif
    return (uint16_t)r;
#else
    return (isnan(a) || isnan(b)) ? 0x4500 : (a < b) ? 0x0100 : (a == b) ? 0x4000 : 0;
#endif
}
/* x87_round (nearbyint / floor / ceil / trunc by the control word's RC field), without the libm calls for |v| < 2^31
 * (ARMv7 has no rounding instruction; nearbyint saves and restores the FP environment): truncate through int32 (exact
 * there), adjust per mode, give a zero result the sign of v as the libm functions do. Same result for every input
 * (checked against libm by the 602F0 test's self-test); NaN, infinities and |v| >= 2^31 take libm. */
static inline double nx_round(uint16_t fcw, double v)
{
    const unsigned rc = (fcw >> 10) & 3u;
    if (__builtin_expect(fabs(v) < 2147483648.0, 1)) {
        const int32_t i = (int32_t)v;
        double t = (double)i;
        switch (rc) {
        case 0: { const double d = v - t;                      /* exact: t is v with its fraction cleared */
                  if (d > 0.5 || (d == 0.5 && (i & 1))) t += 1.0;
                  else if (d < -0.5 || (d == -0.5 && (i & 1))) t -= 1.0;
                  break; }
        case 1: if (t > v) t -= 1.0; break;
        case 2: if (t < v) t += 1.0; break;
        default: break;
        }
        return t == 0.0 ? copysign(0.0, v) : t;
    }
    switch (rc) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); }
}
/* Tests: the rounding above against libm. */
double xv_native_606b0_round(uint16_t fcw, double v) { return nx_round(fcw, v); }

/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go. */
static void n_budget(xctx *c, uint32_t backedges)
{
    while (backedges) {
        int32_t n = c->preempt >= 1 ? c->preempt : 1;
        if ((uint32_t)n > backedges) { c->preempt -= (int32_t)backedges; return; }
        backedges -= (uint32_t)n; c->preempt -= n;
        xv_preempt(c);
    }
}
static int n_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
static int n_nan32(uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu); }

extern void xv_flare_barrier(unsigned);

/* ===============================================================================================================
 * 1. f_000606B0: the per-flare region.
 * =============================================================================================================== */
enum {
    R_BELOW = 0x40,                 /* window [E - 0x40, E + 0xC8): 606B0's frame (0xB8 of locals + 4 pushes) and the */
    R_WIN = R_BELOW + 0xC8,         /* callee frames below it (60000 -> 60E90 reaches E - 0x3C) */
    R_EXIT_DRAW = 0x60B13u, R_EXIT_DONE = 0x60DC0u,
};
typedef struct {
    double X[8];                    /* X[d] = the x87 slot d below the entry top: st[(F - d) & 7] */
    uint32_t eax, ecx, edx, ebx, ebp, esi, edi, E;
    uint16_t fsw;
    unsigned F, touched;            /* touched: the lifted code has masked fsp (a push/pop, or 60000's exit) */
    uint32_t be;
    uint8_t *S;
    /* image constants and render globals, read once per region (the window never overlaps them) */
    double c_a68, c_a78, c_a80, c_a98, c_acc, c_ad0, c_ad4, c_ad8, c_c2c, c_af8;
    double g_6c8, g_6cc, g_6d0, g_6d4, g_6d8, g_6dc, g_764, g_768, g_76c, g_770, g_774, g_778, g_77c, g_780, g_784;
    uint32_t i_6d4, i_6d8, i_6dc;
    /* f_000602F0 only */
    double c_abc, c_b14, c_c30, c_c34;
    uint32_t K;                     /* [1F2840]: the CRT floor's control word */
    uint16_t fcw, g_6c0;
    int barriers;                   /* call the flare barriers (not in verify mode's native pass) */
} rs;
typedef struct { unsigned flares, reflections, draws, be; } r_info;
/* frame offsets relative to E (the body's esp): s->S points at offset 0 inside the shadow */
/* The frame base inside the shadow is 4-aligned (the shadow is 8-aligned, R_BELOW / P_BELOW are multiples of 4), so
 * word-aligned offsets compile to direct VFP loads/stores (no core <-> VFP register transfers on the A9). */
#define SB(s) ((uint8_t *)__builtin_assume_aligned((s)->S, 4))
static inline uint32_t sr32(const rs *s, int k) { uint32_t v; memcpy(&v, SB(s) + k, 4); return v; }
static inline uint16_t sr16(const rs *s, int k) { uint16_t v; memcpy(&v, SB(s) + k, 2); return v; }
static inline void sw32(rs *s, int k, uint32_t v) { memcpy(SB(s) + k, &v, 4); }
static inline void sw16(rs *s, int k, uint16_t v) { memcpy(SB(s) + k, &v, 2); }
static inline void sw8(rs *s, int k, uint8_t v) { SB(s)[k] = v; }
static inline double srf(const rs *s, int k) { float v; memcpy(&v, SB(s) + k, 4); return (double)v; }
static inline void swf(rs *s, int k, double d) { float v = (float)d; memcpy(SB(s) + k, &v, 4); }
static inline double sr64(const rs *s, int k) { double v; memcpy(&v, SB(s) + k, 8); return v; }
static inline void sw64(rs *s, int k, double d) { memcpy(SB(s) + k, &d, 8); }
#define CMP(A, B, depth) (s->fsw = (uint16_t)((s->fsw & ~0x4700u) | nx_cc((A), (B)) | (((s->F - (depth)) & 7u) << 11)))
#define FNSTSW_AX() (s->eax = (s->eax & 0xFFFF0000u) | s->fsw)
#define AH() ((s->eax >> 8) & 0xFFu)
#define SWAP(a, b) do { double t_ = X[a]; X[a] = X[b]; X[b] = t_; } while (0)

/* f_00060E90 (unpack the 11/11/10 normal in ecx into three floats at [eax]), called at x87 depth 0: the return
 * address at frame offset kr, its 0x10-byte frame below, the output at frame offset ko (eax = E + ko). The spliced
 * body (build-x87) writes its one slot back and leaves fsp alone. */
static inline __attribute__((always_inline)) void r_60E90(rs *s, nfl *fl, int kr, uint32_t ret, int ko)
{
    double *X = s->X;
    const int L = kr - 0x10;
    sw32(s, kr, ret);
    uint32_t ecx = s->ecx, edx = ecx;
    edx = nf_shl32(fl, edx, 21); sw32(s, L, edx);
    X[1] = (double)(int32_t)sr32(s, L);
    ecx = nf_shr32(fl, ecx, 11); edx = ecx; edx = nf_shl32(fl, edx, 21);
    X[1] = X[1] * s->c_ad8; sw32(s, L, edx); ecx = nf_shr32(fl, ecx, 11); X[1] = X[1] + s->c_a78; ecx = nf_shl32(fl, ecx, 22);
    X[1] = X[1] * s->c_ad4; swf(s, L + 4, X[1]);
    edx = sr32(s, L + 4);
    X[1] = (double)(int32_t)sr32(s, L); sw32(s, L, ecx); sw32(s, ko, edx);
    X[1] = X[1] * s->c_ad8; X[1] = X[1] + s->c_a78; X[1] = X[1] * s->c_ad4; swf(s, L + 8, X[1]);
    edx = sr32(s, L + 8);
    X[1] = (double)(int32_t)sr32(s, L); sw32(s, ko + 4, edx);
    X[1] = X[1] * s->c_ad0; X[1] = X[1] + s->c_a78; X[1] = X[1] * s->c_acc; swf(s, L + 0xC, X[1]);
    edx = sr32(s, L + 0xC); sw32(s, ko + 8, edx);
    s->ecx = s->eax; s->edx = edx;               /* mov ecx,eax; eax stays the output pointer */
}

/* f_0005FE30: the address of the flare row's brightness byte (ecx = row) -> eax. */
static inline __attribute__((always_inline)) void r_5FE30(rs *s, nfl *fl, const nm *m)
{
    xv_flare_barrier(2u);
    const uint32_t row = s->ecx;
    s->eax = nm_rd(m, row + 0x1Eu, 2);           /* xor eax,eax; mov ax,[ecx+1Eh] */
    const uint32_t ah = (s->eax >> 8) & 0xFFu;
    FLG(XK_LOGIC, 0, 0, ah, 8);
    if (ah & 0x80u) {
        s->edx = (uint32_t)(int32_t)(int16_t)nm_rd(m, row + 0x20u, 2);
        s->ecx = nm_rd(m, row + 0x22u, 1);
        s->eax &= 0x7FFFu; s->eax = nf_shl32(fl, s->eax, 16); s->eax |= s->edx; s->ecx &= 0xFFFFFF7Fu;
        s->eax = s->ecx + s->eax * 4u + 0x27FFB0u;
    } else {
        s->edx = nm_rd(m, row + 0x22u, 1);
        s->ecx = (uint32_t)(int32_t)(int16_t)nm_rd(m, row + 0x20u, 2);
        s->eax = (uint32_t)(int32_t)(int16_t)s->eax;
        s->eax = nf_imul32(fl, s->eax, 0x22u);
        s->edx &= 0xFFFFFF7Fu; s->edx = s->edx + s->ecx * 4u;
        s->eax = s->edx + s->eax + 0x2BFFD2u;
    }
}

/* f_00060000 (the flare's rotation by mode di = [def+80h]; esi = the flare row), called at depth 0 with the return
 * address at E - 4; leaves the angle at depth 1. Spliced body: the exit writes its five slots, fsp and fsw back. */
static inline __attribute__((always_inline)) void r_60000(rs *s, nfl *fl, const nm *m)
{
    double *X = s->X;
    enum { L = -0x28 };                          /* esp inside 60000 */
    sw32(s, -4, 0x608B8u);
    s->ecx = nm_rd(m, s->esi + 0x10u, 4);
    s->eax = s->E + (uint32_t)(L + 0x18);
    sw32(s, L + 4, 0x3F800000u); sw32(s, L + 8, 0); sw32(s, L, 0);
    r_60E90(s, fl, L - 4, 0x60027u, L + 0x18);
    s->ecx = sr32(s, L + 0x18); s->edx = sr32(s, L + 0x1C); s->eax = sr32(s, L + 0x20);
    sw32(s, L + 0x14, s->eax);
    s->eax = (uint32_t)(int32_t)(int16_t)s->edi;
    FL_INCDEC(); s->eax -= 1u;                   /* dec eax */
    FLG(XK_SUB, s->eax, 3u, s->eax - 3u, 32);    /* cmp eax,3 */
    sw32(s, L + 0xC, s->ecx); sw32(s, L + 0x10, s->edx);
    if (!nf_c(fl) && !nf_z(fl)) goto p602A1;     /* ja: mode not 1..4 */
    switch (s->eax) { case 0: goto p6004F; case 1: goto p6011F; case 2: goto p60184; default: goto p60236; }
p6004F:
    X[1] = s->g_76c; s->edx = s->i_6d8; X[1] = X[1] * srf(s, L + 0x10); s->eax = s->i_6dc;
    X[2] = s->g_768; s->ecx = s->i_6d4; X[2] = X[2] * srf(s, L + 0x14);
    sw32(s, L + 0x1C, s->edx); sw32(s, L + 0x20, s->eax); sw32(s, L + 0x18, s->ecx);
    X[1] = X[1] - X[2];
    X[2] = s->g_764; X[2] = X[2] * srf(s, L + 0x14); X[3] = s->g_76c; X[3] = X[3] * srf(s, L + 0xC); X[2] = X[2] - X[3];
    X[3] = s->g_768; X[3] = X[3] * srf(s, L + 0xC); X[4] = s->g_764; X[4] = X[4] * srf(s, L + 0x10); X[3] = X[3] - X[4];
    X[4] = srf(s, L + 0x10); X[4] = X[4] * X[1]; X[5] = X[2]; X[5] = X[5] * srf(s, L + 0xC); X[4] = X[4] - X[5]; swf(s, L + 8, X[4]);
    X[4] = srf(s, L + 0xC); X[4] = X[4] * X[3]; X[5] = srf(s, L + 0x14); X[5] = X[5] * X[1]; X[4] = X[4] - X[5]; swf(s, L + 4, X[4]);
    SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x14); SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x10); X[2] = X[2] - X[3];
    X[1] = X[2];                                 /* fstp st(1) */
    X[2] = srf(s, L + 0x1C); X[2] = X[2] * srf(s, L + 4); X[3] = srf(s, L + 0x20); X[3] = X[3] * srf(s, L + 8);
p600F2:
    X[2] = X[2] + X[3]; SWAP(2, 1); X[2] = X[2] * srf(s, L + 0x18); X[1] = X[1] + X[2];
    X[2] = srf(s, L + 0x20); X[2] = X[2] * srf(s, L + 0x14); X[3] = srf(s, L + 0x1C); X[3] = X[3] * srf(s, L + 0x10); X[2] = X[2] + X[3];
    X[3] = srf(s, L + 0xC); X[3] = X[3] * srf(s, L + 0x18); X[2] = X[2] + X[3]; X[2] = -X[2];
    goto p602A9;
p6011F:
    X[1] = srf(s, L + 0xC); X[1] = -X[1]; swf(s, L + 0x18, X[1]);
    X[1] = srf(s, L + 0x10); X[1] = -X[1]; swf(s, L + 0x1C, X[1]);
    X[1] = srf(s, L + 0x14); X[1] = -X[1]; swf(s, L + 0x20, X[1]);
    X[1] = X[1] * s->g_76c; X[2] = srf(s, L + 0x1C); X[2] = X[2] * s->g_768; X[1] = X[1] + X[2];
    X[2] = s->g_764; X[2] = X[2] * srf(s, L + 0x18); X[1] = X[1] + X[2];
    X[2] = srf(s, L + 0x20); X[2] = X[2] * s->g_784; X[3] = srf(s, L + 0x1C); X[3] = X[3] * s->g_780; X[2] = X[2] + X[3];
    X[3] = s->g_77c; X[3] = X[3] * srf(s, L + 0x18); X[2] = X[2] + X[3]; X[2] = -X[2];
    goto p602A9;
p60184:
    X[1] = s->g_76c; X[1] = X[1] * srf(s, L + 0x10); X[2] = s->g_768; X[2] = X[2] * srf(s, L + 0x14); X[1] = X[1] - X[2];
    X[2] = s->g_764; X[2] = X[2] * srf(s, L + 0x14); X[3] = s->g_76c; X[3] = X[3] * srf(s, L + 0xC); X[2] = X[2] - X[3];
    X[3] = s->g_768; X[3] = X[3] * srf(s, L + 0xC); X[4] = s->g_764; X[4] = X[4] * srf(s, L + 0x10); X[3] = X[3] - X[4];
    X[4] = srf(s, L + 0x10); X[4] = X[4] * X[1]; X[5] = X[2]; X[5] = X[5] * srf(s, L + 0xC); X[4] = X[4] - X[5]; swf(s, L + 4, X[4]);
    X[4] = srf(s, L + 0xC); X[4] = X[4] * X[3]; X[5] = srf(s, L + 0x14); X[5] = X[5] * X[1]; X[4] = X[4] - X[5]; swf(s, L + 8, X[4]);
    SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x14); SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x10); X[2] = X[2] - X[3];
    X[1] = X[2];
    X[2] = nm_f32(m, s->esi + 4u); X[2] = X[2] - s->g_6c8; swf(s, L + 0x18, X[2]);
    X[2] = nm_f32(m, s->esi + 8u); X[2] = X[2] - s->g_6cc; swf(s, L + 0x1C, X[2]);
    X[2] = nm_f32(m, s->esi + 0xCu); X[2] = X[2] - s->g_6d0; swf(s, L + 0x20, X[2]);
    X[2] = srf(s, L + 0x1C); X[2] = X[2] * srf(s, L + 8); X[3] = srf(s, L + 0x20); X[3] = X[3] * srf(s, L + 4);
    s->be++;                                     /* jmp 600F2: a back-edge (X_PREEMPT) */
    goto p600F2;
p60236:
    X[1] = nm_f32(m, s->esi + 4u); X[1] = X[1] - s->g_6c8; swf(s, L + 0x18, X[1]);
    X[1] = nm_f32(m, s->esi + 8u); X[1] = X[1] - s->g_6cc; swf(s, L + 0x1C, X[1]);
    X[1] = nm_f32(m, s->esi + 0xCu); X[1] = X[1] - s->g_6d0; swf(s, L + 0x20, X[1]);
    X[1] = X[1] * s->g_76c; X[2] = srf(s, L + 0x1C); X[2] = X[2] * s->g_768; X[1] = X[1] + X[2];
    X[2] = s->g_764; X[2] = X[2] * srf(s, L + 0x18); X[1] = X[1] + X[2];
    X[2] = srf(s, L + 0x20); X[2] = X[2] * s->g_784; X[3] = srf(s, L + 0x1C); X[3] = X[3] * s->g_780; X[2] = X[2] + X[3];
    X[3] = s->g_77c; X[3] = X[3] * srf(s, L + 0x18); X[2] = X[2] + X[3]; X[2] = -X[2];
    goto p602A9;
p602A1:
    X[1] = srf(s, L + 8); X[2] = srf(s, L + 4);
p602A9:
    FLG(XK_LOGIC, 0, 0, s->edi & 0xFFFFu, 16);  /* test di,di */
    if (!(s->edi & 0xFFFFu)) goto p602C9;
    X[3] = X[1]; CMP(X[3], s->c_a68, 3); FNSTSW_AX();
    { const uint32_t r = AH() & 0x44u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) goto p602C9; }
    X[1] = atan2(X[1], X[2]);                    /* fpatan */
    X[1] = X[1] * s->c_c2c;
    s->touched = 1;
    return;
p602C9:
    X[1] = srf(s, L);                            /* fstp st(0) x2; fld dword [esp] */
    s->touched = 1;
}

/* f_00011120 (normalize the vector at frame offset kv, return its length or 0.0), called at depth 0 with the return
 * address at E - 4; leaves st0 at depth 1. */
static inline __attribute__((always_inline)) void r_11120(rs *s, nfl *fl, int kv, uint32_t ret)
{
    double *X = s->X;
    sw32(s, -4, ret);
    X[1] = srf(s, kv + 8); X[2] = srf(s, kv + 4); X[3] = srf(s, kv);
    X[4] = X[3]; X[4] = X[4] * X[3];
    X[5] = X[2]; X[5] = X[5] * X[2];
    X[4] = X[4] + X[5];
    X[5] = X[1]; X[5] = X[5] * X[1];
    X[4] = X[4] + X[5];
    X[4] = sqrt(X[4]);
    X[1] = X[4];                                 /* fstp st(3); fstp st(0) x2 */
    s->touched = 1;
    X[2] = X[1]; X[2] = fabs(X[2]);
    CMP(X[2], s->c_af8, 2); FNSTSW_AX();
    { const uint32_t r = AH() & 5u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) { X[1] = s->c_a68; return; } }
    X[2] = s->c_a78; X[2] = X[2] / X[1];
    X[3] = X[2]; X[3] = X[3] * srf(s, kv); swf(s, kv, X[3]);
    X[3] = X[2]; X[3] = X[3] * srf(s, kv + 4); swf(s, kv + 4, X[3]);
    X[2] = X[2] * srf(s, kv + 8); swf(s, kv + 8, X[2]);
}

/* 6096D..609B0 (and the two copies): clamp st0 (depth 1) to [0, 1] into the frame dword k; pops. */
static inline __attribute__((always_inline)) void r_clamp(rs *s, nfl *fl, int k)
{
    double *X = s->X;
    CMP(X[1], s->c_a68, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 5u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) { sw32(s, k, 0); return; } }
    CMP(X[1], s->c_a78, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) { sw32(s, k, 0x3F800000u); return; } }
    swf(s, k, X[1]);
}

/* The region from an entry label to L_00060B13 (draw this reflection) or L_00060DC0 (all flares done). Returns the
 * exit label, or 0 when declined (nothing written). */
static int r_run(xctx *c, unsigned entry, r_info *info)
{
    const uint32_t E = c->r[4], wlo = E - R_BELOW;
    memset(info, 0, sizeof *info);
    if (E & 7u) return 0;                        /* the frame is 8-aligned (and esp,-8): never in practice */
    /* the image constants / render globals / flare count are read with fixed-address semantics */
    if (nm_overlap(wlo, R_WIN, 0x1F0000u, 0x1000u) || nm_overlap(wlo, R_WIN, 0x2E34E0u, 4u) || nm_overlap(wlo, R_WIN, 0x2FC000u, 0x1000u))
        return 0;
    uint8_t S[R_WIN] __attribute__((aligned(8)));
    nm mm; nm_init(&mm, S, wlo, R_WIN); const nm *const m = &mm;
    rs state; rs *const s = &state; double *const X = s->X;
    nfl flags = { c->f_kind, c->f_op1, c->f_op2, c->f_res, c->f_bits, c->f_cf_override, c->f_cf, c->f_of_override, c->f_of };
    nfl *const fl = &flags;
    s->S = S + R_BELOW; s->E = E;
    s->F = c->fsp & 7u;
    for (unsigned d = 0; d < 8; ++d) X[d] = c->st[(s->F - d) & 7u];
    s->fsw = c->fsw; s->touched = 0; s->be = 0;
    s->eax = c->r[0]; s->ecx = c->r[1]; s->edx = c->r[2]; s->ebx = c->r[3]; s->ebp = c->r[5]; s->esi = c->r[6]; s->edi = c->r[7];
    /* The constants (page 1F0000) and the render camera (page 2FC000): outside the window (checked above) and never
     * straddling, so each x87_load_f32 of them is one translation of its page: translate each page once. */
    {
        const uint8_t *k = NM_HP(m, 0x1F0000u), *g = NM_HP(m, 0x2FC000u);
#define KF(p, a) ({ float v_; memcpy(&v_, (p) + ((a) & 0xFFFu), 4); (double)v_; })
        s->c_a68 = KF(k, 0x1F0A68u); s->c_a78 = KF(k, 0x1F0A78u); s->c_a80 = KF(k, 0x1F0A80u); s->c_a98 = KF(k, 0x1F0A98u);
        s->c_acc = KF(k, 0x1F0ACCu); s->c_ad0 = KF(k, 0x1F0AD0u); s->c_ad4 = KF(k, 0x1F0AD4u); s->c_ad8 = KF(k, 0x1F0AD8u);
        s->c_c2c = KF(k, 0x1F0C2Cu); memcpy(&s->c_af8, k + 0xAF8u, 8);
        s->g_6c8 = KF(g, 0x2FC6C8u); s->g_6cc = KF(g, 0x2FC6CCu); s->g_6d0 = KF(g, 0x2FC6D0u);
        s->g_6d4 = KF(g, 0x2FC6D4u); s->g_6d8 = KF(g, 0x2FC6D8u); s->g_6dc = KF(g, 0x2FC6DCu);
        s->g_764 = KF(g, 0x2FC764u); s->g_768 = KF(g, 0x2FC768u); s->g_76c = KF(g, 0x2FC76Cu);
        s->g_770 = KF(g, 0x2FC770u); s->g_774 = KF(g, 0x2FC774u); s->g_778 = KF(g, 0x2FC778u);
        s->g_77c = KF(g, 0x2FC77Cu); s->g_780 = KF(g, 0x2FC780u); s->g_784 = KF(g, 0x2FC784u);
#undef KF
    }
    s->i_6d4 = nm_i32(m, 0x2FC6D4u); s->i_6d8 = nm_i32(m, 0x2FC6D8u); s->i_6dc = nm_i32(m, 0x2FC6DCu);
    unsigned exit;
    switch (entry) {
    case 0x606FCu: s->eax = 0; goto l60700;      /* xor eax,eax (flags dead); mov edi,edi */
    case 0x60700u: goto l60700;
    case 0x60AC0u: goto l60AC0;
    default: goto l60DA6;
    }

l60700:                                          /* one flare row: eax = its index (sign-extended) */
    info->flares++;
    s->esi = s->eax * 5u;
    s->ecx = nm_rd(m, s->esi * 8u + 0x2C76E0u, 4);
    s->esi = s->esi * 8u + 0x2C76D0u;
    s->eax = E + 0xBCu;
    sw32(s, 0x54, s->esi);
    r_60E90(s, fl, -4, 0x60721u, 0xBC);
    s->ecx = sr32(s, 0xBC); s->edx = sr32(s, 0xC0); s->eax = sr32(s, 0xC4);
    sw32(s, 0x64, s->ecx);
    s->ecx = nm_rd(m, s->esi + 0x22u, 1) & 0xFFFFFF7Fu;
    { const uint16_t a = (uint16_t)s->ecx, b = nm_i16(m, 0x2FC6C2u); FLG(XK_SUB, a, b, (uint16_t)(a - b), 16); }
    sw32(s, 0x68, s->edx); sw32(s, 0x6C, s->eax);
    if (!nf_z(fl)) goto l60DA6;                  /* not this view's stage */
    s->eax = nm_rd(m, s->esi + 0x24u, 4); FLG(XK_LOGIC, 0, 0, s->eax, 32);
    s->ebp = nm_rd(m, s->esi, 4);
    if (F_JLE()) goto l60DA6;                    /* area <= 0 */
    s->ebx = nm_rd(m, s->esi + 0x1Bu, 1); FLG(XK_LOGIC, 0, 0, s->ebx, 32);
    if (nf_c(fl) || nf_z(fl)) goto l60DA6;       /* color alpha 0 */
    s->eax = nm_rd(m, s->ebp + 0xC4u, 4); FLG(XK_LOGIC, 0, 0, s->eax, 32);
    if (F_JLE()) goto l60DA6;                    /* no reflections */
    s->edx = s->esi + 4u;
    s->eax = nm_rd(m, s->edx, 4); s->ecx = nm_rd(m, s->edx + 4u, 4); s->edx = nm_rd(m, s->edx + 8u, 4);
    sw32(s, 0x74, s->eax);
    s->touched = 1;
    X[1] = srf(s, 0x74); X[1] = X[1] - s->g_6c8;
    sw32(s, 0x78, s->ecx); sw32(s, 0x7C, s->edx);
    s->ecx = s->esi;
    swf(s, 0x20, X[1]);
    X[1] = srf(s, 0x78); X[1] = X[1] - s->g_6cc; swf(s, 0x24, X[1]);
    X[1] = srf(s, 0x7C); X[1] = X[1] - s->g_6d0; swf(s, 0x28, X[1]);
    X[1] = X[1] * s->g_6dc;
    X[2] = srf(s, 0x24); X[2] = X[2] * s->g_6d8; X[1] = X[1] + X[2];
    X[2] = srf(s, 0x20); X[2] = X[2] * s->g_6d4; X[1] = X[1] + X[2];
    swf(s, 0x2C, X[1]);                          /* distance along the view axis */
    X[1] = s->g_6d4; X[1] = X[1] * srf(s, 0x2C);
    X[2] = s->g_6d8; X[2] = X[2] * srf(s, 0x2C);
    X[3] = s->g_6dc; X[3] = X[3] * srf(s, 0x2C);
    swf(s, 0x40, X[3]);
    SWAP(2, 1); X[2] = X[2] - srf(s, 0x20); swf(s, 0x38, X[2]);
    X[1] = X[1] - srf(s, 0x24);
    X[2] = srf(s, 0x40); X[2] = X[2] - srf(s, 0x28);
    X[3] = srf(s, 0x38); X[3] = X[3] + X[3]; swf(s, 0x38, X[3]);
    SWAP(2, 1); X[2] = X[2] + X[2]; swf(s, 0x3C, X[2]);
    X[1] = X[1] + X[1]; swf(s, 0x40, X[1]);
    sw32(s, -4, 0x6082Du);
    r_5FE30(s, fl, m);
    s->eax = nm_rd(m, s->eax, 1);
    sw32(s, 0x1C, s->eax);
    X[1] = (double)(int32_t)sr32(s, 0x1C); X[1] = X[1] * s->c_a98; swf(s, 0x5C, X[1]);
    X[1] = nm_f32(m, s->ebp + 0x1Cu); CMP(X[1], s->c_a68, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (r) { X[1] = s->c_a78; goto l6088D; } }
    X[1] = srf(s, 0x2C); X[1] = X[1] - nm_f32(m, s->ebp + 0x1Cu);
    X[2] = nm_f32(m, s->ebp + 0x18u); X[2] = X[2] - nm_f32(m, s->ebp + 0x1Cu);
    X[1] = X[1] / X[2];
    CMP(X[1], s->c_a68, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 5u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) { X[1] = s->c_a68; goto l6088D; } }
    CMP(X[1], s->c_a78, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) X[1] = s->c_a78; }
l6088D:                                          /* distance fade in st0 (depth 1) */
    X[2] = srf(s, 0x5C);
    s->edi = (s->edi & 0xFFFF0000u) | nm_rd(m, s->ebp + 0x80u, 2);
    X[2] = X[2] * X[1];
    s->ecx = s->ebx & 0xFFu;
    sw32(s, 0x1C, s->ecx);
    X[3] = (double)(int32_t)sr32(s, 0x1C);
    X[2] = X[2] * X[3];
    X[2] = X[2] * s->c_a98;
    swf(s, 0x34, X[2]);                          /* brightness */
    r_60000(s, fl, m);                           /* the flare's rotation (depth 1) */
    X[1] = X[1] * nm_f32(m, s->ebp + 0x84u);
    s->ecx = E + 0x20u;
    swf(s, 0x98, X[1]);
    X[1] = s->g_76c; X[1] = X[1] * srf(s, 0x28);
    X[2] = s->g_768; X[2] = X[2] * srf(s, 0x24); X[1] = X[1] + X[2];
    X[2] = srf(s, 0x20); X[2] = X[2] * s->g_764; X[1] = X[1] + X[2];
    X[2] = s->g_778; X[2] = X[2] * srf(s, 0x28);
    X[3] = s->g_774; X[3] = X[3] * srf(s, 0x24); X[2] = X[2] + X[3];
    X[3] = srf(s, 0x20); X[3] = X[3] * s->g_770; X[2] = X[2] + X[3];
    X[1] = atan2(X[1], X[2]);                    /* fpatan */
    X[1] = X[1] * s->c_a80;
    swf(s, 0x9C, X[1]);
    X[1] = nm_f32(m, s->ebp + 8u); X[1] = X[1] - nm_f32(m, s->ebp + 0xCu);
    X[1] = s->c_a78 / X[1];                      /* fdivr */
    swf(s, 0x18, X[1]);
    X[1] = X[1] * nm_f32(m, s->ebp + 0xCu);
    X[1] = -X[1];
    swf(s, 0x14, X[1]);
    r_11120(s, fl, 0x20, 0x6093Au);              /* normalize the offset (ecx = E + 0x20); fstp st(0) */
    sw32(s, 0x80, 0x3F800000u);
    X[1] = srf(s, 0x6C); X[1] = X[1] * s->g_6dc;
    X[2] = srf(s, 0x68); X[2] = X[2] * s->g_6d8; X[1] = X[1] + X[2];
    X[2] = srf(s, 0x64); X[2] = X[2] * s->g_6d4; X[1] = X[1] + X[2];
    X[1] = X[1] * srf(s, 0x18);
    X[1] = srf(s, 0x14) - X[1];                  /* fsubr */
    r_clamp(s, fl, 0x84);
    X[1] = srf(s, 0x6C); X[1] = X[1] * srf(s, 0x28);
    X[2] = srf(s, 0x68); X[2] = X[2] * srf(s, 0x24); X[1] = X[1] + X[2];
    X[2] = srf(s, 0x64); X[2] = X[2] * srf(s, 0x20); X[1] = X[1] + X[2];
    X[1] = X[1] * srf(s, 0x18);
    X[1] = srf(s, 0x14) - X[1];
    r_clamp(s, fl, 0x88);
    X[1] = srf(s, 0x28); X[1] = X[1] * s->g_6dc;
    X[2] = srf(s, 0x24); X[2] = X[2] * s->g_6d8; X[1] = X[1] + X[2];
    X[2] = srf(s, 0x20); X[2] = X[2] * s->g_6d4; X[1] = X[1] + X[2];
    X[1] = X[1] * srf(s, 0x18);
    X[1] = X[1] + srf(s, 0x14);
    r_clamp(s, fl, 0x8C);
    X[1] = srf(s, 0x34); CMP(X[1], s->c_a68, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (r) goto l60DA6; }   /* brightness <= 0 */
    s->edx = nm_rd(m, s->esi + 0x23u, 1);
    s->eax = nm_rd(m, s->ebp + 0xC4u, 4);
    sw32(s, 0x1C, s->edx);
    s->ebx = 0;                                  /* xor ebx,ebx (flags dead) */
    FLG(XK_LOGIC, 0, 0, s->eax, 32);
    X[1] = (double)(int32_t)sr32(s, 0x1C);
    sw32(s, 0x1C, s->ebx);
    X[1] = X[1] * s->c_a98;
    swf(s, 0x60, X[1]);
    if (F_JLE()) goto l60DA6;
    s->edi = 0;                                  /* xor edi,edi (flags dead); lea ecx,[ecx] */
l60AC0:                                          /* one reflection: ebx = its index, edi = (int16) index */
    info->reflections++;
    s->ecx = nm_rd(m, s->ebp + 0xC8u, 4);
    s->edi = nf_shl32(fl, s->edi, 7);
    s->touched = 1;
    X[1] = nm_f32(m, s->edi + s->ecx + 0x34u);
    s->eax = (uint32_t)(int32_t)(int16_t)nm_rd(m, s->edi + s->ecx + 0x3Cu, 2);
    X[2] = nm_f32(m, s->edi + s->ecx + 0x38u);
    s->edi += s->ecx;
    FLG(XK_LOGIC, 0, 0, s->ebx & 0xFFFFu, 16);   /* test bx,bx */
    X[2] = X[2] - X[1];
    X[2] = X[2] * srf(s, 0x60);
    X[2] = X[2] + X[1];
    X[2] = X[2] * nm_f32(m, E + s->eax * 4u + 0x80u);   /* [esp+eax*4+80h]: one of the four edge fades */
    X[2] = X[2] * srf(s, 0x34);
    swf(s, 0x14, X[2]);                          /* fstp; fstp st(0) */
    if (!(s->ebx & 0xFFFFu)) { s->ecx = sr32(s, 0x14); sw32(s, 0x34, s->ecx); }
    X[1] = srf(s, 0x14); CMP(X[1], s->c_a68, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) { info->draws++; exit = R_EXIT_DRAW; goto out; } }
    s->eax = nm_rd(m, s->ebp + 0xC4u, 4);       /* 60D90: next reflection */
    FL_INCDEC(); s->ebx += 1u;
    s->edi = (uint32_t)(int32_t)(int16_t)s->ebx;
    FLG(XK_SUB, s->edi, s->eax, s->edi - s->eax, 32);
    sw32(s, 0x1C, s->ebx);
    if (F_JL()) { s->be++; goto l60AC0; }
l60DA6:                                          /* next flare */
    s->eax = sr32(s, 0x70);
    s->ecx = nm_i32(m, 0x2E34E0u);
    FL_INCDEC(); s->eax += 1u;
    sw32(s, 0x70, s->eax);
    s->eax = (uint32_t)(int32_t)(int16_t)s->eax;
    FLG(XK_SUB, s->eax, s->ecx, s->eax - s->ecx, 32);
    if (F_JL()) { s->be++; goto l60700; }
    exit = R_EXIT_DONE;

out:
    x_guest_write_pages(wlo, S, R_WIN);
    c->r[0] = s->eax; c->r[1] = s->ecx; c->r[2] = s->edx; c->r[3] = s->ebx; c->r[5] = s->ebp; c->r[6] = s->esi; c->r[7] = s->edi;
    for (unsigned d = 1; d < 8; ++d) c->st[(s->F - d) & 7u] = X[d];
    if (s->touched) c->fsp = s->F;               /* the region ends at depth 0 */
    c->fsw = s->fsw;
    c->f_kind = fl->kind; c->f_op1 = fl->op1; c->f_op2 = fl->op2; c->f_res = fl->res; c->f_bits = fl->bits;
    c->f_cf_override = fl->cfo; c->f_cf = fl->cf; c->f_of_override = fl->ofo; c->f_of = fl->of;
    info->be = s->be;
    return (int)exit;
}

/* ---- f_000606B0 hooks ------------------------------------------------------------------------------------------ */
enum { RC_CALLS, RC_REGIONS, RC_NATIVE, RC_VERIFIED, RC_MISMATCHED, RC_DECLINED, RC_STALE, RC_FLARES, RC_REFLECTIONS,
       RC_DRAWS, RC_NANONLY, RC_TIMED_NATIVE, RC_TIMED_GUEST, RC_N };
static unsigned r_counter[RC_N], r_mismatch_total, r_nan_total;
static uint64_t r_native_us, r_guest_us;
#define RC_ADD(i, v) __atomic_fetch_add(&r_counter[i], (unsigned)(v), __ATOMIC_RELAXED)
/* A guest run of the region being verified or timed (the scene runs on one thread at a time). While it is pending
 * the entry hooks stand aside; the exit probes finish it. */
static struct {
    xctx *c;
    int verify;
    unsigned exit;
    uint32_t wlo, be;
    int32_t preempt;
    uint64_t t0;
    r_info info;
    xctx native;
    uint8_t win[R_WIN];
} r_pend;

static int r_mode_value = -1;
static int r_mode(void)
{
    int mode = __atomic_load_n(&r_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_606B0"); mode = e ? atoi(e) : XV_NATIVE_606B0_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        XK_LOG("[native-606b0] f_000606B0 per-flare region (flare loop to the reflection draw test): %s\n",
               mode == 2 ? "native" : mode == 1 ? "verify (native vs guest, guest result kept)" : "off");
        __atomic_store_n(&r_mode_value, mode, __ATOMIC_RELAXED);
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_606b0_force(int mode)
{
    __atomic_store_n(&r_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED);
    r_pend.c = 0; r_mismatch_total = r_nan_total = 0;             /* each test run logs its own first differences */
}
static int r_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_606B0_TIME"); on = e && atoi(e) != 0; }
    return on;
}
static void r_count(const r_info *o) { RC_ADD(RC_FLARES, o->flares); RC_ADD(RC_REFLECTIONS, o->reflections); RC_ADD(RC_DRAWS, o->draws); }
static void r_report_nan(const char *what, uint32_t a, uint32_t b)
{
    if (__atomic_add_fetch(&r_nan_total, 1, __ATOMIC_RELAXED) <= 4)
        XK_LOG("[native-606b0] NAN-ONLY %s native %08X guest %08X (both NaN: which operand's NaN a two-NaN operation keeps is host code generation)\n", what, a, b);
}
static void r_report_mismatch(const char *what, uint32_t a, uint32_t b)
{
    if (__atomic_add_fetch(&r_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-606b0] MISMATCH %s native %08X guest %08X (region: %u flares, %u reflections, %u draws)\n",
               what, a, b, r_pend.info.flares, r_pend.info.reflections, r_pend.info.draws);
}

/* Entry hook at L_000606B0 (0x606B0: bookkeeping only), L_000606FC, L_00060700, L_00060AC0, L_00060DA6.
 * Returns 1: continue at L_00060B13, 2: continue at L_00060DC0, 0: run the guest block. */
int xv_native_606b0_enter(xctx *c, unsigned label)
{
    if (label == 0x606B0u) {                      /* function entry: any pending record of this context is stale */
        if (r_pend.c == c) { r_pend.c = 0; RC_ADD(RC_STALE, 1); }
        RC_ADD(RC_CALLS, 1);
        return 0;
    }
    if (r_pend.c) return 0;                       /* inside a verified / timed guest region (or another thread's) */
    const int mode = r_mode(), timed = r_timing();
    if (!mode) {
        if (!timed) return 0;
        r_pend.c = c; r_pend.verify = 0; r_pend.t0 = xk_os_monotonic_us();
        return 0;
    }
    r_info o;
    RC_ADD(RC_REGIONS, 1);
    if (mode == 2) {
        const uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
        const int x = r_run(c, label, &o);
        if (!x) { RC_ADD(RC_DECLINED, 1); return 0; }
        if (timed) { __atomic_fetch_add(&r_native_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); RC_ADD(RC_TIMED_NATIVE, 1); }
        n_budget(c, o.be); r_count(&o); RC_ADD(RC_NATIVE, 1);
        return x == R_EXIT_DRAW ? 1 : 2;
    }
    /* verify: snapshot, native, keep its result aside, restore; the guest runs the region with an unbounded budget
     * and the probe at its exit label compares. */
    const uint32_t wlo = c->r[4] - R_BELOW;
    uint8_t before_win[R_WIN];
    x_guest_read_pages(before_win, wlo, R_WIN);
    const xctx before = *c;
    const uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
    const int x = r_run(c, label, &o);
    const uint64_t t1 = timed ? xk_os_monotonic_us() : 0;
    if (!x) { RC_ADD(RC_DECLINED, 1); return 0; }
    r_pend.native = *c;
    x_guest_read_pages(r_pend.win, wlo, R_WIN);
    x_guest_write_pages(wlo, before_win, R_WIN);
    *c = before;
    r_pend.verify = 1; r_pend.exit = (unsigned)x; r_pend.wlo = wlo; r_pend.be = o.be; r_pend.info = o;
    r_pend.preempt = before.preempt;
    c->preempt = 1 << 30;
    if (timed) { __atomic_fetch_add(&r_native_us, t1 - t0, __ATOMIC_RELAXED); RC_ADD(RC_TIMED_NATIVE, 1); }
    r_pend.t0 = timed ? xk_os_monotonic_us() : 0;
    r_pend.c = c;
    return 0;
}

/* Exit probes at L_00060B13 and L_00060DC0. */
void xv_native_606b0_probe(xctx *c, unsigned label)
{
    if (r_pend.c != c) return;
    const uint64_t t = r_timing() ? xk_os_monotonic_us() : 0;
    r_pend.c = 0;
    if (t) { __atomic_fetch_add(&r_guest_us, t - r_pend.t0, __ATOMIC_RELAXED); RC_ADD(RC_TIMED_GUEST, 1); }
    if (!r_pend.verify) return;
    const uint32_t guest_be = (uint32_t)((1 << 30) - c->preempt);
    c->preempt = r_pend.preempt;
    uint8_t win[R_WIN];
    x_guest_read_pages(win, r_pend.wlo, R_WIN);
    const xctx *n = &r_pend.native;
    unsigned bad = 0;
#define R_CMP(what, a, b) do { if ((a) != (b)) { bad++; r_report_mismatch(what, (uint32_t)(a), (uint32_t)(b)); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    R_CMP("exit label", r_pend.exit, label);
    for (unsigned i = 0; i < 8; ++i) R_CMP(rn[i], n->r[i], c->r[i]);
    R_CMP("backedges", r_pend.be, guest_be);
    R_CMP("fsp", n->fsp, c->fsp); R_CMP("fcw", n->fcw, c->fcw); R_CMP("fsw", n->fsw, c->fsw); R_CMP("df", n->df, c->df);
    R_CMP("f_kind", n->f_kind, c->f_kind); R_CMP("f_op1", n->f_op1, c->f_op1); R_CMP("f_op2", n->f_op2, c->f_op2);
    R_CMP("f_res", n->f_res, c->f_res); R_CMP("f_bits", n->f_bits, c->f_bits);
    R_CMP("f_cf_override", n->f_cf_override, c->f_cf_override); R_CMP("f_of_override", n->f_of_override, c->f_of_override);
    R_CMP("f_cf", n->f_cf, c->f_cf); R_CMP("f_of", n->f_of, c->f_of);
    for (unsigned i = 0; i < 8; ++i)
        if (!n_same_double(n->st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &n->st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st slot %u (lo word)", i); bad++; r_report_mismatch(w, (uint32_t)a, (uint32_t)b);
        }
    unsigned nan_only = 0;
    for (unsigned i = 0; i < R_WIN; i += 4) {
        uint32_t a, b; memcpy(&a, r_pend.win + i, 4); memcpy(&b, win + i, 4);
        if (a == b) continue;
        char w[48]; snprintf(w, sizeof w, "stack[esp%+d]", (int)i - R_BELOW);
        if (n_nan32(a) && n_nan32(b)) { nan_only++; r_report_nan(w, a, b); }   /* NaN payload/sign of a two-NaN operation */
        else { bad++; r_report_mismatch(w, a, b); }
    }
    if (nan_only) RC_ADD(RC_NANONLY, 1);
#undef R_CMP
    n_budget(c, guest_be);
    r_count(&r_pend.info); RC_ADD(RC_VERIFIED, 1);
    if (bad) RC_ADD(RC_MISMATCHED, 1);
}

void xv_native_606b0_report(unsigned frames);

/* ===============================================================================================================
 * 2. f_000602F0: one cluster's BSP lens-flare markers into the flare list.
 *    Frame: S = esp in the body (entry esp - 0x5C); the window [S - 0x54, S + 0x5C) holds 602F0's locals (the
 *    direction/perpendicular vectors at S+1C/S+28, the flare record at S+34..S+5B) and every callee frame below S (the
 *    deepest: 61270 -> 19E7B -> 1EC1F / 1EABA at S - 0x4C).
 * =============================================================================================================== */
enum { P_BELOW = 0x54, P_WIN = P_BELOW + 0x5C };
typedef struct { unsigned markers, added, resets, floors, be; } p_info;

/* f_0001EC1F (_controlfp(new, mask) with the arguments at kr + 4 / kr + 8), the return address at kr */
static inline __attribute__((always_inline)) void p_controlfp(rs *s, int kr, uint32_t ret)
{
    const int P = kr - 4;                            /* its ebp */
    sw32(s, kr, ret);
    sw32(s, P, s->ebp);                              /* push ebp; mov ebp,esp */
    s->ebp = s->E + (uint32_t)P;
    sw32(s, P - 4, s->ecx);                          /* push ecx */
    sw16(s, P - 4, s->fcw);                          /* fnstcw [ebp-4] */
    s->eax = sr32(s, P + 0xC);
    s->ecx = sr32(s, P + 8);
    s->ecx &= sr32(s, P + 0xC);
    s->eax = ~s->eax;
    s->eax &= sr32(s, P - 4);                        /* the stored control word and the pushed ecx's high half */
    s->eax |= s->ecx;
    sw32(s, P + 0xC, s->eax);
    s->fcw = sr16(s, P + 0xC);                       /* fldcw */
    s->eax = (uint32_t)(int32_t)(int16_t)sr16(s, P - 4);
    s->ebp = sr32(s, P);                             /* leave; ret */
}

/* f_00019E7B (CRT floor: round the double at S-0x20 under the control word [1F2840], restore the old word) called
 * from 61270 at depth 0 with the return address at S-0x24; the result at depth 1. Finite input and the precision
 * exception masked (checked at entry): the 19F01 path, through 19F15 directly (exact) or via 19F23 (a back-edge). */
static inline __attribute__((always_inline)) void p_floor(rs *s, nfl *fl, const nm *m, uint32_t ret)
{
    double *X = s->X;
    enum { T = -0x24, B = -0x28 };
    sw32(s, T, ret);
    sw32(s, B, s->ebp); s->ebp = s->E + (uint32_t)B;        /* push ebp; mov ebp,esp */
    sw32(s, B - 4, s->ecx); sw32(s, B - 8, s->ecx);         /* push ecx x2 */
    sw32(s, B - 0xC, s->ebx); sw32(s, B - 0x10, s->esi);    /* push ebx; push esi */
    s->esi = 0xFFFFu;
    sw32(s, B - 0x14, s->esi); sw32(s, B - 0x18, s->K);     /* push esi; push [1F2840] */
    p_controlfp(s, B - 0x1C, 0x19E93u);
    X[1] = sr64(s, B + 8);
    s->ecx = sr32(s, B - 0x18); s->ecx = sr32(s, B - 0x14); /* pop ecx x2 */
    s->ebx = s->eax;
    s->eax = nm_rd(m, s->E + (uint32_t)(B + 0xE), 4);      /* mov eax,[ebp+0Eh]: 2-aligned, may straddle */
    sw32(s, B - 0x14, s->ecx);
    { const uint16_t ax = (uint16_t)(s->eax & 0x7FF0u); s->eax = (s->eax & 0xFFFF0000u) | ax;
      FLG(XK_SUB, ax, 0x7FF0u, (uint16_t)(ax - 0x7FF0u), 16); }
    sw32(s, B - 0x18, s->ecx);
    sw64(s, B - 0x18, X[1]);                                /* fstp qword [esp] (depth 0) */
    /* 19F01: f_0001EABA (frndint under the current control word) */
    sw32(s, B - 0x1C, 0x19F06u);
    sw32(s, B - 0x20, s->ecx); sw32(s, B - 0x24, s->ecx);
    X[1] = sr64(s, B - 0x18);
    X[1] = nx_round(s->fcw, X[1]);
    sw64(s, B - 0x24, X[1]); X[1] = sr64(s, B - 0x24);
    s->ecx = sr32(s, B - 0x24); s->ecx = sr32(s, B - 0x20);
    sw64(s, B - 8, X[1]);                                   /* fst qword [ebp-8] */
    CMP(X[1], sr64(s, B + 8), 1);                           /* fcomp qword [ebp+8] */
    s->ecx = sr32(s, B - 0x18); s->ecx = sr32(s, B - 0x14);
    FNSTSW_AX();
    { const uint32_t r = AH() & 0x44u; FLG(XK_LOGIC, 0, 0, r, 8);
      if (nf_parity_even(r)) {                              /* 19F23: inexact; masked (entry check): back to 19F15 */
          FLG(XK_LOGIC, 0, 0, s->ebx & 0x20u, 8);
          s->be++;
      } }
    sw32(s, B - 0x14, s->esi); sw32(s, B - 0x18, s->ebx);   /* 19F15: push esi; push ebx */
    p_controlfp(s, B - 0x1C, 0x19F1Cu);
    X[1] = sr64(s, B - 8);
    s->ecx = sr32(s, B - 0x18); s->ecx = sr32(s, B - 0x14);
    s->esi = sr32(s, B - 0x10); s->ebx = sr32(s, B - 0xC);  /* 19F45: pop esi; pop ebx; leave; ret */
    s->ebp = sr32(s, B);
}

/* 61270's clamp of one component to [-1, 1] (depth 1) */
static inline __attribute__((always_inline)) void p_clamp(rs *s, nfl *fl, int k)
{
    double *X = s->X;
    X[1] = srf(s, k); CMP(X[1], s->c_abc, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 5u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) { X[1] = s->c_abc; return; } }
    X[1] = srf(s, k); CMP(X[1], s->c_a78, 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) { X[1] = s->c_a78; return; } }
    X[1] = srf(s, k);
}
static inline uint32_t p_fistp(uint16_t fcw, double v)
{
    const double r = nx_round(fcw, v);
    return (r >= -2147483648.0 && r <= 2147483647.0) ? (uint32_t)(int32_t)r : 0x80000000u;
}
/* f_00061270 (pack the vector at frame offset kv into 11/11/10 bits -> eax), its pointer argument at S-4 and the
 * return address at S-8; ret 4. */
static inline __attribute__((always_inline)) void p_61270(rs *s, nfl *fl, const nm *m, int kv, uint32_t ret)
{
    double *X = s->X;
    sw32(s, -8, ret);
    s->ecx = sr32(s, -4);
    p_clamp(s, fl, kv);
    X[1] = X[1] * s->c_c34;
    sw32(s, -0x14, s->esi); sw32(s, -0x18, s->edi);         /* push esi; push edi */
    sw64(s, -0x20, X[1]);                                   /* sub esp,8; fstp qword [esp] */
    p_floor(s, fl, m, 0x612BAu);
    swf(s, -0x10, X[1]);
    X[1] = srf(s, -0x10); sw32(s, -0xC, p_fistp(s->fcw, X[1]));
    s->ecx = sr32(s, -4);
    s->edi = sr32(s, -0xC);
    p_clamp(s, fl, kv + 4);
    s->edi &= 0x7FFu;                                       /* (between the compare and fnstsw in the guest) */
    X[1] = X[1] * s->c_c34;
    sw64(s, -0x20, X[1]);
    p_floor(s, fl, m, 0x6131Bu);
    swf(s, -0xC, X[1]);
    X[1] = srf(s, -0xC); sw32(s, -0x10, p_fistp(s->fcw, X[1]));
    s->ecx = sr32(s, -4);
    s->esi = sr32(s, -0x10);
    p_clamp(s, fl, kv + 8);
    s->esi &= 0x7FFu;
    X[1] = X[1] * s->c_c30;
    sw64(s, -0x20, X[1]);
    p_floor(s, fl, m, 0x6137Cu);
    swf(s, -0xC, X[1]);
    X[1] = srf(s, -0xC); sw32(s, -0x10, p_fistp(s->fcw, X[1]));
    s->eax = sr32(s, -0x10);
    s->eax = nf_shl32(fl, s->eax, 11); s->eax |= s->esi;
    s->eax = nf_shl32(fl, s->eax, 11); s->eax |= s->edi;
    s->edi = sr32(s, -0x18); s->esi = sr32(s, -0x14);      /* pop edi; pop esi; add esp,8; ret 4 */
}

/* f_000B1260 (a vector perpendicular to [S+1C] into [S+28]; edx/ecx point there), return address at S-4. */
static inline __attribute__((always_inline)) void p_B1260(rs *s, nfl *fl)
{
    double *X = s->X;
    enum { KI = 0x1C, KO = 0x28, L = -0xC };
    sw32(s, -4, 0x603A7u);
    X[1] = srf(s, KI); X[1] = fabs(X[1]);
    X[2] = srf(s, KI + 4); X[2] = fabs(X[2]); swf(s, L, X[2]);
    X[2] = srf(s, KI + 8); X[2] = fabs(X[2]); swf(s, L + 4, X[2]);
    CMP(X[1], srf(s, L), 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (nf_parity_even(r)) goto pB12AB; }
    CMP(X[1], srf(s, L + 4), 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (nf_parity_even(r)) goto pB12AB; }
    sw32(s, KO, 0); s->eax = sr32(s, KI + 8); sw32(s, KO + 4, s->eax);   /* |x| smallest: (0, z, -y) */
    X[1] = srf(s, KI + 4); X[1] = -X[1]; s->eax = s->ecx; swf(s, KO + 8, X[1]);
    return;
pB12AB:
    X[1] = srf(s, L); CMP(X[1], srf(s, L + 4), 1); FNSTSW_AX();
    { const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (nf_parity_even(r)) goto pB12D3; }
    X[1] = srf(s, KI + 8); sw32(s, KO + 4, 0); X[1] = -X[1]; s->eax = s->ecx; swf(s, KO, X[1]);   /* |y| smallest: (-z, 0, x) */
    s->edx = sr32(s, KI); sw32(s, KO + 8, s->edx);
    return;
pB12D3:
    s->eax = sr32(s, KI + 4); sw32(s, KO, s->eax);                     /* |z| smallest: (y, -x, 0) */
    X[1] = srf(s, KI); X[1] = -X[1]; sw32(s, KO + 8, 0); swf(s, KO + 4, X[1]); s->eax = s->ecx;
}

/* f_0005FE80 (append the flare record at [edx] = S+34 to the flare list), return address at S-4. */
static inline __attribute__((always_inline)) void p_5FE80(rs *s, nfl *fl, const nm *m, p_info *info)
{
    double *X = s->X;
    enum { R = 0x34 };
    sw32(s, -4, 0x60438u);
    if (s->barriers) xv_flare_barrier(6u);
    FLG(XK_SUB, s->g_6c0, 0u, s->g_6c0, 16);
    sw32(s, -8, s->ebx); sw32(s, -0xC, s->esi); sw32(s, -0x10, s->edi);
    if (!nf_z(fl)) goto pFFAD;
    s->ecx = nm_i32(m, 0x2E34E0u);
    FLG(XK_SUB, s->ecx, 0x400u, s->ecx - 0x400u, 32);
    if (nf_s(fl) == nf_o(fl)) goto pFF9D;           /* the list is full */
    X[1] = srf(s, R + 4); s->esi = sr32(s, R); X[1] = X[1] - s->g_6c8;
    X[2] = srf(s, R + 8); X[2] = X[2] - s->g_6cc;
    X[3] = srf(s, R + 0xC); X[3] = X[3] - s->g_6d0;
    X[4] = nm_f32(m, s->esi + 0x1Cu); CMP(X[4], s->c_a68, 4); FNSTSW_AX();
    { const uint32_t r = AH() & 0x44u; FLG(XK_LOGIC, 0, 0, r, 8); if (!nf_parity_even(r)) goto pFF08; }
    X[4] = s->g_6dc; X[4] = X[4] * X[3];
    X[5] = s->g_6d8; X[5] = X[5] * X[2]; X[4] = X[4] + X[5];
    X[5] = s->g_6d4; X[5] = X[5] * X[1]; X[4] = X[4] + X[5];
    CMP(X[4], nm_f32(m, s->esi + 0x1Cu), 4);        /* fcomp; fstp st(0); fnstsw ax; fstp st(0); test; fstp st(0) */
    FNSTSW_AX();
    { const uint32_t r = AH() & 5u; FLG(XK_LOGIC, 0, 0, r, 8); if (nf_parity_even(r)) goto pFFAD; }
pFF08:
    { const uint32_t r = sr32(s, R + 0x18) & 0xFF000000u; FLG(XK_LOGIC, 0, 0, r, 32); if (nf_c(fl) || nf_z(fl)) goto pFFAD; }
    info->added++;
    s->eax = s->ecx; FL_INCDEC(); s->ecx += 1u;
    s->ebx = s->eax * 5u;
    nm_wi32(m, 0x2E34E0u, s->ecx);
    s->ebx = s->ebx * 8u + 0x2C76D0u;
    s->esi = s->edx; s->edi = s->ebx;
    for (unsigned k = 0; k < 10; ++k) {              /* rep movsd (ecx = 10): element-wise, page-split */
        uint32_t v; nm_gread(m, &v, s->esi, 4); nm_gwrite(m, s->edi, &v, 4); s->esi += 4u; s->edi += 4u;
    }
    s->ecx = 0;
    s->eax = (s->eax & 0xFFFF0000u) | sr16(s, R + 0x1C);
    { const uint16_t a = (uint16_t)s->eax; FLG(XK_SUB, a, 0xFFFFu, (uint16_t)(a - 0xFFFFu), 16); if (!nf_z(fl)) goto pFF74; }
    s->eax = (s->eax & 0xFFFF0000u) | sr16(s, R + 0x1E);
    { const uint16_t a = (uint16_t)s->eax; FLG(XK_SUB, a, 0xFFFFu, (uint16_t)(a - 0xFFFFu), 16); if (!nf_z(fl)) goto pFF51; }
    s->edi = sr32(s, -0x10); s->esi = sr32(s, -0xC);
    nm_wr(m, s->ebx + 0x1Eu, 2, 0x8000u);
    s->ebx = sr32(s, -8);
    return;
pFF51:
    s->ecx = (uint32_t)(int32_t)(int16_t)sr16(s, R + 0x20);
    s->eax = (uint32_t)(int32_t)(int16_t)s->eax;
    s->eax = nf_shl32(fl, s->eax, 16);
    s->eax |= s->ecx;
    s->edx = s->eax + 8u;
    s->eax = nf_sar32(fl, s->eax, 16);
    s->edi = sr32(s, -0x10);
    s->eax |= 0xFFFF8000u;
    s->esi = sr32(s, -0xC);
    nm_wr(m, s->ebx + 0x20u, 2, s->edx & 0xFFFFu);
    nm_wr(m, s->ebx + 0x1Eu, 2, s->eax & 0xFFFFu);
    s->ebx = sr32(s, -8);
    return;
pFF74:
    s->edx = (uint32_t)(int32_t)(int16_t)nm_rd(m, s->ebx + 0x1Eu, 2);
    s->edx = nf_imul32(fl, s->edx, 0x22u);
    s->edx += 0x2BFFD0u;
    { const uint16_t a = (uint16_t)s->eax, b = (uint16_t)nm_rd(m, s->edx, 2); FLG(XK_SUB, a, b, (uint16_t)(a - b), 16); if (nf_z(fl)) goto pFFAD; }
    info->resets++;
    if (s->barriers) xv_flare_barrier(3u);
    s->eax = 0; s->edi = s->edx + 2u;
    for (unsigned k = 0; k < 8; ++k) { const uint32_t z = 0; nm_gwrite(m, s->edi, &z, 4); s->edi += 4u; }   /* rep stosd */
    s->ecx = 0;
    s->eax = nm_rd(m, s->ebx + 0x1Cu, 2);
    s->edi = sr32(s, -0x10); s->esi = sr32(s, -0xC);
    nm_wr(m, s->edx, 2, s->eax & 0xFFFFu);
    s->ebx = sr32(s, -8);
    return;
pFF9D:
    s->eax = (s->eax & ~0xFFu) | nm_i8(m, 0x2E34E4u);
    { const uint8_t al = (uint8_t)s->eax; FLG(XK_LOGIC, 0, 0, al, 8); if (al) goto pFFAD; }
    nm_wi8(m, 0x2E34E4u, 1);
pFFAD:
    s->edi = sr32(s, -0x10); s->esi = sr32(s, -0xC); s->ebx = sr32(s, -8);
}

/* The whole f_000602F0. Returns 1 (ran, state committed) or 0 (declined, nothing written). */
static int p_run(xctx *c, p_info *info, int barriers)
{
    const uint32_t E0 = c->r[4], Sa = E0 - 0x5Cu, wlo = Sa - P_BELOW;
    memset(info, 0, sizeof *info);
    if (E0 & 3u) return 0;                          /* 4-aligned frame: no constant-offset access straddles a page */
    if (!(c->fcw & 0x20u)) return 0;                /* precision exception unmasked: the CRT floor raises (guest path) */
    if (nm_overlap(wlo, P_WIN, 0x1F0000u, 0x1000u) || nm_overlap(wlo, P_WIN, 0x1F2840u, 4u) || nm_overlap(wlo, P_WIN, 0x39BE58u, 4u) ||
        nm_overlap(wlo, P_WIN, 0x39CE24u, 4u) || nm_overlap(wlo, P_WIN, 0x2FEB8Au, 1u) || nm_overlap(wlo, P_WIN, 0x2FC000u, 0x1000u) ||
        nm_overlap(wlo, P_WIN, 0x2E34E0u, 8u) || nm_overlap(wlo, P_WIN, 0x2C76D0u, 40u * 1024u))
        return 0;                                   /* (the flare rows: then [S+50h] stays the 0xFFFF stored before each
                                                     * 5FE80, whose surface-slot path and its draining barrier are unreachable) */
    uint8_t W[P_WIN] __attribute__((aligned(8)));
    nm mm; nm_init(&mm, W, wlo, P_WIN); const nm *const m = &mm;
    rs state; rs *const s = &state; double *const X = s->X;
    nfl flags = { c->f_kind, c->f_op1, c->f_op2, c->f_res, c->f_bits, c->f_cf_override, c->f_cf, c->f_of_override, c->f_of };
    nfl *const fl = &flags;
    s->S = W + P_BELOW; s->E = Sa; s->barriers = barriers;
    s->F = c->fsp & 7u;
    for (unsigned d = 0; d < 8; ++d) X[d] = c->st[(s->F - d) & 7u];
    s->fsw = c->fsw; s->fcw = c->fcw; s->touched = 0; s->be = 0;
    s->eax = c->r[0]; s->ecx = c->r[1]; s->edx = c->r[2]; s->ebx = c->r[3]; s->ebp = c->r[5]; s->esi = c->r[6]; s->edi = c->r[7];
    {   /* constants (page 1F0000) and render camera (page 2FC000): one translation per page (see r_run) */
        const uint8_t *k = NM_HP(m, 0x1F0000u), *g = NM_HP(m, 0x2FC000u);
#define KF(p, a) ({ float v_; memcpy(&v_, (p) + ((a) & 0xFFFu), 4); (double)v_; })
        s->c_a68 = KF(k, 0x1F0A68u); s->c_a78 = KF(k, 0x1F0A78u); s->c_abc = KF(k, 0x1F0ABCu); s->c_b14 = KF(k, 0x1F0B14u);
        s->c_c30 = KF(k, 0x1F0C30u); s->c_c34 = KF(k, 0x1F0C34u); memcpy(&s->c_af8, k + 0xAF8u, 8);
        s->g_6c8 = KF(g, 0x2FC6C8u); s->g_6cc = KF(g, 0x2FC6CCu); s->g_6d0 = KF(g, 0x2FC6D0u);
        s->g_6d4 = KF(g, 0x2FC6D4u); s->g_6d8 = KF(g, 0x2FC6D8u); s->g_6dc = KF(g, 0x2FC6DCu);
#undef KF
    }
    s->K = nm_i32(m, 0x1F2840u); s->g_6c0 = nm_i16(m, 0x2FC6C0u);
    /* prologue */
    s->eax = nm_i32(m, 0x39BE58u);                  /* the BSP */
    s->edx = nm_rd(m, s->eax + 0x138u, 4);          /* its clusters */
    sw32(s, 0xC, s->ebx);                           /* sub esp,4Ch; push ebx */
    s->ebx = (uint32_t)(int32_t)(int16_t)s->ecx;
    s->ebx = nf_imul32(fl, s->ebx, 0x68u);
    s->ebx += s->edx;                               /* the cluster */
    { const uint16_t n = (uint16_t)nm_rd(m, s->ebx + 0x42u, 2); FLG(XK_SUB, n, 0u, n, 16); }
    sw32(s, 0x14, s->eax); sw32(s, 0x10, 0);
    if (nf_c(fl) || nf_z(fl)) {                     /* no markers: pop ebx; add esp,4Ch; ret */
        s->ebx = sr32(s, 0xC);
        goto out;
    }
    sw32(s, 8, s->ebp); sw32(s, 4, s->esi); sw32(s, 0, s->edi);
    for (;;) {                                      /* 60330: one marker (eax = the BSP) */
        info->markers++;
        s->edi = nm_rd(m, s->ebx + 0x40u, 2);
        s->ecx = sr32(s, 0x10);
        s->edx = nm_rd(m, s->eax + 0x12Cu, 4);      /* the markers */
        s->edi += s->ecx;
        s->esi = s->edi;
        s->esi = nf_shl32(fl, s->esi, 4);
        s->ecx = (uint32_t)(int32_t)(int8_t)nm_rd(m, s->esi + s->edx + 0xDu, 1);
        s->ebp = nm_rd(m, s->esi + s->edx + 0xFu, 1);
        s->esi += s->edx;
        s->edx = nm_rd(m, s->eax + 0x120u, 4);      /* the lens flares */
        s->eax = (uint32_t)(int32_t)(int8_t)nm_rd(m, s->esi + 0xCu, 1);
        sw32(s, 0x18, s->eax);
        s->ebp = nf_shl32(fl, s->ebp, 4);
        s->ebp += s->edx;
        s->touched = 1;
        X[1] = (double)(int32_t)sr32(s, 0x18);
        s->edx = (uint32_t)(int32_t)(int8_t)nm_rd(m, s->esi + 0xEu, 1);
        X[1] = X[1] * s->c_b14;
        sw32(s, 0x18, s->ecx);
        s->ecx = s->E + 0x28u;
        swf(s, 0x1C, X[1]);
        X[1] = (double)(int32_t)sr32(s, 0x18);
        sw32(s, 0x18, s->edx);
        s->edx = s->E + 0x1Cu;
        X[1] = X[1] * s->c_b14;
        swf(s, 0x20, X[1]);
        X[1] = (double)(int32_t)sr32(s, 0x18); X[1] = X[1] * s->c_b14; swf(s, 0x24, X[1]);
        p_B1260(s, fl);
        s->ecx = s->E + 0x1Cu; r_11120(s, fl, 0x1C, 0x603B0u);
        s->ecx = s->E + 0x28u; r_11120(s, fl, 0x28, 0x603BBu);
        s->eax = s->E + 0x1Cu; sw32(s, -4, s->eax);
        p_61270(s, fl, m, 0x1C, 0x603C7u);
        s->ecx = s->E + 0x28u; sw32(s, -4, s->ecx);
        sw32(s, 0x44, s->eax);
        p_61270(s, fl, m, 0x28, 0x603D5u);
        s->edx = nm_rd(m, s->ebp + 0xCu, 4) & 0xFFFFu;
        sw32(s, 0x48, s->eax);
        s->eax = nm_i32(m, 0x39CE24u);              /* the tag instances */
        s->edx = nf_shl32(fl, s->edx, 5);
        s->ecx = nm_rd(m, s->edx + s->eax + 0x14u, 4);
        s->edx = nm_rd(m, s->esi, 4); s->eax = nm_rd(m, s->esi + 4u, 4);
        sw32(s, 0x38, s->edx); sw32(s, 0x3C, s->eax);
        s->eax = 0xFFFFFFFFu;
        s->edx = s->edi; s->edx = nf_sar32(fl, s->edx, 16);
        sw32(s, 0x34, s->ecx);
        s->ecx = nm_rd(m, s->esi + 8u, 4);
        sw32(s, 0x4C, s->eax); sw16(s, 0x50, (uint16_t)s->eax);
        s->eax = (s->eax & ~0xFFu) | nm_i8(m, 0x2FEB8Au);
        sw16(s, 0x52, (uint16_t)s->edx);
        s->edx = s->E + 0x34u;
        sw32(s, 0x40, s->ecx); sw8(s, 0x57, 0); sw16(s, 0x54, (uint16_t)s->edi); sw8(s, 0x56, (uint8_t)s->eax);
        p_5FE80(s, fl, m, info);
        s->eax = sr32(s, 0x10);
        s->ecx = nm_rd(m, s->ebx + 0x42u, 2);
        FL_INCDEC(); s->eax += 1u;
        FLG(XK_SUB, s->eax, s->ecx, s->eax - s->ecx, 32);
        sw32(s, 0x10, s->eax);
        if (!F_JL()) break;
        s->be++;
        s->eax = sr32(s, 0x14);                     /* 60323 */
    }
    s->edi = sr32(s, 0); s->esi = sr32(s, 4); s->ebp = sr32(s, 8); s->ebx = sr32(s, 0xC);
out:
    x_guest_write_pages(wlo, W, P_WIN);
    c->r[0] = s->eax; c->r[1] = s->ecx; c->r[2] = s->edx; c->r[3] = s->ebx; c->r[5] = s->ebp; c->r[6] = s->esi; c->r[7] = s->edi;
    c->r[4] = E0 + 4u;
    for (unsigned d = 1; d < 8; ++d) c->st[(s->F - d) & 7u] = X[d];
    if (s->touched) c->fsp = s->F;
    c->fsw = s->fsw; c->fcw = s->fcw;
    c->f_kind = fl->kind; c->f_op1 = fl->op1; c->f_op2 = fl->op2; c->f_res = fl->res; c->f_bits = fl->bits;
    c->f_cf_override = fl->cfo; c->f_cf = fl->cf; c->f_of_override = fl->ofo; c->f_of = fl->of;
    info->be = s->be;
    return 1;
}
/* ---- f_000602F0 hook ------------------------------------------------------------------------------------------ */
enum { PC_CALLS, PC_NATIVE, PC_VERIFIED, PC_MISMATCHED, PC_DECLINED, PC_MARKERS, PC_ADDED, PC_FLOORS, PC_TIMED_NATIVE,
       PC_TIMED_GUEST, PC_N };
static unsigned p_counter[PC_N], p_mismatch_total;
static uint64_t p_native_us, p_guest_us;
#define PC_ADD(i, v) __atomic_fetch_add(&p_counter[i], (unsigned)(v), __ATOMIC_RELAXED)
static xctx *volatile p_guest_ctx;               /* verify mode re-runs the guest body of this context (the hook stands aside) */
extern void f_000602F0(xctx *);
static int p_mode_value = -1;
static int p_mode(void)
{
    int mode = __atomic_load_n(&p_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_602F0"); mode = e ? atoi(e) : XV_NATIVE_602F0_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        XK_LOG("[native-602f0] f_000602F0 lens-flare marker collection: %s\n",
               mode == 2 ? "native" : mode == 1 ? "verify (native vs guest, guest result kept)" : "off");
        __atomic_store_n(&p_mode_value, mode, __ATOMIC_RELAXED);
    }
    return mode;
}
void xv_native_602f0_force(int mode) { __atomic_store_n(&p_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); p_mismatch_total = 0; }
static int p_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_602F0_TIME"); on = e && atoi(e) != 0; }
    return on;
}
static void p_count(const p_info *o) { PC_ADD(PC_MARKERS, o->markers); PC_ADD(PC_ADDED, o->added); PC_ADD(PC_FLOORS, o->markers * 6u); }
static void p_report_mismatch(const char *what, uint32_t a, uint32_t b, const p_info *o)
{
    if (__atomic_add_fetch(&p_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-602f0] MISMATCH %s native %08X guest %08X (call: %u markers, %u added)\n", what, a, b, o->markers, o->added);
}
/* verify-mode copies of what the call can write: the flare rows from the current count on, the count and the
 * list-full byte (the stack window is on the host stack) */
static uint8_t *p_rows_before, *p_rows_native; static int p_busy;

/* Entry hook of f_000602F0: 1 = handled (guest body skipped). */
int xv_native_602f0(xctx *c)
{
    const int mode = p_mode();
    if (p_guest_ctx == c) return 0;
    const int timed = p_timing();
    if (!mode) {
        if (!timed) return 0;
        const uint64_t t0 = xk_os_monotonic_us();
        p_guest_ctx = c; f_000602F0(c); p_guest_ctx = 0;
        __atomic_fetch_add(&p_guest_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); PC_ADD(PC_TIMED_GUEST, 1); PC_ADD(PC_CALLS, 1);
        return 1;
    }
    p_info o;
    PC_ADD(PC_CALLS, 1);
    if (mode == 2) {
        const uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
        if (!p_run(c, &o, 1)) { PC_ADD(PC_DECLINED, 1); return 0; }
        if (timed) { __atomic_fetch_add(&p_native_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); PC_ADD(PC_TIMED_NATIVE, 1); }
        n_budget(c, o.be); p_count(&o); PC_ADD(PC_NATIVE, 1);
        return 1;
    }
    if (__atomic_exchange_n(&p_busy, 1, __ATOMIC_ACQUIRE)) {     /* another thread is verifying: guest only */
        PC_ADD(PC_DECLINED, 1);
        p_guest_ctx = c; f_000602F0(c); p_guest_ctx = 0;
        return 1;
    }
    enum { ROWS = 40u * 1024u };
    if (!p_rows_before && (!(p_rows_before = malloc(ROWS)) || !(p_rows_native = malloc(ROWS)))) {
        __atomic_store_n(&p_busy, 0, __ATOMIC_RELEASE);
        XK_LOG("[native-602f0] verify: no memory for the row copies: verification off, guest path\n");
        xv_native_602f0_force(0); return 0;
    }
    const uint32_t wlo = c->r[4] - 0x5Cu - P_BELOW;
    uint32_t c0 = X_IMG32(0x2E34E0u); if ((int32_t)c0 < 0) c0 = 0; if (c0 > 1024u) c0 = 1024u;
    const uint32_t rows = 0x2C76D0u + 40u * c0, nrows = ROWS - 40u * c0;
    uint8_t win_before[P_WIN], win_native[P_WIN], win_guest[P_WIN], cnt_before[8], cnt_native[8], cnt_guest[8];
    x_guest_read_pages(win_before, wlo, P_WIN);
    x_guest_read_pages(p_rows_before, rows, nrows);
    x_guest_read_pages(cnt_before, 0x2E34E0u, 8);
    const xctx before = *c;
    const uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
    const int ran = p_run(c, &o, 0);             /* no barrier calls: the guest pass makes them */
    const uint64_t t1 = timed ? xk_os_monotonic_us() : 0;
    if (!ran) {
        __atomic_store_n(&p_busy, 0, __ATOMIC_RELEASE);
        PC_ADD(PC_DECLINED, 1);
        p_guest_ctx = c; f_000602F0(c); p_guest_ctx = 0;
        return 1;
    }
    const xctx native = *c;
    x_guest_read_pages(win_native, wlo, P_WIN);
    x_guest_read_pages(p_rows_native, rows, nrows);
    x_guest_read_pages(cnt_native, 0x2E34E0u, 8);
    x_guest_write_pages(wlo, win_before, P_WIN);
    x_guest_write_pages(rows, p_rows_before, nrows);
    x_guest_write_pages(0x2E34E0u, cnt_before, 8);
    *c = before; c->preempt = 1 << 30;
    const uint64_t t2 = timed ? xk_os_monotonic_us() : 0;
    p_guest_ctx = c; f_000602F0(c); p_guest_ctx = 0;
    const uint64_t t3 = timed ? xk_os_monotonic_us() : 0;
    if (timed) {
        __atomic_fetch_add(&p_native_us, t1 - t0, __ATOMIC_RELAXED); __atomic_fetch_add(&p_guest_us, t3 - t2, __ATOMIC_RELAXED);
        PC_ADD(PC_TIMED_NATIVE, 1); PC_ADD(PC_TIMED_GUEST, 1);
    }
    const uint32_t guest_be = (uint32_t)((1 << 30) - c->preempt);
    c->preempt = before.preempt;
    x_guest_read_pages(win_guest, wlo, P_WIN);
    x_guest_read_pages(cnt_guest, 0x2E34E0u, 8);
    unsigned bad = 0;
#define P_CMP(what, a, b) do { if ((a) != (b)) { bad++; p_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &o); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) P_CMP(rn[i], native.r[i], c->r[i]);
    P_CMP("backedges", o.be, guest_be);
    P_CMP("fsp", native.fsp, c->fsp); P_CMP("fcw", native.fcw, c->fcw); P_CMP("fsw", native.fsw, c->fsw); P_CMP("df", native.df, c->df);
    P_CMP("f_kind", native.f_kind, c->f_kind); P_CMP("f_op1", native.f_op1, c->f_op1); P_CMP("f_op2", native.f_op2, c->f_op2);
    P_CMP("f_res", native.f_res, c->f_res); P_CMP("f_bits", native.f_bits, c->f_bits);
    P_CMP("f_cf_override", native.f_cf_override, c->f_cf_override); P_CMP("f_of_override", native.f_of_override, c->f_of_override);
    P_CMP("f_cf", native.f_cf, c->f_cf); P_CMP("f_of", native.f_of, c->f_of);
    for (unsigned i = 0; i < 8; ++i)
        if (!n_same_double(native.st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &native.st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st slot %u (lo word)", i); bad++; p_report_mismatch(w, (uint32_t)a, (uint32_t)b, &o);
        }
    for (unsigned i = 0; i < P_WIN; i += 4) {
        uint32_t a, b; memcpy(&a, win_native + i, 4); memcpy(&b, win_guest + i, 4);
        if (a != b) { char w[48]; snprintf(w, sizeof w, "stack[esp%+d]", (int)i - P_BELOW - 0x5C); bad++; p_report_mismatch(w, a, b, &o); }
    }
    for (unsigned i = 0; i < 8; i += 4) {
        uint32_t a, b; memcpy(&a, cnt_native + i, 4); memcpy(&b, cnt_guest + i, 4);
        P_CMP(i ? "[2E34E4]" : "flare count [2E34E0]", a, b);
    }
    x_guest_read_pages(p_rows_before, rows, nrows);             /* now the guest's rows */
    for (unsigned i = 0; i < nrows; i += 4) {
        uint32_t a, b; memcpy(&a, p_rows_native + i, 4); memcpy(&b, p_rows_before + i, 4);
        if (a != b) { char w[48]; snprintf(w, sizeof w, "flare row %u +%02X", c0 + i / 40u, i % 40u); bad++; p_report_mismatch(w, a, b, &o); break; }
    }
#undef P_CMP
    __atomic_store_n(&p_busy, 0, __ATOMIC_RELEASE);
    n_budget(c, guest_be);
    p_count(&o); PC_ADD(PC_VERIFIED, 1);
    if (bad) PC_ADD(PC_MISMATCHED, 1);
    return 1;
}

static void p_report(unsigned frames)
{
    unsigned n[PC_N];
    for (unsigned i = 0; i < PC_N; ++i) n[i] = __atomic_exchange_n(&p_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t nu = __atomic_exchange_n(&p_native_us, 0, __ATOMIC_RELAXED), gu = __atomic_exchange_n(&p_guest_us, 0, __ATOMIC_RELAXED);
    if (!n[PC_CALLS]) return;
    char timing[160] = "";
    if (n[PC_TIMED_NATIVE] || n[PC_TIMED_GUEST])
        snprintf(timing, sizeof timing, "; us/call native %.2f guest %.2f; ms/frame native %.3f guest %.3f",
                 n[PC_TIMED_NATIVE] ? (double)nu / n[PC_TIMED_NATIVE] : 0.0, n[PC_TIMED_GUEST] ? (double)gu / n[PC_TIMED_GUEST] : 0.0,
                 frames ? (double)nu / frames / 1000.0 : 0.0, frames ? (double)gu / frames / 1000.0 : 0.0);
    XK_LOG("[native-602f0] %u frames: calls %u native %u verified %u mismatched %u (total mismatches %u); markers %u added %u floors %u; declined %u%s\n",
           frames, n[PC_CALLS], n[PC_NATIVE], n[PC_VERIFIED], n[PC_MISMATCHED], __atomic_load_n(&p_mismatch_total, __ATOMIC_RELAXED),
           n[PC_MARKERS], n[PC_ADDED], n[PC_FLOORS], n[PC_DECLINED], timing);
}

void xv_native_606b0_report(unsigned frames)
{
    p_report(frames);

    unsigned n[RC_N];
    for (unsigned i = 0; i < RC_N; ++i) n[i] = __atomic_exchange_n(&r_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t nu = __atomic_exchange_n(&r_native_us, 0, __ATOMIC_RELAXED), gu = __atomic_exchange_n(&r_guest_us, 0, __ATOMIC_RELAXED);
    if (!n[RC_CALLS] && !n[RC_REGIONS]) return;
    char timing[160] = "";
    if (n[RC_TIMED_NATIVE] || n[RC_TIMED_GUEST])
        snprintf(timing, sizeof timing, "; us/region native %.2f guest %.2f; ms/frame native %.3f guest %.3f",
                 n[RC_TIMED_NATIVE] ? (double)nu / n[RC_TIMED_NATIVE] : 0.0, n[RC_TIMED_GUEST] ? (double)gu / n[RC_TIMED_GUEST] : 0.0,
                 frames ? (double)nu / frames / 1000.0 : 0.0, frames ? (double)gu / frames / 1000.0 : 0.0);
    XK_LOG("[native-606b0] %u frames: calls %u regions %u native %u verified %u mismatched %u (total mismatches %u) nan-only %u; flares %u reflections %u draws %u; declined %u stale %u%s\n",
           frames, n[RC_CALLS], n[RC_REGIONS], n[RC_NATIVE], n[RC_VERIFIED], n[RC_MISMATCHED], __atomic_load_n(&r_mismatch_total, __ATOMIC_RELAXED),
           n[RC_NANONLY], n[RC_FLARES], n[RC_REFLECTIONS], n[RC_DRAWS], n[RC_DECLINED], n[RC_STALE], timing);
}
