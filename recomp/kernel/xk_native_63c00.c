/* xk_native_63c00.c - native Halo CE (Xbox 3925) lens-flare visibility test: f_00063C00.
 *
 * f_00060560 (scene flare pass) -> f_00062240 (push eax/ecx/edx; call) -> f_00063C00(point*, size, query id), ~180 calls
 * per frame in the a10 cinematic. The guest function:
 *   f_000637A0  projects the flare: size > [1F0A68], f_000B5EA0 (point by the view matrix 0x2FC72C, native
 *               xv_math_point_transform), four projection dot products (0x2FC860..0x2FC89C), 1/w, the viewport
 *               rectangle 0x2FC6F4, depth clamp; writes screen x/y/z and the half extents to the caller's locals;
 *   then        extents raised to 1.0, four edges clamped to [[1F0D84], [1F0AE4]], floored by f_00019E7B (the CRT
 *               floor: under the control word at 0x1F2840), fistp, int16 corners, area = (x1-x0)*(y1-y0);
 *   area > 0    D3DDevice_BeginVisibilityTest, Begin(QUADLIST), 4x SetVertexData4f(0, x, y, z, 1), End,
 *               EndVisibilityTest(query id); returns the area (0 for an empty rectangle or a failed projection).
 * The native replaces the guest code of 63C00, 637A0, B5EA0 and the four 19E7B calls; the seven D3D HLE calls are made
 * exactly as the guest makes them (same guest stack contents and registers at each call, same XV_HLE_CALL
 * bookkeeping), so the renderer sees the identical call stream.
 *
 * Exact by construction: every guest-visible effect of the stage body is reproduced -
 *  - guest memory addressed as the shards address it (the thread's table and arena base cached per function like the
 *    shard preamble's xram_/xpt_/imgb_, every access translated through the table when it happens; split-aware reads
 *    where the lift uses x87_load_f32; X_IMG* where the lift uses it); each input read once (the lift re-reads image
 *    constants no one writes); the final value of every stack dword the guest writes - the dead stack below esp
 *    included: 637A0's frame, B5EA0's output, the lifted 19E7B frame - and, before each HLE call, exactly the stack
 *    above esp, the registers and the x87 state the guest has there (intermediate values nothing reads are skipped);
 *  - float math in the guest's operand order on doubles, float rounding at every fstp/fld dword round trip,
 *    x87 compares with x87_compare's condition codes and the lift's OR-ed TOP bits, x87_round/x87_store_i32
 *    semantics for frndint/fistp; the unit is compiled -ffp-contract=off;
 *  - registers (eax/ecx/edx exit values, esp += 16 for ret 0Ch), the lazy flags of the last test (and the imul
 *    carry/overflow cells), the x87 slots the x87-regs body and its callees leave (fsp unchanged), fsw, fcw;
 *  - f_00019E7B as the stage runs it: XV_NATIVE_CRT_FLOAT built and on -> xv_native_crt_float(c, 2) semantics (no
 *    stack frame, eax = old cw); otherwise the lifted CRT floor (its frame, fcomp condition codes, registers).
 * Declined (the guest body runs, nothing written): a misaligned esp (a 4-byte slot straddling a page is written
 * with one translation and read back split by the lift) or a point that overlaps the stack window, a non-finite
 * matrix/point/projection/size input on the projection path (NaN payload propagation is the host compiler's operand
 * order), a non-finite floor argument or an inexact floor with the precision exception unmasked (the CRT's
 * matherr/_except paths), and - lifted CRT floor only - a call whose floor back-edges would reach xv_preempt() (the
 * budget is otherwise decremented exactly as the guest's X_PREEMPT does). Counted as "declined" by reason.
 * Assumed (true for these five, checked in verify mode): the D3D HLE calls read only their stack arguments and change
 * only eax and esp. Kept from the stage: the object-math guard (XV_OBJECT_MATH_GUARD) that xv_math_point_transform
 * takes, the phase-timer scope of 637A0 (id 18). Not kept: the [native-point]/[crt-float] counters.
 *
 * XV_NATIVE_63C00 build flag (hook tools/patch_native_63c00_hooks.py at the entry of f_00063C00);
 * env XV_NATIVE_63C00: 0 off (default XV_NATIVE_63C00_DEFAULT), 1 verify (the native runs dry - the HLE calls are
 * recorded, not made - then the guest body on the same state with every D3D HLE entry observed; registers, flags,
 * x87, the stack window and the HLE call records compared; the guest result is kept), 2 native.
 * XV_NATIVE_63C00_TIME=1: us/call (compute until the first HLE call, and the HLE block) for the guest body (mode 0),
 * the native (2) or both (1). Counters: [native-63c00] every 60 frames. */
#include "xk.h"
#include "../xv_x86rt.h"
#include "../xv_phase.h"
#include "xk_object_jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__vita__)
#include <time.h>
#endif

#ifndef XV_NATIVE_63C00_DEFAULT
#define XV_NATIVE_63C00_DEFAULT 0
#endif
#if defined(XV_NATIVE_CRT_FLOAT) && XV_NATIVE_CRT_FLOAT && !defined(XV_NATIVE_CRT_FLOAT_DEFAULT)
#define XV_NATIVE_CRT_FLOAT_DEFAULT 1        /* as xk_crt_float.c */
#endif

enum {
    N63_EPS = 0x1F0A68u, N63_ONE = 0x1F0A78u, N63_HALF = 0x1F0AA0u, N63_LO = 0x1F0D84u, N63_HI = 0x1F0AE4u,
    N63_CW = 0x1F2840u, N63_RECT = 0x2FC6F4u, N63_MATRIX = 0x2FC72Cu, N63_PROJ = 0x2FC860u,
    N63_WINDOW_LO = 0x80u, N63_WINDOW_BYTES = 0x80u + 0x10u,     /* the stack window [E-0x80, E+0x10) */
};
enum { N63_F1 = 1, N63_F2, N63_S };                             /* 637A0 fails at the size / depth test; succeeds */

/* ---- D3D HLE calls, as XV_HLE_CALL makes them ------------------------------------------------------------------ */
extern void xv_hle_D3DDevice_BeginVisibilityTest(xctx *);
extern void xv_hle_D3DDevice_Begin(xctx *);
extern void xv_hle_D3DDevice_SetVertexData4f(xctx *);
extern void xv_hle_D3DDevice_End(xctx *);
extern void xv_hle_D3DDevice_EndVisibilityTest(xctx *);
extern volatile uint32_t xv_cur_fn;
extern int xv_hle_timing; extern unsigned xv_hle_timed_calls; void xv_hle_time_add(const char *, uint64_t);
enum { N63_BEGINVIS, N63_BEGIN, N63_SETV4F, N63_END, N63_ENDVIS, N63_HLE_KINDS };
static const struct { uint32_t addr; void (*fn)(xctx *); const char *macro_name, *d3d_name; uint8_t nargs; } n63_hle_tab[N63_HLE_KINDS] = {
    { 0x181C00u, xv_hle_D3DDevice_BeginVisibilityTest, "xv_hle_D3DDevice_BeginVisibilityTest", "D3DDevice_BeginVisibilityTest", 0 },
    { 0x1846C0u, xv_hle_D3DDevice_Begin, "xv_hle_D3DDevice_Begin", "D3DDevice_Begin", 1 },
    { 0x184580u, xv_hle_D3DDevice_SetVertexData4f, "xv_hle_D3DDevice_SetVertexData4f", "D3DDevice_SetVertexData4f", 5 },
    { 0x184700u, xv_hle_D3DDevice_End, "xv_hle_D3DDevice_End", "D3DDevice_End", 0 },
    { 0x181C30u, xv_hle_D3DDevice_EndVisibilityTest, "xv_hle_D3DDevice_EndVisibilityTest", "D3DDevice_EndVisibilityTest", 1 },
};

/* One HLE call's view of the guest: what the call can read. Recorded by the dry run and by the verify tap. */
typedef struct { uint8_t kind; uint32_t r[8], args[5], fsp; uint16_t fsw, fcw; double st[8]; } n63_hle_rec;
typedef struct { unsigned n; n63_hle_rec rec[8]; } n63_hle_log;
static void n63_record(const xctx *c, unsigned kind, n63_hle_log *log)    /* kind 0xFF: an HLE outside the five */
{
    if (log->n >= 8) { log->n++; return; }
    n63_hle_rec *r = &log->rec[log->n++];
    memset(r, 0, sizeof *r);
    r->kind = (uint8_t)kind; memcpy(r->r, c->r, sizeof r->r); r->fsp = c->fsp; r->fsw = c->fsw; r->fcw = c->fcw;
    memcpy(r->st, c->st, sizeof r->st);
    if (kind < N63_HLE_KINDS) for (unsigned i = 0; i < n63_hle_tab[kind].nargs; ++i) r->args[i] = X_M32(c->r[4] + 4u + 4u * i);
}

typedef struct { n63_hle_log *dry; uint64_t hle_start; } n63_run_ctl;   /* dry != 0: record instead of calling */
static uint64_t n63_now_ns(void);
static inline __attribute__((always_inline)) void n63_hle(xctx *c, unsigned kind, n63_run_ctl *ctl)
{
    if (ctl->dry) {               /* verify: the HLE's guest-visible effect only (eax is dead after every call here) */
        n63_record(c, kind, ctl->dry);
        c->r[0] = 0; c->r[4] += 4u + 4u * n63_hle_tab[kind].nargs;
        return;
    }
    void (*fn)(xctx *) = n63_hle_tab[kind].fn; const uint32_t addr = n63_hle_tab[kind].addr;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if (xv_is_object_job(c)) { xv_object_job_hle(c, addr, fn); return; }
#endif
    uint32_t saved = xv_cur_fn; xv_cur_fn = 0x80000000u | addr;
    if (xv_hle_timing) {
        xv_hle_timed_calls++; uint64_t t0 = xk_os_monotonic_us(); fn(c);
        xv_hle_time_add(n63_hle_tab[kind].macro_name, xk_os_monotonic_us() - t0);
    } else fn(c);
    xv_cur_fn = saved;
}

/* ---- the computation (reads only) ------------------------------------------------------------------------------ */
/* Every guest input is read once, through the calling thread's table, with the access the lift uses for it (X_IMG*
 * for the viewport words and the 19E7B control word, split-aware x87 loads for floats, X_M32 for integer words). The
 * lift re-reads [1F0A68], [1F0A78], [1F0D84], [1F0AE4] and [1F2840] several times: they are image constants no one
 * writes. Values the guest stores to its own frame and reloads (fstp/fld dword, fistp/movsx, the pushed size) are
 * carried in registers with the same rounding; with esp 4-aligned (checked) every such slot lies in one page, where
 * the lift's single-translation and split-aware accesses agree. */
#if defined(XV_NATIVE_63C00_COVERAGE)
/* tools/tests/native_63c00.c: which branches the randomized cases reached (not built into the game). */
enum { CV_F1, CV_F2, CV_Z_CLAMP, CV_RX_RAISE, CV_RY_RAISE, CV_LO, CV_HI, CV_INEXACT, CV_FIST_RANGE, CV_I16_WRAP,
       CV_IMUL_OVF, CV_NEGATIVE, CV_ZERO, CV_DRAWN, CV_SCALE, CV_COUNT };
unsigned xv_native_63c00_cov[CV_COUNT];
#define N63_COV(i) (xv_native_63c00_cov[i]++)
#else
#define N63_COV(i) ((void)0)
#endif
enum { N63_DECLINE_LAYOUT = 1, N63_DECLINE_INPUT, N63_DECLINE_FLOOR, N63_DECLINE_PREEMPT };

typedef struct {
    int path, crt_native;
    uint32_t E, A1, A2, T;                /* T = x87 slot of st(0) after one push: (fsp + 7) & 7 */
    uint16_t fcw, fsw;                    /* entry control word; c->fsw at the exit and at the HLE calls */
    double a2f;
    /* B5EA0 (xv_math_point_transform): the stored point and the dead x87 slots */
    uint32_t scale, P[3]; double second, y_last, z_middle, third, z_last;
    /* 637A0 */
    uint32_t fD1, fD2, fQ1, fQ2, fSX, fSY, fZ, fRX, fRY;   /* float bits stored to the frame */
    double D2, Q2, invw, Z, ONE, RY;
    /* 63C00 */
    uint32_t rx1, ry1;                    /* [E-20h]/[E-1Ch] after the raise to 1.0 */
    uint64_t v4;                          /* the fourth floor's argument (qword at [E-30h]) */
    double r4d;                           /* xr0 after the fourth fld dword: (float) floor */
    uint32_t i[4];                        /* the fistp results: x0 y0 x1 y1 */
    uint16_t cw4;                         /* lifted CRT: the fourth floor's control word (dead frame word) */
    uint64_t r4;                          /* lifted CRT: the fourth floor result (dead frame qword) */
    uint32_t backedges;                   /* lifted CRT: X_PREEMPT count (its inexact path jumps back) */
} n63_plan;

/* Guest memory as the shards address it: the thread's table and arena base cached per function (the shard preamble's
 * xram_/xpt_/imgb_), every access translated through the table at the time of the access. */
#define N63_CACHE() uint8_t *const xram_ = g_xram; const uint32_t *const xpt_ = X_PT; uint8_t *const imgb_ = X_IMG_BASE; \
    (void)xram_; (void)xpt_; (void)imgb_
#define N63_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))
#define N63_M32(a) (*(xu32_u *)N63_G(a))
#define N63_M16(a) (*(xu16_u *)N63_G(a))
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define N63_IMG32(a) (*(xu32_u *)N63_G(a))
#define N63_IMG16(a) (*(xu16_u *)N63_G(a))
#else
#define N63_IMG32(a) (*(xu32_u *)(imgb_ + (uint32_t)(a)))
#define N63_IMG16(a) (*(xu16_u *)(imgb_ + (uint32_t)(a)))
#endif
/* A float/dword the lift loads with x87_load_f32 (x_guest_read: split across pages when it straddles one). */
#define N63_R32(a) (((uint32_t)(a) & 0xFFFu) <= 0xFFCu ? N63_M32(a) : n63_read_split(a))
static uint32_t n63_read_split(uint32_t a) { uint32_t v; x_guest_read_pages(&v, a, 4); return v; }
static inline double n63_d(uint32_t bits) { float f; memcpy(&f, &bits, 4); return (double)f; }
static inline uint32_t n63_fbits(double d) { float f = (float)d; uint32_t b; memcpy(&b, &f, 4); return b; }
static inline int n63_finite32(uint32_t bits) { return (bits & 0x7F800000u) != 0x7F800000u; }
static inline uint32_t n63_i32(double v)    /* x87_store_i32 */
{ return (v >= -2147483648.0 && v <= 2147483647.0) ? (uint32_t)(int32_t)v : 0x80000000u; }
static inline uint16_t n63_cc(xctx *c, double a, double b)   /* the condition codes x87_compare / x87r_compare set */
{ xctx t; (void)c; t.fsw = 0; t.fsp = 0; x87_compare(&t, a, b, 0); return (uint16_t)(t.fsw & 0x4500u); }
static int n63_crt_native(void)
{
#if defined(XV_NATIVE_CRT_FLOAT) && XV_NATIVE_CRT_FLOAT
    static int on = -1;                   /* the same switch xv_native_crt_float reads */
    if (on < 0) { const char *e = getenv("XV_NATIVE_CRT_FLOAT"); on = e ? atoi(e) != 0 : XV_NATIVE_CRT_FLOAT_DEFAULT; }
    return on;
#else
    return 0;
#endif
}

/* 0 = computed, else the decline reason. */
static int n63_compute(xctx *c, n63_plan *p)
{
    N63_CACHE();
    const uint32_t E = c->r[4];
    p->E = E; p->T = (c->fsp + 7u) & 7u; p->fcw = c->fcw;
    p->A1 = N63_M32(E + 4u); p->A2 = N63_M32(E + 8u);
    /* A misaligned esp lets a 4-byte slot straddle a page: the lift writes it with one translation (X_W32) and reads it
     * back split-aware, i.e. from two pages. Halo keeps esp 4-aligned; anything else stays on the guest body. */
    if (E & 3u) return N63_DECLINE_LAYOUT;
    const uint16_t top = (uint16_t)((c->fsw & ~0x4700u) | (p->T << 11));   /* TOP is OR-ed in by every compare */
    const double eps = n63_d(N63_R32(N63_EPS));
    p->a2f = n63_d(p->A2);
    /* ---- 637A0 */
    if (!(p->a2f > eps)) {                                         /* fcomp; test ah,41h; jne */
        p->path = N63_F1; N63_COV(CV_F1);
        p->fsw = (uint16_t)(top | n63_cc(c, p->a2f, eps));
        return 0;
    }
    if (p->A1 + 12u > E - N63_WINDOW_LO && p->A1 < E + 0x10u) return N63_DECLINE_LAYOUT;   /* the point inside the frames written below */
    uint32_t mb[13], vb[3];
    {
        XV_PHASE_SCOPE(c, 18u);
        {
            XV_OBJECT_MATH_GUARD();                                /* as xv_math_point_transform */
            for (unsigned i = 0; i < 13; ++i) mb[i] = N63_R32(N63_MATRIX + 4u * i);
            for (unsigned i = 0; i < 3; ++i) vb[i] = N63_R32(p->A1 + 4u * i);
        }
    }
    uint32_t bad = 0;
    for (unsigned i = 0; i < 13; ++i) bad |= !n63_finite32(mb[i]);
    for (unsigned i = 0; i < 3; ++i) bad |= !n63_finite32(vb[i]);
    uint32_t prb[16];
    for (unsigned i = 0; i < 16; ++i) { prb[i] = N63_R32(N63_PROJ + 4u * i); bad |= !n63_finite32(prb[i]); }
    const uint32_t oneb = N63_R32(N63_ONE), halfb = N63_R32(N63_HALF);
    bad |= !n63_finite32(oneb) | !n63_finite32(halfb) | !n63_finite32(p->A2);
    if (bad) return N63_DECLINE_INPUT;
    double m[13]; for (unsigned i = 0; i < 13; ++i) m[i] = n63_d(mb[i]);
    double x = n63_d(vb[0]), y = n63_d(vb[1]), z = n63_d(vb[2]);
    p->scale = mb[0];
    if (p->scale != 0x3F800000u) { const double s = m[0]; x = x * s; y = y * s; z = z * s; N63_COV(CV_SCALE); }
    double first = (z * m[7] + y * m[4]) + x * m[1]; first = first + m[10];
    const double y_last = x * m[2];
    double second = (z * m[8] + y * m[5]) + y_last; second = second + m[11];
    const double z_middle = y * m[6], z_last = x * m[3];
    double third = (z * m[9] + z_middle) + z_last; third = third + m[12];
    p->second = second; p->y_last = y_last; p->z_middle = z_middle; p->third = third; p->z_last = z_last;
    p->P[0] = n63_fbits(first); p->P[1] = n63_fbits(second); p->P[2] = n63_fbits(third);
    const double P0 = n63_d(p->P[0]), P1 = n63_d(p->P[1]), P2 = n63_d(p->P[2]);   /* fld [esp+1Ch..24h] */
    double pr[16]; for (unsigned i = 0; i < 16; ++i) pr[i] = n63_d(prb[i]);
    const double one = n63_d(oneb), half = n63_d(halfb);
    /* operand order of the lift: products st*mem, faddp earlier+later */
    const double D1 = ((pr[9] * P2 + pr[5] * P1) + pr[1] * P0) + pr[13];      /* 884 874 864 894 -> [esp+30h] */
    const double D2 = ((pr[10] * P2 + pr[6] * P1) + pr[2] * P0) + pr[14];     /* 888 878 868 898 -> fst [esp+10h] */
    const double Q1 = pr[0] * p->a2f, Q2 = pr[5] * p->a2f;                    /* 860, 874 * size */
    p->fD1 = n63_fbits(D1); p->fD2 = n63_fbits(D2); p->fQ1 = n63_fbits(Q1); p->fQ2 = n63_fbits(Q2);
    p->D2 = D2; p->Q2 = Q2;
    if (!(D2 > eps)) {                                             /* not (depth > eps) */
        p->path = N63_F2; N63_COV(CV_F2);
        p->fsw = (uint16_t)(top | n63_cc(c, D2, eps));
        return 0;
    }
    p->path = N63_S;
    const uint32_t e0 = N63_IMG32(N63_RECT), e1 = N63_IMG32(N63_RECT + 4u);   /* eax = [2FC6F4], edi = [2FC6F8] */
    const int32_t H = (int16_t)(uint16_t)(N63_IMG16(N63_RECT + 6u) - N63_IMG16(N63_RECT + 2u));   /* si */
    const int32_t W = (int16_t)(uint16_t)(e1 - e0);                                           /* di */
    const double D3 = ((pr[11] * P2 + pr[7] * P1) + pr[3] * P0) + pr[15];     /* 88C 87C 86C 89C */
    const double invw = one / D3;                                              /* fdivr [1F0A78] */
    const double Hd = (double)H;                                               /* fild [esp+2Ch] */
    const double D4 = ((pr[8] * P2 + pr[4] * P1) + pr[0] * P0) + pr[12];      /* 880 870 860 890 */
    const double SX = (((D4 * invw) + one) * Hd - one) * half;                /* -> [ebp] */
    const double Wd = n63_d(n63_fbits((double)W));                             /* fild, fstp dword, reloaded */
    const double SY = ((one - invw * n63_d(p->fD1)) * Wd - one) * half;       /* -> [ebp+4] */
    double Z = invw * n63_d(p->fD2);
    const uint16_t top3 = (uint16_t)(top | (((p->T + 5u) & 7u) << 11));        /* the depth clamp compares at st(3) */
    const int zkeep = one > Z;                                                 /* je 63935 */
    if (!zkeep) { Z = one; N63_COV(CV_Z_CLAMP); }
    const double RX = ((Hd * invw) * n63_d(p->fQ1)) * half;                    /* -> [eax] */
    const double RY = ((Wd * invw) * n63_d(p->fQ2)) * half;                    /* -> [eax+4] */
    p->invw = invw; p->Z = Z; p->ONE = one; p->RY = RY;
    p->fSX = n63_fbits(SX); p->fSY = n63_fbits(SY); p->fZ = n63_fbits(Z); p->fRX = n63_fbits(RX); p->fRY = n63_fbits(RY);
    /* ---- 63C00: raise the extents to 1.0, clamp and floor the edges */
    const double rxs = n63_d(p->fRX), rys = n63_d(p->fRY);
    const int rx_raise = one > rxs, ry_raise = one > rys;
    if (rx_raise) N63_COV(CV_RX_RAISE);
    if (ry_raise) N63_COV(CV_RY_RAISE);
    p->rx1 = rx_raise ? 0x3F800000u : p->fRX; p->ry1 = ry_raise ? 0x3F800000u : p->fRY;
    const double rx = n63_d(p->rx1), ry = n63_d(p->ry1), sx = n63_d(p->fSX), sy = n63_d(p->fSY);
    const double lo = n63_d(N63_R32(N63_LO)), hi = n63_d(N63_R32(N63_HI));
    p->crt_native = n63_crt_native();
    const uint16_t cw = p->crt_native ? (uint16_t)N63_M32(N63_CW) : (uint16_t)N63_IMG32(N63_CW);   /* 19E7B's fldcw */
    const double edge[4] = { sx - rx, sy - ry, sx + rx, sy + ry };
    double cmp_a = 0, cmp_b = 0;                                       /* the last compare: its condition codes stay in fsw */
    double r = 0;
    p->backedges = 0;
    for (unsigned k = 0; k < 4; ++k) {
        double v = edge[k];
        cmp_a = v;
        if (v < lo) { cmp_b = lo; v = lo; N63_COV(CV_LO); }           /* test ah,5; jp not taken */
        else { cmp_b = hi; if (v > hi) { v = hi; N63_COV(CV_HI); } }   /* test ah,41h; jne not taken */
        /* f_00019E7B: NaN/inf -> matherr; round under [1F2840]; inexact with the precision exception unmasked -> _except */
        uint64_t vb64; memcpy(&vb64, &v, 8);
        if (((uint32_t)(vb64 >> 32) & 0x7FF00000u) == 0x7FF00000u) return N63_DECLINE_FLOOR;
        c->fcw = cw; r = x87_round(c, v); c->fcw = p->fcw;
        if (r != v) {
            if (!(p->fcw & 0x20u)) return N63_DECLINE_FLOOR;
            N63_COV(CV_INEXACT);
            if (!p->crt_native) p->backedges++;                        /* 19F26 jne 19F15: a back-edge */
        }
        if (!p->crt_native) { cmp_a = r; cmp_b = v; }                  /* 19E7B: fcomp qword [ebp+8] */
        const double rf = n63_d(n63_fbits(r));                         /* fstp dword, fld dword */
        /* fistp: x87_round(rf) under the caller's control word is rf itself - r is integral and a float of an integral
         * double is integral (every float of magnitude >= 2^23 is) - so the store converts rf. */
        const double xr = rf;
        p->i[k] = n63_i32(xr);
#if defined(XV_NATIVE_63C00_COVERAGE)
        if (!(xr >= -2147483648.0 && xr <= 2147483647.0)) N63_COV(CV_FIST_RANGE);
        else if ((int32_t)(int16_t)p->i[k] != (int32_t)p->i[k]) N63_COV(CV_I16_WRAP);
#endif
        if (k == 3) { memcpy(&p->v4, &v, 8); p->r4d = rf; memcpy(&p->r4, &r, 8); p->cw4 = cw; }
    }
    /* The lifted floor's back-edges: X_PREEMPT() each. When one of them would reach xv_preempt() (a scheduling point
     * on the owner, the slice is 20000) the guest body runs it at its place; otherwise only the budget moves. */
    if (p->backedges && (int32_t)c->preempt <= (int32_t)p->backedges) return N63_DECLINE_PREEMPT;
    p->fsw = (uint16_t)(top3 | n63_cc(c, cmp_a, cmp_b));
    return 0;
}

/* ---- the guest-visible effects ---------------------------------------------------------------------------------
 * The final value of every stack dword the guest writes (its own frame, 637A0's, B5EA0's output, the lifted CRT
 * floor's frame), and at each HLE call exactly the stack above esp, the registers and the x87 state the guest has
 * there. The five D3D HLE calls only read their arguments; they write no guest memory. */
#define N63_W(off, v) (N63_M32(E - (off)) = (uint32_t)(v))

enum { N63_EXIT_F, N63_EXIT_EMPTY, N63_EXIT_DRAW };
static int n63_commit(xctx *c, const n63_plan *p, n63_run_ctl *ctl)
{
    N63_CACHE();
    const uint32_t E = p->E, T = p->T;
    uint32_t *const r = c->r;                                     /* eax ecx edx ebx esp ebp esi edi */
    const uint32_t ebx0 = r[3], ebp0 = r[5], esi0 = r[6], edi0 = r[7];
    if (p->path != N63_S) {                                       /* 637A0 failed: its frame and 63C00's pushes */
        N63_W(0x28u, E - 0x20u); N63_W(0x30u, p->A2); N63_W(0x34u, 0x63C1Bu);   /* push eax, (ecx), edx; call 637A0 */
        N63_W(0x50u, ebx0); N63_W(0x54u, ebp0); N63_W(0x58u, esi0); N63_W(0x5Cu, edi0);   /* push ebx ebp esi edi */
        X_FLAGS(XK_LOGIC, 0, 0, 0, 8);                            /* test al,al (al = 0) */
        c->fsw = p->fsw; r[0] = 0; r[4] = E + 0x10u;
        if (p->path == N63_F1) {
            N63_W(0x2Cu, E - 0xCu);
            r[1] = E - 0xCu; r[2] = p->A1;
            c->st[T] = p->a2f;
            return N63_EXIT_F;
        }
        N63_W(0x60u, 0x637EBu);                                   /* call B5EA0 */
        N63_W(0x2Cu, p->fD1);                                     /* [esp+30h] */
        N63_W(0x38u, p->P[2]); N63_W(0x3Cu, p->P[1]); N63_W(0x40u, p->P[0]);   /* B5EA0's output */
        N63_W(0x44u, p->fQ2); N63_W(0x48u, p->fQ1); N63_W(0x4Cu, p->fD2);
        r[1] = N63_MATRIX; r[2] = p->scale;
        c->st[T] = p->D2; c->st[(T - 1u) & 7u] = p->Q2; c->st[(T - 2u) & 7u] = p->z_middle; c->st[(T - 3u) & 7u] = p->second;
        c->st[(T - 4u) & 7u] = p->y_last;
        return N63_EXIT_F;
    }
    /* 637A0 succeeded; 63C00 raised, clamped and floored the four edges */
    const uint32_t x0s = (uint32_t)(int32_t)(int16_t)p->i[0], y0s = (uint32_t)(int32_t)(int16_t)p->i[1];
    const uint32_t x1s = (uint32_t)(int32_t)(int16_t)p->i[2], y1s = (uint32_t)(int32_t)(int16_t)p->i[3];
    const uint32_t dy = y1s - y0s;
    const int64_t prod = (int64_t)(int32_t)(x1s - x0s) * (int32_t)dy;
    const uint32_t area = (uint32_t)prod;
    N63_W(0x60u, 0x637EBu);
    N63_W(0x4u, p->fZ); N63_W(0x8u, p->fSY); N63_W(0xCu, p->fSX);   /* 637A0's screen x/y/z */
    N63_W(0x18u, y0s); N63_W(0x1Cu, p->ry1); N63_W(0x20u, x1s); N63_W(0x28u, esi0);
    const uint32_t vhi = (uint32_t)(p->v4 >> 32), cwhi = vhi & 0xFFFF0000u;
    if (p->crt_native) { N63_W(0x50u, ebx0); N63_W(0x54u, ebp0); N63_W(0x58u, esi0); N63_W(0x5Cu, edi0); }
    else {                                                        /* the lifted floor's frame at S = E-34h (the fourth call) */
        N63_W(0x50u, (uint32_t)(int32_t)(int16_t)p->fcw);         /* push ebx (old cw) */
        N63_W(0x54u, 0x19F1Cu); N63_W(0x58u, E - 0x38u);          /* the second _controlfp's return address, its push ebp */
        N63_W(0x5Cu, cwhi | p->cw4);                              /* its push ecx, low half = fnstcw */
        c->preempt -= (int32_t)p->backedges;                      /* X_PREEMPT, never reaching xv_preempt (checked) */
    }
    c->st[(T - 1u) & 7u] = p->RY; c->st[(T - 2u) & 7u] = p->Z; c->st[(T - 3u) & 7u] = p->ONE; c->st[(T - 4u) & 7u] = p->y_last;
    X_FLAGS(XK_LOGIC, 0, 0, area, 32);                            /* imul esi,eax; test esi,esi */
    c->f_cf = c->f_of = prod != (int64_t)(int32_t)area;
    c->fsw = p->fsw;
    c->st[T] = p->r4d;                                            /* fld dword: the fourth floor */
#if defined(XV_NATIVE_63C00_COVERAGE)
    if (c->f_cf) N63_COV(CV_IMUL_OVF);
    N63_COV((int32_t)area < 0 ? CV_NEGATIVE : area ? CV_DRAWN : CV_ZERO);
#endif
    if ((int32_t)area <= 0) {
        N63_W(0x10u, x0s); N63_W(0x14u, p->i[2]); N63_W(0x24u, y1s);
        N63_W(0x2Cu, vhi); N63_W(0x30u, (uint32_t)p->v4); N63_W(0x34u, 0x63D7Fu);   /* the fourth floor's argument, return */
        if (p->crt_native) {                                      /* 637A0's and B5EA0's leftovers */
            N63_W(0x38u, p->P[2]); N63_W(0x3Cu, p->P[1]); N63_W(0x40u, p->P[0]);
            N63_W(0x44u, p->fQ2); N63_W(0x48u, p->fQ1); N63_W(0x4Cu, p->fD2);
        } else {
            N63_W(0x38u, ebp0); N63_W(0x3Cu, (uint32_t)(p->r4 >> 32)); N63_W(0x40u, (uint32_t)p->r4);   /* push ebp; fst qword [ebp-8] */
            N63_W(0x44u, ebx0); N63_W(0x48u, esi0); N63_W(0x4Cu, cwhi | p->fcw);                        /* push ebx, esi; combined word */
        }
        r[0] = 0; r[1] = y0s; r[2] = x0s; r[4] = E + 0x10u;
        return N63_EXIT_EMPTY;
    }
    /* area > 0: the visibility quad; before each call the stack above esp, the registers and st(0) are the guest's */
    const uint32_t fX0 = n63_fbits((double)(int32_t)x0s), fY0 = n63_fbits((double)(int32_t)y0s);
    const uint32_t fX1 = n63_fbits((double)(int32_t)x1s), fY1 = n63_fbits((double)(int32_t)y1s);
    N63_W(0x10u, x0s); N63_W(0x14u, p->i[2]); N63_W(0x24u, y1s);
    N63_W(0x2Cu, ebx0); N63_W(0x30u, ebp0); N63_W(0x34u, edi0);   /* push ebx ebp edi */
    if (ctl->hle_start == 0) ctl->hle_start = n63_now_ns();
    r[0] = dy; r[1] = y0s; r[2] = x0s; r[6] = area;
    N63_W(0x38u, 0x63DD4u); r[4] = E - 0x38u;
    n63_hle(c, N63_BEGINVIS, ctl);
    N63_W(0x38u, 7u); N63_W(0x3Cu, 0x63DDBu); r[4] = E - 0x3Cu;
    n63_hle(c, N63_BEGIN, ctl);
    /* fild [y0]; mov edi,[z]; push 1.0; fstp [y0]; mov ebx; fild [x0]; push edi; push ebx; fstp [x0]; mov ebp; push ebp; push 0 */
    N63_W(0x38u, 0x3F800000u); N63_W(0x14u, fY0); N63_W(0x3Cu, p->fZ); N63_W(0x40u, fY0);
    N63_W(0x10u, fX0); N63_W(0x44u, fX0); N63_W(0x48u, 0u); N63_W(0x4Cu, 0x63E06u);
    r[3] = fY0; r[5] = fX0; r[7] = p->fZ; r[4] = E - 0x4Cu;
    c->st[T] = (double)(int32_t)x0s;
    n63_hle(c, N63_SETV4F, ctl);
    N63_W(0x10u, fX1); N63_W(0x44u, fX1); N63_W(0x4Cu, 0x63E21u);   /* the re-pushed 1.0, z, y0 and 0 are equal words */
    r[3] = fX1; r[4] = E - 0x4Cu;
    c->st[T] = (double)(int32_t)x1s;
    n63_hle(c, N63_SETV4F, ctl);
    N63_W(0x24u, fY1); N63_W(0x40u, fY1); N63_W(0x4Cu, 0x63E3Cu);
    r[0] = fY1; r[4] = E - 0x4Cu;
    c->st[T] = (double)(int32_t)y1s;
    n63_hle(c, N63_SETV4F, ctl);
    N63_W(0x44u, fX0); N63_W(0x4Cu, 0x63E4Fu);
    r[1] = fY1; r[4] = E - 0x4Cu;
    n63_hle(c, N63_SETV4F, ctl);
    N63_W(0x38u, 0x63E54u); r[4] = E - 0x38u;
    n63_hle(c, N63_END, ctl);
    const uint32_t a3 = N63_M32(E + 0xCu);                        /* mov edx,[esp+40h] */
    N63_W(0x38u, a3); N63_W(0x3Cu, 0x63E5Eu); r[2] = a3; r[4] = E - 0x3Cu;
    n63_hle(c, N63_ENDVIS, ctl);
    r[0] = area; r[3] = ebx0; r[5] = ebp0; r[6] = esi0; r[7] = edi0; r[4] = E + 0x10u;   /* pop edi ebp ebx; mov eax,esi; pop esi; ret 0Ch */
    return N63_EXIT_DRAW;
}
#undef N63_W

/* ---- modes, verify, timing, counters --------------------------------------------------------------------------- */
static uint64_t n63_now_ns(void)
{
#if defined(__vita__)
    return xk_os_monotonic_us() * 1000u;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

enum { NC_CALLS, NC_FAIL, NC_EMPTY, NC_DRAW, NC_DECLINED, NC_DECLINE_LAYOUT, NC_DECLINE_INPUT, NC_DECLINE_FLOOR, NC_DECLINE_PREEMPT, NC_VERIFIED, NC_MISMATCHED, NC_RACED, NC_SKIPPED,
       NC_T_NATIVE, NC_T_GUEST, NC_T_NATIVE_DRAW, NC_T_GUEST_DRAW, NC_COUNTERS };
static unsigned n63_counter[NC_COUNTERS], n63_mismatch_total;
enum { NT_NATIVE, NT_GUEST, NT_NATIVE_COMPUTE, NT_GUEST_COMPUTE, NT_NATIVE_HLE, NT_GUEST_HLE, NT_SUMS };
static uint64_t n63_ns[NT_SUMS];
#define N63_ADD(i, v) __atomic_fetch_add(&n63_counter[i], (unsigned)(v), __ATOMIC_RELAXED)
#define N63_ADD_NS(i, v) __atomic_fetch_add(&n63_ns[i], (uint64_t)(v), __ATOMIC_RELAXED)
extern void f_00063C00(xctx *);
extern void (*volatile xv_hle_tap)(xctx *, const char *);   /* recomp/kernel/xd3d.c: every D3D HLE entry */

static int n63_mode_value = -1;
static int n63_mode(void)
{
    int mode = n63_mode_value;
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_63C00"); mode = e ? atoi(e) : XV_NATIVE_63C00_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        XK_LOG("[native-63c00] f_00063C00 flare visibility test: %s (floor: %s)\n",
               mode == 2 ? "native" : mode == 1 ? "verify (native dry run vs guest, guest result kept)" : "off",
               n63_crt_native() ? "xv_native_crt_float" : "lifted CRT");
        n63_mode_value = mode;
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_63c00_force(int mode) { n63_mode_value = mode < 0 || mode > 2 ? 0 : mode; }
static int n63_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_63C00_TIME"); on = e && atoi(e) != 0; }
    return on;
}

/* The guest reference run: the hook returns 0 for this context; the tap records its HLE entries. */
static xctx *volatile n63_ref_ctx;
static n63_hle_log *volatile n63_tap_log;
static uint64_t n63_tap_first_ns;
static void n63_tap(xctx *c, const char *name)
{
    if (c != n63_ref_ctx) return;
    if (!n63_tap_first_ns) n63_tap_first_ns = n63_now_ns();
    n63_hle_log *log = n63_tap_log;
    if (!log) return;
    unsigned k = 0;
    while (k < N63_HLE_KINDS && strcmp(name, n63_hle_tab[k].d3d_name)) ++k;
    n63_record(c, k < N63_HLE_KINDS ? k : 0xFFu, log);
}
static void n63_tap_install(void)
{
    static int done;
    if (!done) { done = 1; xv_hle_tap = n63_tap; }
}
/* One reference run at a time (the scene runs on one thread at a time; a second context meanwhile runs unobserved). */
static int n63_claim(xctx *c)
{
    xctx *expected = 0;
    return __atomic_compare_exchange_n((xctx **)&n63_ref_ctx, &expected, c, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static uint64_t n63_guest(xctx *c, n63_hle_log *log, uint64_t *first_hle)   /* the caller holds the claim */
{
    n63_tap_first_ns = 0; n63_tap_log = log;
    uint64_t t0 = n63_now_ns();
    f_00063C00(c);
    uint64_t t1 = n63_now_ns();
    n63_tap_log = 0; __atomic_store_n((xctx **)&n63_ref_ctx, (xctx *)0, __ATOMIC_RELEASE);
    *first_hle = n63_tap_first_ns ? n63_tap_first_ns - t0 : 0;
    return t1 - t0;
}

static void n63_declined(int why)
{
    N63_ADD(NC_DECLINED, 1);
    N63_ADD(why == N63_DECLINE_LAYOUT ? NC_DECLINE_LAYOUT : why == N63_DECLINE_INPUT ? NC_DECLINE_INPUT : why == N63_DECLINE_FLOOR ? NC_DECLINE_FLOOR : NC_DECLINE_PREEMPT, 1);
}
static void n63_count(int exit)
{
    N63_ADD(NC_CALLS, 1);
    N63_ADD(exit == N63_EXIT_F ? NC_FAIL : exit == N63_EXIT_EMPTY ? NC_EMPTY : NC_DRAW, 1);
}
static void n63_report_mismatch(const char *what, uint32_t a, uint32_t b, const n63_plan *p)
{
    if (__atomic_add_fetch(&n63_mismatch_total, 1, __ATOMIC_RELAXED) <= 16)
        XK_LOG("[native-63c00] MISMATCH %s native %08X guest %08X (path %d, E %08X, point %08X)\n", what, a, b, p->path, p->E, p->A1);
}
static int n63_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a); }

/* Inputs the native read, re-read after the guest run: a change means another thread wrote them in between. */
static uint32_t n63_input_hash(xctx *c, const n63_plan *p)
{
    (void)c; uint32_t h = 2166136261u;
#define H32(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    H32(X_M32(p->E + 4u)); H32(X_M32(p->E + 8u)); H32(X_M32(p->E + 0xCu));
    for (unsigned i = 0; i < 3; ++i) { uint32_t w; x_guest_read(&w, p->A1 + 4u * i, 4); H32(w); }
    for (unsigned i = 0; i < 13; ++i) { uint32_t w; x_guest_read(&w, N63_MATRIX + 4u * i, 4); H32(w); }
    for (unsigned i = 0; i < 16; ++i) { uint32_t w; x_guest_read(&w, N63_PROJ + 4u * i, 4); H32(w); }
    static const uint32_t k[] = { N63_EPS, N63_ONE, N63_HALF, N63_LO, N63_HI, N63_CW, N63_RECT, N63_RECT + 4u };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) { uint32_t w; x_guest_read(&w, k[i], 4); H32(w); }
#undef H32
    return h;
}

/* Entry hook of f_00063C00: 1 = handled (guest body skipped). */
int xv_native_63c00(xctx *c)
{
    const int mode = n63_mode();
    if (c == n63_ref_ctx) return 0;                              /* the reference run of verify/timing */
    const int timed = n63_timing();
    if (!mode) {
        if (!timed) return 0;
        n63_tap_install();
        if (!n63_claim(c)) return 0;
        uint64_t first = 0, t = n63_guest(c, 0, &first);
        N63_ADD_NS(NT_GUEST, t); N63_ADD(NC_T_GUEST, 1);
        if (first) { N63_ADD_NS(NT_GUEST_COMPUTE, first); N63_ADD_NS(NT_GUEST_HLE, t - first); N63_ADD(NC_T_GUEST_DRAW, 1); }
        else N63_ADD_NS(NT_GUEST_COMPUTE, t);
        N63_ADD(NC_CALLS, 1);
        return 1;
    }
    n63_plan p;
    if (mode == 2) {
        n63_run_ctl ctl = { 0, 0 };
        const uint64_t t0 = timed ? n63_now_ns() : 0;
        const int why = n63_compute(c, &p);
        if (why) { n63_declined(why); return 0; }
        const int exit = n63_commit(c, &p, &ctl);
        if (timed) {
            const uint64_t t1 = n63_now_ns();
            N63_ADD_NS(NT_NATIVE, t1 - t0); N63_ADD(NC_T_NATIVE, 1);
            if (ctl.hle_start) { N63_ADD_NS(NT_NATIVE_COMPUTE, ctl.hle_start - t0); N63_ADD_NS(NT_NATIVE_HLE, t1 - ctl.hle_start); N63_ADD(NC_T_NATIVE_DRAW, 1); }
            else N63_ADD_NS(NT_NATIVE_COMPUTE, t1 - t0);
        }
        n63_count(exit);
        return 1;
    }
    /* verify: the native runs dry (HLE calls recorded), then the guest body runs on the restored state with its HLE
     * entries observed; everything either can write is compared; the guest's result (and its HLE calls) stand. */
    n63_tap_install();
    if (!n63_claim(c)) { N63_ADD(NC_SKIPPED, 1); return 0; }
    const uint32_t E = c->r[4], lo = E - N63_WINDOW_LO;
    uint8_t stack_before[N63_WINDOW_BYTES], stack_native[N63_WINDOW_BYTES], stack_guest[N63_WINDOW_BYTES];
    x_guest_read_pages(stack_before, lo, N63_WINDOW_BYTES);
    const xctx before = *c;
    const uint64_t t0 = n63_now_ns();
    const int why = n63_compute(c, &p);
    if (why) { __atomic_store_n((xctx **)&n63_ref_ctx, (xctx *)0, __ATOMIC_RELEASE); n63_declined(why); return 0; }
    n63_hle_log native_log = { 0 }, guest_log = { 0 };
    n63_run_ctl ctl = { &native_log, 0 };
    const int exit = n63_commit(c, &p, &ctl);
    const uint64_t t1 = n63_now_ns();
    const uint32_t inputs = n63_input_hash(c, &p);
    const xctx native = *c;
    x_guest_read_pages(stack_native, lo, N63_WINDOW_BYTES);
    x_guest_write_pages(lo, stack_before, N63_WINDOW_BYTES);
    *c = before;
    uint64_t first = 0; const uint64_t tg = n63_guest(c, &guest_log, &first);
    if (timed) {
        N63_ADD_NS(NT_NATIVE, t1 - t0); N63_ADD(NC_T_NATIVE, 1); N63_ADD_NS(NT_GUEST, tg); N63_ADD(NC_T_GUEST, 1);
        if (ctl.hle_start) N63_ADD_NS(NT_NATIVE_COMPUTE, ctl.hle_start - t0); else N63_ADD_NS(NT_NATIVE_COMPUTE, t1 - t0);
        if (first) { N63_ADD_NS(NT_GUEST_COMPUTE, first); N63_ADD_NS(NT_GUEST_HLE, tg - first); N63_ADD(NC_T_GUEST_DRAW, 1); }
        else N63_ADD_NS(NT_GUEST_COMPUTE, tg);
    }
    x_guest_read_pages(stack_guest, lo, N63_WINDOW_BYTES);
    n63_count(exit); N63_ADD(NC_VERIFIED, 1);
    if (n63_input_hash(c, &p) != inputs) { N63_ADD(NC_RACED, 1); return 1; }   /* inputs changed under us: no verdict */
    unsigned bad = 0;
#define N63_CMP(what, a, b) do { if ((a) != (b)) { bad++; n63_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &p); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) N63_CMP(rn[i], native.r[i], c->r[i]);
    N63_CMP("fsp", native.fsp, c->fsp); N63_CMP("fcw", native.fcw, c->fcw); N63_CMP("fsw", native.fsw, c->fsw);
    N63_CMP("df", native.df, c->df); N63_CMP("preempt", native.preempt, c->preempt);
    N63_CMP("f_kind", native.f_kind, c->f_kind); N63_CMP("f_op1", native.f_op1, c->f_op1); N63_CMP("f_op2", native.f_op2, c->f_op2);
    N63_CMP("f_res", native.f_res, c->f_res); N63_CMP("f_bits", native.f_bits, c->f_bits);
    N63_CMP("f_cf_override", native.f_cf_override, c->f_cf_override); N63_CMP("f_of_override", native.f_of_override, c->f_of_override);
    N63_CMP("f_cf", native.f_cf, c->f_cf); N63_CMP("f_of", native.f_of, c->f_of);
    for (unsigned i = 0; i < 8; ++i)
        if (!n63_same_double(native.st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &native.st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[32]; snprintf(w, sizeof w, "st[slot %u] lo", i); bad++; n63_report_mismatch(w, (uint32_t)a, (uint32_t)b, &p);
        }
    for (unsigned i = 0; i < N63_WINDOW_BYTES; i += 4) {
        uint32_t a, b; memcpy(&a, stack_native + i, 4); memcpy(&b, stack_guest + i, 4);
        if (a != b) { bad++; char w[40]; snprintf(w, sizeof w, "stack[E%+d]", (int)(lo + i - E)); n63_report_mismatch(w, a, b, &p); }
    }
    N63_CMP("hle calls", native_log.n, guest_log.n);
    for (unsigned i = 0; i < native_log.n && i < guest_log.n && i < 8; ++i) {
        const n63_hle_rec *a = &native_log.rec[i], *b = &guest_log.rec[i];
        char w[48];
        if (a->kind != b->kind) { snprintf(w, sizeof w, "hle[%u] kind", i); N63_CMP(w, a->kind, b->kind); continue; }
        for (unsigned j = 0; j < 8; ++j) { snprintf(w, sizeof w, "hle[%u] %s", i, rn[j]); N63_CMP(w, a->r[j], b->r[j]); }
        for (unsigned j = 0; j < n63_hle_tab[a->kind].nargs; ++j) { snprintf(w, sizeof w, "hle[%u] arg%u", i, j); N63_CMP(w, a->args[j], b->args[j]); }
        snprintf(w, sizeof w, "hle[%u] fsp", i); N63_CMP(w, a->fsp, b->fsp);
        snprintf(w, sizeof w, "hle[%u] fsw", i); N63_CMP(w, a->fsw, b->fsw);
        snprintf(w, sizeof w, "hle[%u] fcw", i); N63_CMP(w, a->fcw, b->fcw);
        for (unsigned j = 0; j < 8; ++j)
            if (!n63_same_double(a->st[j], b->st[j])) { uint64_t x, y; memcpy(&x, &a->st[j], 8); memcpy(&y, &b->st[j], 8); snprintf(w, sizeof w, "hle[%u] st[slot %u] lo", i, j); N63_CMP(w, (uint32_t)x, (uint32_t)y); }
    }
#undef N63_CMP
    if (bad) N63_ADD(NC_MISMATCHED, 1);
    return 1;
}

#if defined(XV_NATIVE_63C00_COVERAGE)
unsigned xv_native_63c00_declined(void) { return __atomic_load_n(&n63_counter[NC_DECLINED], __ATOMIC_RELAXED); }   /* tests */
#endif
void xv_native_63c00_report(unsigned frames)
{
    unsigned n[NC_COUNTERS]; uint64_t ns[NT_SUMS];
    for (unsigned i = 0; i < NC_COUNTERS; ++i) n[i] = __atomic_exchange_n(&n63_counter[i], 0u, __ATOMIC_RELAXED);
    for (unsigned i = 0; i < NT_SUMS; ++i) ns[i] = __atomic_exchange_n(&n63_ns[i], 0, __ATOMIC_RELAXED);
    if (!n[NC_CALLS] && !n[NC_DECLINED]) return;
    char timing[200] = ""; size_t k = 0;
#define N63_US(sum, cnt) ((cnt) ? (double)(sum) / 1000.0 / (double)(cnt) : 0.0)
    if (n[NC_T_NATIVE])
        k += (size_t)snprintf(timing + k, sizeof timing - k, "; native us/call %.2f (compute %.2f, hle %.2f/draw)", N63_US(ns[NT_NATIVE], n[NC_T_NATIVE]),
                              N63_US(ns[NT_NATIVE_COMPUTE], n[NC_T_NATIVE]), N63_US(ns[NT_NATIVE_HLE], n[NC_T_NATIVE_DRAW]));
    if (n[NC_T_GUEST] && k < sizeof timing)
        snprintf(timing + k, sizeof timing - k, "; guest us/call %.2f (compute %.2f, hle %.2f/draw)", N63_US(ns[NT_GUEST], n[NC_T_GUEST]),
                 N63_US(ns[NT_GUEST_COMPUTE], n[NC_T_GUEST]), N63_US(ns[NT_GUEST_HLE], n[NC_T_GUEST_DRAW]));
#undef N63_US
    XK_LOG("[native-63c00] %u frames: calls %u (projection-fail %u, empty %u, drawn %u) declined %u (layout %u, inputs %u, floor %u, preempt %u); verified %u mismatched %u raced %u skipped %u (total mismatches %u)%s\n",
           frames, n[NC_CALLS], n[NC_FAIL], n[NC_EMPTY], n[NC_DRAW], n[NC_DECLINED], n[NC_DECLINE_LAYOUT], n[NC_DECLINE_INPUT], n[NC_DECLINE_FLOOR], n[NC_DECLINE_PREEMPT], n[NC_VERIFIED], n[NC_MISMATCHED], n[NC_RACED], n[NC_SKIPPED],
           __atomic_load_n(&n63_mismatch_total, __ATOMIC_RELAXED), timing);
}
