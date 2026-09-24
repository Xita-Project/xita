/* xk_native_aim_blend.c - native Halo CE (Xbox 3925) 2-D animation overlay blend: f_000A39B0 and its callees.
 *
 * f_00090770 (object type postprocess dispatch, called per object from f_0008DDF0 in the per-object update loop
 * f_0008FB70 of the game tick) spends ~95 % of its time in f_0003ECC0 (the unit postprocess: aiming / looking), and
 * that spends ~94 % in f_000A39B0: the bilinear "aim screen" blend. For every node of the animation's skeleton it
 * decodes four keyframe rotations (yaw x pitch grid neighbours; int16 quaternions scaled by 1/32767, f_000A2C30),
 * blends them with three lerps (f_000B0E70, shortest arc) + normalizations (f_000B4320), multiplies the node's
 * rotation by the result (f_000B0EF0), and adds the bilinearly blended keyframe translation to the node position.
 * The setup converts yaw/pitch into frame indices and fractions with the MSVC CRT: _ftol-style truncation
 * f_0001D150 and fmod through the _CIfmod dispatcher f_00180ADA (-> f_000220FF classify, jmp to f_00180AE4 fprem or
 * f_0002219C zero, f_00180C8A epilogue); f_000A5080 turns frame indices into keyframe data pointers.
 *
 * The native replaces f_000A39B0 and all of those callees for the uncompressed-animation path (the compressed path,
 * f_000A3750 / f_000A2EE0, and the early exits stay on the guest body: the hook returns 0 before doing anything).
 * Exact by construction:
 *  - the guest stack below esp: the native runs on a host shadow of [esp-0x3D8, esp+0x10) and performs every stack
 *    store the guest performs, in order (locals, pushes, return addresses, callee frames including the 16-byte
 *    aligned f_0001D150 frame and the 0x2D0-byte f_00180ADA frame), then writes the window back: every dead stack
 *    byte ends as the guest leaves it, bytes the guest does not write keep their values;
 *  - x87: the slots below the entry top are depth-indexed locals updated exactly like the emulator's st[] (pushes,
 *    fxch swaps, dead slots included); fsw from every compare/fxam/fprem with the TOP field the emulator stores; fcw
 *    set to 0x133F inside the fmod and restored; float math in doubles with the guest's operand order, stores
 *    rounded to float, the unit compiled -ffp-contract=off;
 *  - registers the guest leaves (eax/ecx/edx last assignments, ebx/ebp/esi popped from the stack, esp += 0x10 for
 *    ret 0Ch), the lazy-flag record of the last flag-setting instruction and the carry/overflow cells of the last
 *    shift / inc / imul / adc that wrote them;
 *  - guest memory accesses through the same translation as the shards (the thread's page table, X_IMG rules of the
 *    build), integer loads single-translation like X_M16/X_M32, float loads/stores page-split like
 *    x87_load_f32/x87_store_f32; keyframe data and nodes are read and written at the same point in the sequence as
 *    the guest, so overlapping keyframe/node data behaves the same;
 *  - the back-edge budget (c->preempt) drops by exactly the guest's back-edge count, xv_preempt() is called the
 *    same number of times, at the end of the call (scheduling point only).
 * Declined (guest body runs, nothing touched): compressed animations, the two early exits, fcw with the precision
 * exception unmasked, fmod operands the CRT dispatches elsewhere (NaN/inf), object-job threads, an unaligned esp,
 * any overlap of the node array with the stack window / the data the loop re-reads (animation entry fields,
 * the 0.0/1.0/(1/32767) constants) or of the keyframe data with the stack window, and an odd animation header or an
 * animation entry / node array / keyframe pointer that is not 4-aligned (a single-translation load straddling a page
 * reads the host-adjacent page, which could be a stack page the native holds in its shadow).
 * Not reproduced: NaN payload bits where the host compiler may commute an addition or multiplication (the guest
 * body built -O0 and -O2 already disagrees; animation data has no NaNs).
 *
 * XV_NATIVE_AIM_BLEND build flag (hook tools/patch_native_aim_blend_hooks.py at the entry of f_000A39B0); env
 * XV_NATIVE_AIM_BLEND: 0 off (default XV_NATIVE_AIM_BLEND_DEFAULT), 1 verify (native then guest on the same state,
 * compare everything, keep the guest result), 2 native. XV_NATIVE_AIM_BLEND_TIME=1 times the calls (both paths in
 * verify mode). Counters: [native-aim-blend] every 60 frames. */
#include "xk.h"
#include "../xv_x86rt.h"
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS)
#include "xk_object_jobs.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef XV_NATIVE_AIM_BLEND_DEFAULT
#define XV_NATIVE_AIM_BLEND_DEFAULT 0
#endif

enum {
    AB_BELOW = 0x3D8,            /* shadow window: [E - AB_BELOW, E + 0x10), E = esp at entry (-> return address) */
    AB_WIN = AB_BELOW + 0x10,
    AB_Q = AB_BELOW - 0xF4,      /* shadow offset of Q = E - 0xF4: esp inside the body after push esi */
    AB_P = AB_Q + 4,             /* P = esp after push ebp (before push esi) */
    AB_B = AB_Q - 8,             /* B = ebp inside f_00180ADA / f_0001D150 */
    AB_ZERO = 0x1F0A68u, AB_ONE = 0x1F0A78u, AB_DONE = 0x1F0A70u, AB_QSCALE = 0x1F0AE8u,
    AB_MODE_BYTE = 0x204B6Fu, AB_CRT_FAST = 0x1F2EB0u, AB_CRT_ERR = 0x270678u, AB_XLAT = 0x1F2E34u, AB_FMOD = 0x25F628u,
    AB_FPREM = 0x180AE4u, AB_FZERO = 0x2219Cu,
};

/* Guest memory exactly as the generated code addresses it (see xk_native_visibility.c). */
typedef struct { uint8_t *ram; const uint32_t *pt; uint8_t *img; } ab_mem;
#define AB_PTR(a) (m->ram + m->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define AB_IPTR(a) AB_PTR(a)
#else
#define AB_IPTR(a) (m->img + (uint32_t)(a))
#endif
static inline uint8_t ab_m8(const ab_mem *m, uint32_t a) { return *AB_PTR(a); }
static inline uint16_t ab_m16(const ab_mem *m, uint32_t a) { uint16_t v; memcpy(&v, AB_PTR(a), 2); return v; }
static inline uint32_t ab_m32(const ab_mem *m, uint32_t a) { uint32_t v; memcpy(&v, AB_PTR(a), 4); return v; }
static inline uint8_t ab_i8(const ab_mem *m, uint32_t a) { return *AB_IPTR(a); }
static inline uint32_t ab_i32(const ab_mem *m, uint32_t a) { uint32_t v; memcpy(&v, AB_IPTR(a), 4); return v; }
static inline double ab_f32(const ab_mem *m, uint32_t a)
{
    float v;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(&v, AB_PTR(a), 4); else x_guest_read_pages(&v, a, 4);
    return (double)v;
}
static inline double ab_f64(const ab_mem *m, uint32_t a)
{
    double v;
    if ((a & 0xFFFu) <= 0xFF8u) memcpy(&v, AB_PTR(a), 8); else x_guest_read_pages(&v, a, 8);
    return v;
}
static inline void ab_wf32(const ab_mem *m, uint32_t a, double d)
{
    float v = (float)d;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(AB_PTR(a), &v, 4); else x_guest_write_pages(a, &v, 4);
}

/* The stack shadow: offsets into S (constant for everything but the aligned f_0001D150 frame). */
static inline uint8_t s8(const uint8_t *S, int o) { return S[o]; }
static inline uint16_t s16(const uint8_t *S, int o) { uint16_t v; memcpy(&v, S + o, 2); return v; }
static inline uint32_t s32(const uint8_t *S, int o) { uint32_t v; memcpy(&v, S + o, 4); return v; }
static inline double sf(const uint8_t *S, int o) { float v; memcpy(&v, S + o, 4); return (double)v; }
static inline void w8(uint8_t *S, int o, uint8_t v) { S[o] = v; }
static inline void w16(uint8_t *S, int o, uint16_t v) { memcpy(S + o, &v, 2); }
static inline void w32(uint8_t *S, int o, uint32_t v) { memcpy(S + o, &v, 4); }
static inline void wf(uint8_t *S, int o, double d) { float v = (float)d; memcpy(S + o, &v, 4); }
static inline void wd(uint8_t *S, int o, double d) { memcpy(S + o, &d, 8); }

/* Lazy flags (the xctx fields), evaluated like xv_x86rt.h. */
typedef struct { uint32_t kind, op1, op2, res, bits, cfo, cf, ofo, of; } ab_fl;
#define FLG(K, A, B, R, N) do { fl->kind = (K); fl->op1 = (uint32_t)(A); fl->op2 = (uint32_t)(B); fl->res = (uint32_t)(R); \
                                fl->bits = (N); fl->cfo = 0; fl->ofo = 0; } while (0)
static inline uint32_t fl_mask(const ab_fl *f) { return f->bits == 32 ? 0xFFFFFFFFu : ((1u << f->bits) - 1u); }
static inline uint32_t fl_msb(const ab_fl *f, uint32_t v) { return (v >> (f->bits - 1)) & 1u; }
static inline uint32_t fl_c(const ab_fl *f)
{
    if (f->kind == XK_EXPLICIT) return f->res & 1u;
    if (f->cfo) return f->cf;
    uint32_t m = fl_mask(f), a = f->op1 & m, b = f->op2 & m, r = f->res & m;
    switch (f->kind) {
    case XK_ADD: return r < a;
    case XK_ADC: return f->cf ? (r <= a) : (r < a);
    case XK_SUB: return a < b;
    case XK_SBB: return f->cf ? (a <= b) : (a < b);
    default: return 0;
    }
}
static inline uint32_t fl_o(const ab_fl *f)
{
    if (f->kind == XK_EXPLICIT) return (f->res >> 11) & 1u;
    if (f->ofo) return f->of;
    uint32_t a = f->op1, b = f->op2, r = f->res;
    switch (f->kind) {
    case XK_ADD: case XK_ADC: return fl_msb(f, (a ^ r) & (b ^ r));
    case XK_SUB: case XK_SBB: return fl_msb(f, (a ^ b) & (a ^ r));
    default: return 0;
    }
}
static inline uint32_t ab_parity_even(uint32_t v) { v &= 0xFFu; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return !(v & 1u); }
static inline uint32_t fl_eflags(const ab_fl *f, uint32_t df)
{
    uint32_t z, s, p;
    if (f->kind == XK_EXPLICIT) { z = (f->res >> 6) & 1u; s = (f->res >> 7) & 1u; p = (f->res >> 2) & 1u; }
    else { z = (f->res & fl_mask(f)) == 0; s = fl_msb(f, f->res); p = ab_parity_even(f->res); }
    return 0x202u | fl_c(f) | (p << 2) | (z << 6) | (s << 7) | (fl_o(f) << 11) | (df << 10);
}
/* inc/dec: carry kept (the emitted code: cf_ = XF_C; override = 1), the rest not materialized */
#define FL_INCDEC() do { uint32_t cf_ = fl_c(fl); fl->cfo = 1; fl->cf = cf_; } while (0)
static inline uint32_t fl_imul32(ab_fl *fl, uint32_t a, uint32_t b)
{
    int64_t p = (int64_t)(int32_t)a * (int32_t)b; uint32_t r = (uint32_t)p;
    FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = fl->ofo = 1; fl->cf = fl->of = (p != (int64_t)(int32_t)r); return r;
}
static inline uint8_t fl_shl8_1(ab_fl *fl, uint8_t v)
{
    uint8_t r = (uint8_t)((uint32_t)v << 1); FLG(XK_LOGIC, 0, 0, r, 8); fl->cfo = 1; fl->cf = ((uint32_t)v >> 7) & 1u;
    fl->ofo = 1; fl->of = ((r >> 7) & 1u) ^ fl->cf; return r;
}
static inline uint8_t fl_sar8_1(ab_fl *fl, uint8_t v)
{
    int32_t sv = (int8_t)v; uint8_t r = (uint8_t)(sv >> 1); FLG(XK_LOGIC, 0, 0, r, 8); fl->cfo = 1; fl->cf = (uint32_t)sv & 1u;
    fl->ofo = 1; fl->of = 0; return r;
}
static inline uint8_t fl_rol8_1(ab_fl *fl, uint8_t v)
{
    uint8_t r = (uint8_t)(((uint32_t)v << 1) | ((uint32_t)v >> 7));
    fl->cfo = 1; fl->cf = r & 1u; fl->ofo = 1; fl->of = ((r >> 7) & 1u) ^ (r & 1u); return r;
}
static inline uint32_t fl_shl32(ab_fl *fl, uint32_t v, unsigned n)   /* 1 <= n <= 31 */
{
    uint32_t r = v << n; FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = 1; fl->cf = (v >> (32 - n)) & 1u;
    fl->ofo = 1; fl->of = (r >> 31) ^ fl->cf; return r;
}
static inline uint32_t fl_shr32_1(ab_fl *fl, uint32_t v)
{
    uint32_t r = v >> 1; FLG(XK_LOGIC, 0, 0, r, 32); fl->cfo = 1; fl->cf = v & 1u; fl->ofo = 1; fl->of = v >> 31; return r;
}
static inline uint16_t fl_sar16_5(ab_fl *fl, uint16_t v)
{
    int32_t sv = (int16_t)v; uint16_t r = (uint16_t)(sv >> 5); FLG(XK_LOGIC, 0, 0, r, 16); fl->cfo = 1; fl->cf = (uint32_t)(sv >> 4) & 1u;
    fl->ofo = 1; fl->of = 0; return r;
}

/* x87 condition codes (x87_compare / x87_fxam). */
static inline uint16_t ab_cc(double a, double b)
{
    return (isnan(a) || isnan(b)) ? 0x4500 : (a < b) ? 0x0100 : (a == b) ? 0x4000 : 0;
}
static inline uint16_t ab_fxam_cc(double v)
{
    uint16_t cc = isnan(v) ? 0x0100 : isinf(v) ? 0x0500 : v == 0.0 ? 0x4000 : 0x0400;
    return (uint16_t)(cc | (signbit(v) ? 0x0200 : 0));
}
static inline double ab_round(uint16_t fcw, double v)
{
    switch ((fcw >> 10) & 3u) { case 0: return nearbyint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); }
}

/* One call's machine state. X[d] is the x87 slot d below the entry top (st[(F - d) & 7]). */
typedef struct {
    double X[8];
    uint32_t eax, ecx, edx, ebx, esi, ebp;
    uint16_t fsw, fcw;
    unsigned F;
    uint32_t be;                   /* back-edges (X_PREEMPT sites) taken */
    uint32_t Qa;                   /* guest address of Q */
    double zero, one, qscale;
} ab_st;
#define CMP(A, depth) (st->fsw = (uint16_t)((st->fsw & ~0x4700u) | ab_cc((A), st->zero) | (((st->F - (depth)) & 7u) << 11)))
#define FNSTSW_AX() (st->eax = (st->eax & 0xFFFF0000u) | st->fsw)
#define SWAP12() do { double t_ = st->X[2]; st->X[2] = st->X[1]; st->X[1] = t_; } while (0)
#define QA(k) (st->Qa + (uint32_t)(k))

/* f_0001D150 (_ftol-style: the truncated int64 of st0 in edx:eax, st0 popped), called at x87 depth 1, esp = Q. */
static inline __attribute__((always_inline)) void ab_ftol(ab_st *st, ab_fl *fl, uint8_t *S, uint32_t ret)
{
    w32(S, AB_Q - 4, ret);
    w32(S, AB_Q - 8, st->ebp);                                 /* push ebp; ebp = Q - 8 */
    const uint32_t esp1 = (st->Qa - 0x28u) & ~0xFu;            /* sub esp,20h; and esp,-16 */
    const int o = AB_Q + (int)(esp1 - st->Qa);
    st->X[2] = st->X[1];                                       /* fld st(0) */
    wf(S, o + 0x18, st->X[2]);                                 /* fst dword [esp+18h] */
    {                                                          /* fistp qword [esp+10h] */
        double v = ab_round(st->fcw, st->X[2]);
        int64_t r = (v >= -9.2233720368547758e18 && v < 9.2233720368547758e18) ? (int64_t)v : (int64_t)0x8000000000000000ull;
        memcpy(S + o + 0x10, &r, 8);
    }
    { int64_t iv; memcpy(&iv, S + o + 0x10, 8); st->X[2] = (double)iv; }   /* fild qword [esp+10h] */
    st->edx = s32(S, o + 0x18); st->eax = s32(S, o + 0x10);
    FLG(XK_LOGIC, 0, 0, st->eax, 32);
    if (st->eax == 0) {                                        /* L_1D1AF */
        st->edx = s32(S, o + 0x14);
        FLG(XK_LOGIC, 0, 0, st->edx & 0x7FFFFFFFu, 32);
        if (!(st->edx & 0x7FFFFFFFu)) {                        /* L_1D1BB: two fstp dword [esp+18h] */
            wf(S, o + 0x18, st->X[2]); wf(S, o + 0x18, st->X[1]);
            st->ebp = s32(S, AB_Q - 8);                        /* leave; ret */
            return;
        }
        st->be++;                                              /* jne 1D173: back-edge */
    }
    st->X[1] = st->X[1] - st->X[2];                            /* L_1D173: fsubp */
    FLG(XK_LOGIC, 0, 0, st->edx, 32);
    wf(S, o, st->X[1]);                                        /* fstp dword [esp] (both paths) */
    if (!(st->edx >> 31)) {                                    /* L_1D197: round-up correction for x >= 0 */
        st->ecx = s32(S, o);
        { uint32_t a = st->ecx, b = 0x7FFFFFFFu, r = a + b; FLG(XK_ADD, a, b, r, 32); st->ecx = r; }
        { uint32_t a = st->eax, cf = fl_c(fl), r = a - cf; FLG(XK_SBB, a, 0, r, 32); fl->cf = cf; st->eax = r; }
        st->edx = s32(S, o + 0x14);
        { uint32_t a = st->edx, cf = fl_c(fl), r = a - cf; FLG(XK_SBB, a, 0, r, 32); fl->cf = cf; st->edx = r; }
    } else {                                                   /* L_1D179: x < 0 */
        st->ecx = s32(S, o) ^ 0x80000000u;
        { uint32_t a = st->ecx, b = 0x7FFFFFFFu, r = a + b; FLG(XK_ADD, a, b, r, 32); st->ecx = r; }
        { uint32_t a = st->eax, cf = fl_c(fl), r = a + cf; FLG(XK_ADC, a, 0, r, 32); fl->cf = cf; st->eax = r; }
        st->edx = s32(S, o + 0x14);
        { uint32_t a = st->edx, cf = fl_c(fl), r = a + cf; FLG(XK_ADC, a, 0, r, 32); fl->cf = cf; st->edx = r; }
    }
    st->ebp = s32(S, AB_Q - 8);                                /* leave; ret */
}

/* f_00180ADA (_CIfmod: fmod(st1, st0), popped once) at x87 depth 2, esp = Q. 0 = the CRT would take a path the
 * native does not reproduce (NaN/inf operands, error handling): the caller declines before anything is written. */
static inline __attribute__((always_inline)) int ab_fmod(ab_st *st, ab_fl *fl, uint8_t *S, const ab_mem *m, uint32_t df, uint32_t ret)
{
    w32(S, AB_Q - 4, ret);
    st->edx = AB_FMOD;
    w32(S, AB_B, st->ebp);                                     /* push ebp; mov ebp,esp; add esp,-2D0h */
    w32(S, AB_B - 0x2D4, st->ebx);                             /* push ebx */
    w16(S, AB_B - 0xA4, st->fcw);                              /* fnstcw [ebp-0A4h] */
    { uint32_t a = ab_i32(m, AB_CRT_FAST); FLG(XK_SUB, a, 0, a, 32);
      if (!a) { SWAP12(); wd(S, AB_B - 0x86, st->X[2]); SWAP12(); wd(S, AB_B - 0x7E, st->X[2]); st->be++; } }
    /* f_000220FF (_trandisp2) */
    w32(S, AB_B - 0x2D8, 0x180BE0u);
    { uint8_t a = ab_m8(m, st->edx + 0xEu); FLG(XK_SUB, a, 5, (uint8_t)(a - 5u), 8);
      if (a == 5) {
          uint16_t bx = s16(S, AB_B - 0xA4); uint8_t bh = (uint8_t)((bx >> 8) | 2u), r = bh & 0xFEu;
          FLG(XK_LOGIC, bh, 0xFE, r, 8);
          st->ebx = (st->ebx & 0xFFFF0000u) | ((uint32_t)r << 8) | 0x3Fu;
      } else st->ebx = (st->ebx & 0xFFFF0000u) | 0x133Fu; }
    w16(S, AB_B - 0xA2, (uint16_t)st->ebx);
    st->fcw = s16(S, AB_B - 0xA2);
    st->ebx = AB_XLAT;
    st->fsw = (uint16_t)((st->fsw & ~0x4700u) | ab_fxam_cc(st->X[2]));
    w32(S, AB_B - 0x94, st->edx);
    w16(S, AB_B - 0xA0, st->fsw);
    w8(S, AB_B - 0x90, 0);
    SWAP12();
    st->ecx = (st->ecx & ~0xFFu) | s8(S, AB_B - 0x9F);
    st->fsw = (uint16_t)((st->fsw & ~0x4700u) | ab_fxam_cc(st->X[2]));
    w16(S, AB_B - 0xA0, st->fsw);
    SWAP12();
    uint8_t ch = s8(S, AB_B - 0x9F), cl = (uint8_t)st->ecx, al, ah;
    ch = fl_shl8_1(fl, ch); ch = fl_sar8_1(fl, ch); ch = fl_rol8_1(fl, ch);
    al = ab_m8(m, st->ebx + (ch & 0xFu));
    ah = al;
    cl = fl_shl8_1(fl, cl); cl = fl_sar8_1(fl, cl); cl = fl_rol8_1(fl, cl);
    al = ab_m8(m, st->ebx + (cl & 0xFu));
    ah = fl_shl8_1(fl, ah); ah = fl_shl8_1(fl, ah);
    al |= ah;
    st->eax = (uint32_t)(int32_t)(int8_t)al;
    st->ecx = ((st->ecx & 0xFFFF0000u) | ((uint32_t)ch << 8) | cl) & 0x404u;
    st->ebx = st->edx + st->eax;
    { uint32_t a = st->ebx, r = a + 0x10u; FLG(XK_ADD, a, 0x10, r, 32); st->ebx = r; }
    uint32_t target = ab_m32(m, st->ebx);                      /* jmp dword ptr [ebx] */
    if (target == AB_FPREM) {                                  /* f_00180AE4: fxch; fprem; fnstsw ax; sahf; jp; fstp st(1) */
        SWAP12();
        st->X[2] = fmod(st->X[2], st->X[1]); st->fsw &= (uint16_t)~0x0400u;
        FNSTSW_AX();
        uint32_t v = (fl_eflags(fl, df) & ~0xFFu) | ((uint32_t)st->fsw >> 8);
        fl->kind = XK_EXPLICIT; fl->res = v; fl->bits = 32; fl->cfo = 0; fl->ofo = 0;
        st->X[1] = st->X[2];                                   /* C2 was just cleared: the loop never repeats */
    } else if (target == AB_FZERO) {                           /* f_0002219C: fstp st(0) x2; fldz */
        st->X[1] = 0.0;
    } else return 0;
    { uint8_t b = s8(S, AB_B - 0x2C8); w8(S, AB_B - 0x2C8, (uint8_t)(b | 3u)); }   /* or byte [ebp-2C8h],3 */
    /* f_00180C8A */
    w32(S, AB_B - 0x2D8, 0x180BECu);
    { uint32_t a = ab_i32(m, AB_CRT_ERR); FLG(XK_SUB, a, 0, a, 32);
      if (!a) {
          wd(S, AB_B - 0x2D0, st->X[1]);                       /* fst qword [ebp-2D0h] */
          uint8_t b = s8(S, AB_B - 0x90);
          st->eax = (st->eax & ~0xFFu) | b; FLG(XK_LOGIC, b, b, b, 8);
          if (b) return 0;                                     /* error record set by a handler: matherr paths */
          uint16_t ax = s16(S, AB_B - 0xA4), r = ax & 0x20u;
          FLG(XK_LOGIC, ax, 0x20, r, 16); st->eax = (st->eax & 0xFFFF0000u) | r;
          if (!r) {
              uint16_t sw = st->fsw; r = sw & 0x20u;
              FLG(XK_LOGIC, sw, 0x20, r, 16); st->eax = (st->eax & 0xFFFF0000u) | r;
              if (r) return 0;                                 /* unmasked inexact with PE set: _87except */
          }
      } }
    st->fcw = s16(S, AB_B - 0xA4);                             /* fldcw [ebp-0A4h]; ret */
    st->ebx = s32(S, AB_B - 0x2D4);                            /* pop ebx; leave; ret */
    st->ebp = s32(S, AB_B);
    return 1;
}

/* f_000A5080 (uncompressed: keyframe base + stride * frame), esp = Q, the frame index pushed at Q - 4. */
static inline __attribute__((always_inline)) void ab_frame_ptr(ab_st *st, ab_fl *fl, uint8_t *S, const ab_mem *m, uint32_t edi, uint32_t arg, uint32_t ret)
{
    w32(S, AB_Q - 4, arg); w32(S, AB_Q - 8, ret);
    st->ecx = edi;
    uint8_t al = ab_m8(m, edi + 0x3Au);
    st->eax = (st->eax & ~0xFFu) | al;
    st->edx = (st->edx & ~0xFFu) | 1u;
    FLG(XK_LOGIC, 0, 0, al & 1u, 8);
    if (al & 1u) {                                             /* the caller checked [204B6F] == 0 */
        al = ab_i8(m, AB_MODE_BYTE); st->eax = (st->eax & ~0xFFu) | al; FLG(XK_LOGIC, 0, 0, al, 8);
    }
    st->edx &= ~0xFFu; FLG(XK_LOGIC, 0, 0, 0, 8);              /* L_A5092: xor dl,dl; test dl,dl */
    st->eax = ab_m32(m, edi + 0xACu);
    st->ecx = (uint32_t)(int32_t)(int16_t)ab_m16(m, edi + 0x24u);
    st->edx = (uint32_t)(int32_t)(int16_t)s16(S, AB_Q - 4);
    st->ecx = fl_imul32(fl, st->ecx, st->edx);
    st->eax += st->ecx;
}

/* f_000A2C30: four int16 -> float quaternion components (* [1F0AE8]), esp = Q. */
static inline __attribute__((always_inline)) void ab_quat_decode(ab_st *st, uint8_t *S, const ab_mem *m, int out, uint32_t ret)
{
    w32(S, AB_Q - 4, ret); w32(S, AB_Q - 8, st->ecx);
    const uint32_t p = st->ecx;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t v = (uint32_t)(int32_t)(int16_t)ab_m16(m, p + 2u * i);
        if (i < 3) st->edx = v; else st->ecx = v;
        w32(S, AB_Q - 8, v);
        st->X[1] = (double)(int32_t)s32(S, AB_Q - 8) * st->qscale;
        wf(S, out + 4 * (int)i, st->X[1]);
    }
    st->ecx = s32(S, AB_Q - 8);                                /* pop ecx */
}

/* f_000B0E70: out = (1 - t) * qb + (+-t) * qa (sign by the dot product), esp = Q, t pushed at Q - 4. */
static inline __attribute__((always_inline)) void ab_lerp(ab_st *st, ab_fl *fl, uint8_t *S, uint32_t t_bits, int qa, int qb, int out, uint32_t ret)
{
    w32(S, AB_Q - 4, t_bits); w32(S, AB_Q - 8, ret);
    double *X = st->X;
    X[1] = sf(S, AB_Q - 4);
    X[2] = st->one; X[2] = X[2] - X[1];
    X[3] = sf(S, qa + 8); X[3] = X[3] * sf(S, qb + 8);
    X[4] = sf(S, qa + 4); X[4] = X[4] * sf(S, qb + 4);
    X[3] = X[3] + X[4];
    X[4] = sf(S, qb); X[4] = X[4] * sf(S, qa);
    X[3] = X[3] + X[4];
    X[4] = sf(S, qa + 12); X[4] = X[4] * sf(S, qb + 12);
    X[3] = X[3] + X[4];
    CMP(X[3], 3);
    FNSTSW_AX();
    uint32_t r = ((uint32_t)st->fsw >> 8) & 5u; FLG(XK_LOGIC, 0, 0, r, 8);
    if (!ab_parity_even(r)) { SWAP12(); X[2] = -X[2]; SWAP12(); }
    X[3] = X[2]; X[3] = X[3] * sf(S, qb);
    X[4] = X[1]; X[4] = X[4] * sf(S, qa);
    X[3] = X[3] + X[4]; wf(S, out, X[3]);
    X[3] = X[1]; X[3] = X[3] * sf(S, qa + 4);
    X[4] = X[2]; X[4] = X[4] * sf(S, qb + 4);
    X[3] = X[3] + X[4]; wf(S, out + 4, X[3]);
    X[3] = X[1]; X[3] = X[3] * sf(S, qa + 8);
    X[4] = X[2]; X[4] = X[4] * sf(S, qb + 8);
    X[3] = X[3] + X[4]; wf(S, out + 8, X[3]);
    SWAP12(); X[2] = X[2] * sf(S, qa + 12); SWAP12(); X[2] = X[2] * sf(S, qb + 12);
    X[1] = X[1] + X[2]; wf(S, out + 12, X[1]);
}

/* f_000B4320: normalize the quaternion at q (or (0,0,0,1) when |q|^2 <= 0 / NaN), esp = Q. */
static inline __attribute__((always_inline)) void ab_normalize(ab_st *st, ab_fl *fl, uint8_t *S, int q, uint32_t ret)
{
    w32(S, AB_Q - 4, ret);
    double *X = st->X;
    X[1] = sf(S, q + 12); X[2] = sf(S, q + 8); X[3] = sf(S, q + 4); X[4] = sf(S, q);
    X[5] = X[4]; X[5] = X[5] * X[4];
    X[6] = X[3]; X[6] = X[6] * X[3];
    X[5] = X[5] + X[6];
    X[6] = X[2]; X[6] = X[6] * X[2];
    X[5] = X[5] + X[6];
    X[6] = X[1]; X[6] = X[6] * X[1];
    X[5] = X[5] + X[6];
    X[1] = X[5];                                               /* fstp st(4); fstp st(0) x3 */
    CMP(X[1], 1);
    FNSTSW_AX();
    uint32_t r = ((uint32_t)st->fsw >> 8) & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8);
    if (r) {                                                   /* L_B437B */
        st->eax = 0;
        w32(S, q, 0); w32(S, q + 4, 0); w32(S, q + 8, 0); w32(S, q + 12, 0x3F800000u);
        return;
    }
    X[1] = sqrt(X[1]);
    X[1] = st->one / X[1];
    X[2] = X[1]; X[2] = X[2] * sf(S, q); wf(S, q, X[2]);
    X[2] = X[1]; X[2] = X[2] * sf(S, q + 4); wf(S, q + 4, X[2]);
    X[2] = X[1]; X[2] = X[2] * sf(S, q + 8); wf(S, q + 8, X[2]);
    X[1] = X[1] * sf(S, q + 12); wf(S, q + 12, X[1]);
}

/* f_000B0EF0 with eax == edx == node (a) and ecx = the blended quaternion b (stack): node.q = a * b, esp = Q. */
static inline __attribute__((always_inline)) void ab_quat_apply(ab_st *st, ab_fl *fl, uint8_t *S, const ab_mem *m, int b, uint32_t node, uint32_t ret)
{
    w32(S, AB_Q - 4, ret);
    FLG(XK_SUB, st->ecx, st->edx, st->ecx - st->edx, 32);      /* cmp ecx,edx (never equal: b is on the stack) */
    w32(S, AB_Q - 0x18, st->esi);                              /* sub esp,10h; push esi */
    FLG(XK_SUB, st->eax, st->edx, st->eax - st->edx, 32);      /* cmp eax,edx (equal) */
    st->esi = ab_m32(m, node); w32(S, AB_Q - 0x14, st->esi);
    st->esi = ab_m32(m, node + 4); w32(S, AB_Q - 0x10, st->esi);
    st->esi = ab_m32(m, node + 8);
    st->eax = ab_m32(m, node + 12);
    w32(S, AB_Q - 0x8, st->eax);
    w32(S, AB_Q - 0xC, st->esi);
    st->eax = st->Qa - 0x14u;
    const int a = AB_Q - 0x14;
    double *X = st->X;
    X[1] = sf(S, a + 12);
    st->esi = s32(S, AB_Q - 0x18);                             /* pop esi */
    X[1] = X[1] * sf(S, b);
    X[2] = sf(S, a + 8); X[2] = X[2] * sf(S, b + 4); X[1] = X[1] + X[2];
    X[2] = sf(S, b + 12); X[2] = X[2] * sf(S, a); X[1] = X[1] + X[2];
    X[2] = sf(S, a + 4); X[2] = X[2] * sf(S, b + 8); X[1] = X[1] - X[2];
    ab_wf32(m, node, X[1]);
    X[1] = sf(S, b + 8); X[1] = X[1] * sf(S, a);
    X[2] = sf(S, a + 12); X[2] = X[2] * sf(S, b + 4); X[1] = X[1] + X[2];
    X[2] = sf(S, b + 12); X[2] = X[2] * sf(S, a + 4); X[1] = X[1] + X[2];
    X[2] = sf(S, b); X[2] = X[2] * sf(S, a + 8); X[1] = X[1] - X[2];
    ab_wf32(m, node + 4, X[1]);
    X[1] = sf(S, a + 12); X[1] = X[1] * sf(S, b + 8);
    X[2] = sf(S, a + 4); X[2] = X[2] * sf(S, b); X[1] = X[1] + X[2];
    X[2] = sf(S, b + 12); X[2] = X[2] * sf(S, a + 8); X[1] = X[1] + X[2];
    X[2] = sf(S, a); X[2] = X[2] * sf(S, b + 4); X[1] = X[1] - X[2];
    ab_wf32(m, node + 8, X[1]);
    X[1] = sf(S, a + 12); X[1] = X[1] * sf(S, b + 12);
    X[2] = sf(S, b); X[2] = X[2] * sf(S, a); X[1] = X[1] - X[2];
    X[2] = sf(S, a + 4); X[2] = X[2] * sf(S, b + 4); X[1] = X[1] - X[2];
    X[2] = sf(S, b + 8); X[2] = X[2] * sf(S, a + 8); X[1] = X[1] - X[2];
    ab_wf32(m, node + 12, X[1]);
}

static inline int ab_overlap(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen)
{
    return alen && blen && (uint32_t)(a - b) < blen ? 1 : alen && blen && (uint32_t)(b - a) < alen;
}

typedef struct {                   /* one call's counters */
    unsigned nodes, rotated, translated, backedges;
} ab_info;

enum { AB_DECLINE_COMPRESSED, AB_DECLINE_EXIT, AB_DECLINE_FPU, AB_DECLINE_ALIAS, AB_DECLINE_ALIGN, AB_DECLINE_OTHER, AB_DECLINES };

/* The whole call. Returns 1 when it ran (state committed), 0 when declined (nothing written: *why set). */
static int ab_run(xctx *c, ab_info *info, unsigned *why)
{
    ab_mem mm = { g_xram, X_PT, X_IMG_BASE }; const ab_mem *m = &mm;
    const uint32_t E = c->r[4], A = c->r[0], edi = c->r[7];
    memset(info, 0, sizeof *info);
    if (E & 3u) { *why = AB_DECLINE_OTHER; return 0; }
    if (!(c->fcw & 0x20u)) { *why = AB_DECLINE_FPU; return 0; }
    /* cheap exits first: [edi+20h] != 1, frame count < grid, compressed data */
    const uint16_t a0A = ab_m16(m, A + 0xAu), a08 = ab_m16(m, A + 8u), a14 = ab_m16(m, A + 0x14u), a16 = ab_m16(m, A + 0x16u);
    if (ab_m16(m, edi + 0x20u) != 1) { *why = AB_DECLINE_EXIT; return 0; }
    const uint32_t nY = (uint32_t)a0A + a08 + 1u, nP = (uint32_t)a14 + a16 + 1u;
    const int32_t nPs = (int16_t)nP, nYs = (int16_t)nY;
    if ((int32_t)(int16_t)ab_m16(m, edi + 0x22u) < (int32_t)((uint32_t)nYs * (uint32_t)nPs)) { *why = AB_DECLINE_EXIT; return 0; }
    if ((ab_m8(m, edi + 0x3Au) & 1u) && (ab_i8(m, AB_MODE_BYTE) || !ab_m32(m, edi + 0x88u))) { *why = AB_DECLINE_COMPRESSED; return 0; }
    const uint32_t WLO = E - AB_BELOW;
    if (ab_overlap(A, 0x18, WLO, AB_WIN) || ab_overlap(edi, 0xB0, WLO, AB_WIN)) { *why = AB_DECLINE_ALIAS; return 0; }
    /* Single-translation integer loads (X_M16/X_M32) that straddle a page read the host-adjacent page, which may be a
     * page of the stack window the native holds in its shadow: with every such base aligned none can straddle. */
    if ((A & 1u) || (edi & 3u)) { *why = AB_DECLINE_ALIGN; return 0; }

    uint8_t S[AB_WIN];
    x_guest_read_pages(S, WLO, AB_WIN);
    ab_fl flags = { c->f_kind, c->f_op1, c->f_op2, c->f_res, c->f_bits, c->f_cf_override, c->f_cf, c->f_of_override, c->f_of };
    ab_fl *const fl = &flags;
    ab_st state; ab_st *const st = &state;
    st->F = c->fsp & 7u;
    for (unsigned d = 1; d < 8; ++d) st->X[d] = c->st[(st->F - d) & 7u];
    st->X[0] = 0;
    st->fsw = c->fsw; st->fcw = c->fcw; st->be = 0;
    st->eax = c->r[0]; st->ecx = c->r[1]; st->edx = c->r[2]; st->ebx = c->r[3]; st->ebp = c->r[5]; st->esi = c->r[6];
    st->Qa = E - 0xF4u;
    st->zero = ab_f32(m, AB_ZERO); st->one = ab_f32(m, AB_ONE); st->qscale = ab_f32(m, AB_QSCALE);
    const double done = ab_f64(m, AB_DONE);
    const uint32_t df = c->df;
    double *const X = st->X;

    /* ---- f_000A39B0 prologue and setup (reads and stack only: may still decline) ---- */
    w32(S, AB_P + 4, st->ebx);                                 /* sub esp,0E8h; push ebx */
    st->ebx = A;
    st->eax = a0A;
    w32(S, AB_P, st->ebp);                                     /* push ebp */
    st->ebp = nY; st->ecx = a14;
    FLG(XK_SUB, 1, 1, 0, 16);                                  /* cmp word [edi+20h],1 (equal) */
    st->eax = a16;
    w32(S, AB_P + 0x40, st->ecx); w32(S, AB_P + 0x34, st->eax);
    st->eax = nP; w32(S, AB_P + 0x28, st->eax);
    st->eax = (uint32_t)nPs; st->ecx = (uint32_t)nYs;
    st->ecx = fl_imul32(fl, st->ecx, st->eax);
    w32(S, AB_P + 0x2C, st->eax);
    st->eax = (uint32_t)(int32_t)(int16_t)ab_m16(m, edi + 0x22u);
    FLG(XK_SUB, st->eax, st->ecx, st->eax - st->ecx, 32);
    { uint8_t b = ab_m8(m, edi + 0x3Au); FLG(XK_LOGIC, 0, 0, b & 1u, 8);
      if (b & 1u) {
          uint8_t al = ab_i8(m, AB_MODE_BYTE); st->eax = (st->eax & ~0xFFu) | al; FLG(XK_LOGIC, 0, 0, al, 8);
          st->eax = ab_m32(m, edi + 0x88u); FLG(XK_LOGIC, 0, 0, st->eax, 32);
      } }
    w8(S, AB_P + 0xB, 0);
    /* yaw */
    X[1] = sf(S, AB_P + 0xF4);
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 5u; FLG(XK_LOGIC, 0, 0, r, 8);
      X[1] = ab_parity_even(r) ? ab_f32(m, A + 4u) : ab_f32(m, A); }
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 0x44u; FLG(XK_LOGIC, 0, 0, r, 8);
      if (ab_parity_even(r)) { X[2] = sf(S, AB_P + 0xF4); X[2] = X[2] / X[1]; wf(S, AB_P + 0x10, X[2]); }
      else w32(S, AB_P + 0x10, 0); }
    w32(S, AB_Q, st->esi);                                     /* push esi */
    X[1] = sf(S, AB_Q + 0x14);
    ab_ftol(st, fl, S, 0xA3A7Au);
    X[1] = sf(S, AB_Q + 0x14);
    X[2] = done;
    st->esi = st->eax;
    if (!ab_fmod(st, fl, S, m, df, 0xA3A8Bu)) { *why = AB_DECLINE_FPU; return 0; }
    wf(S, AB_Q + 0x10, X[1]);
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 5u; FLG(XK_LOGIC, 0, 0, r, 8);
      if (!ab_parity_even(r)) {
          X[1] = sf(S, AB_Q + 0x10);
          st->esi -= 1u; FL_INCDEC();
          X[1] = X[1] + st->one; wf(S, AB_Q + 0x10, X[1]);
      } }
    st->eax = ab_m16(m, st->ebx + 0xAu);
    FLG(XK_SUB, (uint16_t)st->esi, (uint16_t)st->eax, (uint16_t)(st->esi - st->eax), 16);
    if (!((int16_t)st->esi < (int16_t)st->eax)) { st->esi = st->eax - 1u; w32(S, AB_Q + 0x10, 0x3F800000u); }
    st->eax = ab_m16(m, st->ebx + 8u);
    st->ecx = (uint32_t)(int32_t)(int16_t)st->eax;
    st->edx = (uint32_t)(int32_t)(int16_t)st->esi;
    st->ecx = 0u - st->ecx;
    FLG(XK_SUB, st->edx, st->ecx, st->edx - st->ecx, 32);
    if ((int32_t)st->edx < (int32_t)st->ecx) { st->esi = 0u - st->eax; w32(S, AB_Q + 0x10, 0); }
    /* pitch */
    X[1] = sf(S, AB_Q + 0xFC);
    st->esi += st->eax;
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 5u; FLG(XK_LOGIC, 0, 0, r, 8);
      X[1] = ab_parity_even(r) ? ab_f32(m, st->ebx + 0x10u) : ab_f32(m, st->ebx + 0xCu); }
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 0x44u; FLG(XK_LOGIC, 0, 0, r, 8);
      if (ab_parity_even(r)) { X[2] = sf(S, AB_Q + 0xFC); X[2] = X[2] / X[1]; wf(S, AB_Q + 0x14, X[2]); }
      else w32(S, AB_Q + 0x14, 0); }
    X[1] = sf(S, AB_Q + 0x14);
    ab_ftol(st, fl, S, 0xA3B2Cu);
    X[1] = sf(S, AB_Q + 0x14);
    X[2] = done;
    st->ebx = st->eax;
    if (!ab_fmod(st, fl, S, m, df, 0xA3B3Du)) { *why = AB_DECLINE_FPU; return 0; }
    wf(S, AB_Q + 0x18, X[1]);
    CMP(X[1], 1);
    FNSTSW_AX();
    { uint32_t r = ((uint32_t)st->fsw >> 8) & 5u; FLG(XK_LOGIC, 0, 0, r, 8);
      if (!ab_parity_even(r)) {
          X[1] = sf(S, AB_Q + 0x18);
          st->ebx -= 1u; FL_INCDEC();
          X[1] = X[1] + st->one; wf(S, AB_Q + 0x18, X[1]);
      } }
    st->eax = s32(S, AB_Q + 0x38);
    FLG(XK_SUB, (uint16_t)st->ebx, (uint16_t)st->eax, (uint16_t)(st->ebx - st->eax), 16);
    if (!((int16_t)st->ebx < (int16_t)st->eax)) { st->ebx = st->eax - 1u; w32(S, AB_Q + 0x18, 0x3F800000u); }
    st->eax = s32(S, AB_Q + 0x44);
    st->ecx = (uint32_t)(int32_t)(int16_t)st->eax;
    st->edx = (uint32_t)(int32_t)(int16_t)st->ebx;
    st->ecx = 0u - st->ecx;
    FLG(XK_SUB, st->edx, st->ecx, st->edx - st->ecx, 32);
    if ((int32_t)st->edx < (int32_t)st->ecx) { st->ebx = 0u - st->eax; w32(S, AB_Q + 0x18, 0); }
    st->ebx += st->eax;
    uint32_t cnt16 = 0, nodes = 0;
    int ran_loop = 0;
    FLG(XK_LOGIC, 0, 0, (uint16_t)st->ebx, 16);
    if ((int16_t)st->ebx < 0) goto out;
    { uint16_t b = s16(S, AB_Q + 0x2C); FLG(XK_SUB, (uint16_t)st->ebx, b, (uint16_t)(st->ebx - b), 16);
      if (!((int16_t)st->ebx < (int16_t)b)) goto out; }
    FLG(XK_LOGIC, 0, 0, (uint16_t)st->esi, 16);
    if ((int16_t)st->esi < 0) goto out;
    FLG(XK_SUB, (uint16_t)st->esi, (uint16_t)st->ebp, (uint16_t)(st->esi - st->ebp), 16);
    if (!((int16_t)st->esi < (int16_t)st->ebp)) goto out;
    st->eax = (uint32_t)(int32_t)(int16_t)st->esi;
    st->ecx = (uint32_t)(int32_t)(int16_t)st->ebp;
    st->edx = st->eax + 1u;
    FLG(XK_SUB, st->edx, st->ecx, st->edx - st->ecx, 32);
    if (st->edx == st->ecx) st->edx = st->eax;
    st->ecx = s32(S, AB_Q + 0x30);
    st->eax = (uint32_t)(int32_t)(int16_t)st->ebx;
    FL_INCDEC(); st->eax += 1u;
    FLG(XK_SUB, st->eax, st->ecx, st->eax - st->ecx, 32);
    if (st->eax == st->ecx) st->eax = (uint32_t)(int32_t)(int16_t)st->ebx;
    w32(S, AB_Q + 0x28, st->eax);
    st->eax = st->ebx;
    st->eax = fl_imul32(fl, st->eax, st->ebp);
    st->ecx = st->eax + st->esi;
    st->ebx = st->eax + st->edx;
    st->eax = s32(S, AB_Q + 0x28);
    st->eax = fl_imul32(fl, st->eax, st->ebp);
    w32(S, AB_Q + 0x30, st->ecx);
    st->esi += st->eax;
    st->ebp = st->eax + st->edx;
    w32(S, AB_Q + 0x2C, st->ebx); w32(S, AB_Q + 0x44, st->esi); w32(S, AB_Q + 0x38, st->ebp);
    ab_frame_ptr(st, fl, S, m, edi, st->ecx, 0xA3C06u);
    w32(S, AB_Q + 0x3C, st->eax);
    ab_frame_ptr(st, fl, S, m, edi, st->ebx, 0xA3C12u);
    w32(S, AB_Q + 0x40, st->eax);
    ab_frame_ptr(st, fl, S, m, edi, st->esi, 0xA3C1Eu);
    w32(S, AB_Q + 0x34, st->eax);
    ab_frame_ptr(st, fl, S, m, edi, st->ebp, 0xA3C2Au);
    st->esi = 0; st->ebx = 0; st->ebp = 0;
    cnt16 = ab_m16(m, edi + 0x2Cu);
    FLG(XK_SUB, cnt16, 0, cnt16, 16);
    w32(S, AB_Q + 0x14, st->eax);
    w32(S, AB_Q + 0x1C, st->esi);
    if ((int16_t)cnt16 <= 0) goto out;

    /* ---- declines that depend on the loop's data, then the node loop (writes guest memory) ---- */
    nodes = s32(S, AB_Q + 0x100);
    if ((nodes | s32(S, AB_Q + 0x3C) | s32(S, AB_Q + 0x40) | s32(S, AB_Q + 0x34) | s32(S, AB_Q + 0x14)) & 3u) { *why = AB_DECLINE_ALIGN; return 0; }
    {
        const uint32_t n = (uint32_t)(int16_t)cnt16, span = n * 32u, words = (n + 31u) >> 5;
        if (ab_overlap(nodes, span, WLO, AB_WIN) || ab_overlap(nodes, span, edi + 0x2Cu, 0x40u + 4u * words) ||
            ab_overlap(nodes, span, AB_ZERO, 4) || ab_overlap(nodes, span, AB_ONE, 4) || ab_overlap(nodes, span, AB_QSCALE, 4) ||
            ab_overlap(s32(S, AB_Q + 0x3C), 20u * n, WLO, AB_WIN) || ab_overlap(s32(S, AB_Q + 0x40), 20u * n, WLO, AB_WIN) ||
            ab_overlap(s32(S, AB_Q + 0x34), 20u * n, WLO, AB_WIN) || ab_overlap(s32(S, AB_Q + 0x14), 20u * n, WLO, AB_WIN)) {
            *why = AB_DECLINE_ALIAS; return 0;
        }
    }
    ran_loop = 1;
    for (;;) {                                                 /* L_A3C42 */
        st->ecx = s32(S, AB_Q + 0x100);
        st->eax = (uint32_t)(int32_t)(int16_t)st->ebx;
        st->eax = fl_shl32(fl, st->eax, 5);
        st->eax += st->ecx;
        FLG(XK_LOGIC, 0, 0, st->ebx & 0x1Fu, 8);
        w32(S, AB_Q + 0x48, st->eax);
        info->nodes++;
        if (!(st->ebx & 0x1Fu)) {
            uint16_t ax = fl_sar16_5(fl, (uint16_t)st->ebx);
            st->eax = (uint32_t)(int32_t)(int16_t)ax;
            st->ecx = ab_m32(m, edi + st->eax * 4u + 0x5Cu);
            st->edx = ab_m32(m, edi + st->eax * 4u + 0x6Cu);
            w32(S, AB_Q + 0x28, st->ecx); w32(S, AB_Q + 0x50, st->edx);
        }
        FLG(XK_LOGIC, 0, 0, s8(S, AB_Q + 0x50) & 1u, 8);
        if (s8(S, AB_Q + 0x50) & 1u) {                         /* rotation */
            info->rotated++;
            uint8_t al = s8(S, AB_Q + 0xF); st->eax = (st->eax & ~0xFFu) | al; FLG(XK_LOGIC, 0, 0, al, 8);
            st->eax = QA(0x84);
            st->esi = s32(S, AB_Q + 0x3C); st->ecx = st->esi;
            ab_quat_decode(st, S, m, AB_Q + 0x84, 0xA3D21u);
            st->esi += 8u; w32(S, AB_Q + 0x3C, st->esi);
            st->esi = s32(S, AB_Q + 0x40); st->eax = QA(0xD4); st->ecx = st->esi;
            ab_quat_decode(st, S, m, AB_Q + 0xD4, 0xA3D3Au);
            st->esi += 8u; w32(S, AB_Q + 0x40, st->esi);
            st->esi = s32(S, AB_Q + 0x34); st->eax = QA(0x94); st->ecx = st->esi;
            ab_quat_decode(st, S, m, AB_Q + 0x94, 0xA3D53u);
            st->esi += 8u; w32(S, AB_Q + 0x34, st->esi);
            st->esi = s32(S, AB_Q + 0x14); st->eax = QA(0xB4); st->ecx = st->esi;
            ab_quat_decode(st, S, m, AB_Q + 0xB4, 0xA3D6Cu);
            st->esi += 8u; w32(S, AB_Q + 0x14, st->esi);
            st->edx = s32(S, AB_Q + 0x10);
            st->esi = QA(0xC4); st->ecx = QA(0xD4); st->edx = QA(0x84);
            ab_lerp(st, fl, S, s32(S, AB_Q + 0x10), AB_Q + 0xD4, AB_Q + 0x84, AB_Q + 0xC4, 0xA3D92u);
            st->ecx = QA(0xC4);
            ab_normalize(st, fl, S, AB_Q + 0xC4, 0xA3D9Eu);
            st->eax = s32(S, AB_Q + 0x10);
            st->esi = QA(0xA4); st->ecx = QA(0xB4); st->edx = QA(0x94);
            ab_lerp(st, fl, S, st->eax, AB_Q + 0xB4, AB_Q + 0x94, AB_Q + 0xA4, 0xA3DBDu);
            st->ecx = QA(0xA4);
            ab_normalize(st, fl, S, AB_Q + 0xA4, 0xA3DC9u);
            st->ecx = s32(S, AB_Q + 0x18);
            const uint32_t tp = st->ecx;
            st->esi = QA(0xE4); st->ecx = QA(0xA4); st->edx = QA(0xC4);
            ab_lerp(st, fl, S, tp, AB_Q + 0xA4, AB_Q + 0xC4, AB_Q + 0xE4, 0xA3DE8u);
            st->ecx = QA(0xE4);
            ab_normalize(st, fl, S, AB_Q + 0xE4, 0xA3DF4u);
            st->eax = s32(S, AB_Q + 0x48); st->edx = st->eax; st->ecx = QA(0xE4);
            ab_quat_apply(st, fl, S, m, AB_Q + 0xE4, st->eax, 0xA3E06u);
            st->esi = s32(S, AB_Q + 0x1C);
        }
        st->ecx = s32(S, AB_Q + 0x50);                          /* L_A3E0A */
        { uint8_t al = s8(S, AB_Q + 0x28); st->eax = (st->eax & ~0xFFu) | al;
          st->ecx = fl_shr32_1(fl, st->ecx);
          FLG(XK_LOGIC, 0, 0, al & 1u, 8);
          w32(S, AB_Q + 0x50, st->ecx);
          if (al & 1u) {                                        /* translation */
              info->translated++;
              X[1] = st->one;
              uint8_t cf = s8(S, AB_Q + 0xF); st->eax = (st->eax & ~0xFFu) | cf; FLG(XK_LOGIC, 0, 0, cf, 8);
              X[1] = X[1] - sf(S, AB_Q + 0x10); wf(S, AB_Q + 0x24, X[1]);
              X[1] = st->one; X[1] = X[1] - sf(S, AB_Q + 0x18); wf(S, AB_Q + 0x4C, X[1]);
              /* L_A3EC6: the four keyframe translations (raw dwords) onto the stack */
              st->eax = s32(S, AB_Q + 0x3C); st->ecx = st->eax;
              st->edx = ab_m32(m, st->ecx); w32(S, AB_Q + 0x54, st->edx);
              st->edx = ab_m32(m, st->ecx + 4u); st->ecx = ab_m32(m, st->ecx + 8u);
              st->eax += 0xCu; w32(S, AB_Q + 0x58, st->edx); w32(S, AB_Q + 0x3C, st->eax);
              st->eax = s32(S, AB_Q + 0x40); w32(S, AB_Q + 0x5C, st->ecx); st->edx = st->eax;
              st->ecx = ab_m32(m, st->edx); w32(S, AB_Q + 0x6C, st->ecx);
              st->ecx = ab_m32(m, st->edx + 4u); st->edx = ab_m32(m, st->edx + 8u);
              st->eax += 0xCu; w32(S, AB_Q + 0x70, st->ecx); w32(S, AB_Q + 0x40, st->eax);
              st->eax = s32(S, AB_Q + 0x34); w32(S, AB_Q + 0x74, st->edx); st->ecx = st->eax;
              st->edx = ab_m32(m, st->ecx); w32(S, AB_Q + 0x60, st->edx);
              st->edx = ab_m32(m, st->ecx + 4u); st->ecx = ab_m32(m, st->ecx + 8u);
              st->eax += 0xCu; w32(S, AB_Q + 0x64, st->edx); w32(S, AB_Q + 0x34, st->eax);
              st->eax = s32(S, AB_Q + 0x14); st->edx = st->eax; w32(S, AB_Q + 0x68, st->ecx);
              st->ecx = ab_m32(m, st->edx); w32(S, AB_Q + 0x78, st->ecx);
              st->ecx = ab_m32(m, st->edx + 4u); st->edx = ab_m32(m, st->edx + 8u);
              st->eax += 0xCu; w32(S, AB_Q + 0x7C, st->ecx); w32(S, AB_Q + 0x80, st->edx); w32(S, AB_Q + 0x14, st->eax);
              /* L_A3F4D: bilinear blend added to the node position */
              const double fy = sf(S, AB_Q + 0x10), fp = sf(S, AB_Q + 0x18), gy = sf(S, AB_Q + 0x24), gp = sf(S, AB_Q + 0x4C);
              st->eax = s32(S, AB_Q + 0x48);
              const uint32_t node = st->eax;
              X[1] = sf(S, AB_Q + 0x60) * gy;
              X[2] = sf(S, AB_Q + 0x78) * fy; X[1] = X[1] + X[2];
              X[1] = X[1] * fp;
              X[2] = sf(S, AB_Q + 0x54) * gy;
              X[3] = sf(S, AB_Q + 0x6C) * fy; X[2] = X[2] + X[3];
              X[2] = X[2] * gp;
              X[1] = X[1] + X[2];
              X[1] = X[1] + ab_f32(m, node + 0x10u); ab_wf32(m, node + 0x10u, X[1]);
              X[1] = sf(S, AB_Q + 0x64) * gy;
              X[2] = sf(S, AB_Q + 0x7C) * fy; X[1] = X[1] + X[2];
              X[1] = X[1] * fp;
              X[2] = sf(S, AB_Q + 0x58) * gy;
              X[3] = sf(S, AB_Q + 0x70) * fy; X[2] = X[2] + X[3];
              X[2] = X[2] * gp;
              X[1] = X[1] + X[2];
              X[1] = X[1] + ab_f32(m, node + 0x14u); ab_wf32(m, node + 0x14u, X[1]);
              X[1] = sf(S, AB_Q + 0x5C) * gy;
              X[2] = sf(S, AB_Q + 0x74) * fy; X[1] = X[1] + X[2];
              X[1] = X[1] * gp;
              X[2] = sf(S, AB_Q + 0x68) * gy;
              X[3] = sf(S, AB_Q + 0x80) * fy; X[2] = X[2] + X[3];
              X[2] = X[2] * fp;
              X[1] = X[1] + X[2];
              X[1] = X[1] + ab_f32(m, node + 0x18u); ab_wf32(m, node + 0x18u, X[1]);
          } }
        st->edx = s32(S, AB_Q + 0x28);                          /* L_A3FF0 */
        st->edx = fl_shr32_1(fl, st->edx);
        FL_INCDEC(); st->ebx += 1u;
        FLG(XK_SUB, (uint16_t)st->ebx, cnt16, (uint16_t)(st->ebx - cnt16), 16);
        w32(S, AB_Q + 0x28, st->edx);
        if ((int16_t)st->ebx < (int16_t)cnt16) { st->be++; continue; }
        break;
    }
out:
    (void)ran_loop;
    /* L_A4005: pop esi; pop ebp; pop ebx; add esp,0E8h; ret 0Ch */
    st->esi = s32(S, AB_Q); st->ebp = s32(S, AB_Q + 4); st->ebx = s32(S, AB_Q + 8);
    x_guest_write_pages(WLO, S, AB_BELOW);
    c->r[0] = st->eax; c->r[1] = st->ecx; c->r[2] = st->edx; c->r[3] = st->ebx; c->r[5] = st->ebp; c->r[6] = st->esi;
    c->r[4] = E + 0x10u;
    for (unsigned d = 1; d < 8; ++d) c->st[(st->F - d) & 7u] = st->X[d];
    c->fsw = st->fsw; c->fcw = st->fcw;
    c->f_kind = fl->kind; c->f_op1 = fl->op1; c->f_op2 = fl->op2; c->f_res = fl->res; c->f_bits = fl->bits;
    c->f_cf_override = fl->cfo; c->f_cf = fl->cf; c->f_of_override = fl->ofo; c->f_of = fl->of;
    info->backedges = st->be;
    return 1;
}

/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go at the end of the call. */
static void ab_budget(xctx *c, uint32_t backedges)
{
    while (backedges) {
        int32_t n = c->preempt >= 1 ? c->preempt : 1;
        if ((uint32_t)n > backedges) { c->preempt -= (int32_t)backedges; return; }
        backedges -= (uint32_t)n; c->preempt -= n;
        xv_preempt(c);
    }
}

/* Window counters: updated by the tick thread (or an object job lane), read and reset by the 60-frame report. */
enum { AB_CALLS, AB_NATIVE, AB_VERIFIED, AB_MISMATCHED, AB_NODES, AB_ROTATED, AB_TRANSLATED,
       AB_TIMED_NATIVE, AB_TIMED_GUEST, AB_DECLINED, AB_COUNTERS = AB_DECLINED + AB_DECLINES };
static unsigned ab_counter[AB_COUNTERS], ab_mismatch_total;
static uint64_t ab_native_us, ab_guest_us;
#define AB_ADD(i, v) __atomic_fetch_add(&ab_counter[i], (unsigned)(v), __ATOMIC_RELAXED)
/* The context whose guest body verify mode is re-running (the hook then stands aside). Not thread-local (the Vita
 * build has no TLS): a race between two verifying threads can only make one call compare native against native. */
static xctx *volatile ab_guest_ctx;
extern void f_000A39B0(xctx *);

static int ab_mode_value = -1;
static int ab_mode(void)
{
    int mode = __atomic_load_n(&ab_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_AIM_BLEND"); mode = e ? atoi(e) : XV_NATIVE_AIM_BLEND_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        XK_LOG("[native-aim-blend] f_000A39B0 aim/look overlay blend: %s\n", mode == 2 ? "native" : mode == 1 ? "verify (native vs guest, guest result kept)" : "off");
        __atomic_store_n(&ab_mode_value, mode, __ATOMIC_RELAXED);
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_aim_blend_force(int mode) { __atomic_store_n(&ab_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); }
static int ab_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_AIM_BLEND_TIME"); on = e && atoi(e) != 0; }
    return on;
}
static void ab_count(const ab_info *o)
{
    AB_ADD(AB_NODES, o->nodes); AB_ADD(AB_ROTATED, o->rotated); AB_ADD(AB_TRANSLATED, o->translated);
}

/* Verify mode: what either path can write (the stack window, the node array), before/native/guest. */
typedef struct { uint8_t *before, *native; uint32_t cap; } ab_buf;
static ab_buf ab_nodes_buf; static int ab_nodes_busy;    /* one verifier at a time owns the node buffers */
static int ab_buf_reserve(ab_buf *b, uint32_t bytes)
{
    if (bytes <= b->cap) return 1;
    uint8_t *x = realloc(b->before, bytes), *y = x ? realloc(b->native, bytes) : 0;
    if (x) b->before = x;
    if (!y) return 0;
    b->native = y; b->cap = bytes; return 1;
}
static int ab_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
static void ab_report_mismatch(const char *what, uint32_t a, uint32_t b, const ab_info *o)
{
    if (__atomic_add_fetch(&ab_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-aim-blend] MISMATCH %s native %08X guest %08X (call: %u nodes, %u rotated, %u translated)\n",
               what, a, b, o->nodes, o->rotated, o->translated);
}

/* Entry hook of f_000A39B0: 1 = handled (guest body skipped). */
int xv_native_aim_blend(xctx *c)
{
    int mode = ab_mode();
    if (ab_guest_ctx == c) return 0;
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS)
    if (xv_is_object_job(c)) return 0;          /* object-job lanes keep the guest body (their own stack/lock rules) */
#endif
    const int timed = ab_timing();
    if (!mode) {                                /* off: only XV_NATIVE_AIM_BLEND_TIME=1 measures the guest body here */
        if (!timed) return 0;
        uint64_t t0 = xk_os_monotonic_us();
        ab_guest_ctx = c; f_000A39B0(c); ab_guest_ctx = 0;
        __atomic_fetch_add(&ab_guest_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); AB_ADD(AB_TIMED_GUEST, 1); AB_ADD(AB_CALLS, 1);
        return 1;
    }
    ab_info o; unsigned why = AB_DECLINE_OTHER;
    AB_ADD(AB_CALLS, 1);
    if (mode == 2) {
        uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
        if (!ab_run(c, &o, &why)) { AB_ADD(AB_DECLINED + why, 1); return 0; }
        if (timed) { __atomic_fetch_add(&ab_native_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); AB_ADD(AB_TIMED_NATIVE, 1); }
        ab_budget(c, o.backedges); ab_count(&o); AB_ADD(AB_NATIVE, 1);
        return 1;
    }
    /* verify: snapshot the stack window and the node array, run the native, keep its result, restore, run the
     * guest with an unbounded budget (no scheduling inside the check), compare, keep the guest's result. */
    const uint32_t E = c->r[4], lo = E - AB_BELOW;
    uint32_t nodes = X_M32(E + 0xCu), cnt = (uint32_t)(int32_t)(int16_t)X_M16(c->r[7] + 0x2Cu);
    uint32_t nbytes = (int32_t)cnt > 0 ? cnt * 32u : 0;
    if (__atomic_exchange_n(&ab_nodes_busy, 1, __ATOMIC_ACQUIRE)) {   /* another thread is verifying: guest only */
        AB_ADD(AB_DECLINED + AB_DECLINE_OTHER, 1);
        ab_guest_ctx = c; f_000A39B0(c); ab_guest_ctx = 0;
        return 1;
    }
    if (nbytes > (1u << 20) || (nbytes && !ab_buf_reserve(&ab_nodes_buf, nbytes))) {
        __atomic_store_n(&ab_nodes_busy, 0, __ATOMIC_RELEASE);
        XK_LOG("[native-aim-blend] verify: node buffer unavailable (%u bytes): verification off, guest path\n", nbytes);
        xv_native_aim_blend_force(0); return 0;
    }
    uint8_t stack_before[AB_WIN], stack_native[AB_WIN], stack_guest[AB_WIN];
    x_guest_read_pages(stack_before, lo, AB_WIN);
    if (nbytes) x_guest_read_pages(ab_nodes_buf.before, nodes, nbytes);
    const xctx before = *c;
    uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
    int handled = ab_run(c, &o, &why);
    uint64_t t1 = timed ? xk_os_monotonic_us() : 0;
    if (!handled) {                             /* declined: nothing was written; the guest runs normally */
        __atomic_store_n(&ab_nodes_busy, 0, __ATOMIC_RELEASE);
        AB_ADD(AB_DECLINED + why, 1);
        ab_guest_ctx = c; f_000A39B0(c); ab_guest_ctx = 0;
        return 1;
    }
    const xctx native = *c;
    x_guest_read_pages(stack_native, lo, AB_WIN);
    if (nbytes) x_guest_read_pages(ab_nodes_buf.native, nodes, nbytes);
    x_guest_write_pages(lo, stack_before, AB_WIN);
    if (nbytes) x_guest_write_pages(nodes, ab_nodes_buf.before, nbytes);
    *c = before; c->preempt = 1 << 30;
    uint64_t t2 = timed ? xk_os_monotonic_us() : 0;
    ab_guest_ctx = c; f_000A39B0(c); ab_guest_ctx = 0;
    uint64_t t3 = timed ? xk_os_monotonic_us() : 0;
    if (timed) {
        __atomic_fetch_add(&ab_native_us, t1 - t0, __ATOMIC_RELAXED); __atomic_fetch_add(&ab_guest_us, t3 - t2, __ATOMIC_RELAXED);
        AB_ADD(AB_TIMED_NATIVE, 1); AB_ADD(AB_TIMED_GUEST, 1);
    }
    uint32_t guest_backedges = (uint32_t)((1 << 30) - c->preempt);
    c->preempt = before.preempt;
    x_guest_read_pages(stack_guest, lo, AB_WIN);
    unsigned bad = 0;
#define AB_CMP(what, a, b) do { if ((a) != (b)) { bad++; ab_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &o); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) AB_CMP(rn[i], native.r[i], c->r[i]);
    AB_CMP("backedges", o.backedges, guest_backedges);
    AB_CMP("fsp", native.fsp, c->fsp); AB_CMP("fcw", native.fcw, c->fcw); AB_CMP("fsw", native.fsw, c->fsw);
    AB_CMP("df", native.df, c->df);
    AB_CMP("f_kind", native.f_kind, c->f_kind); AB_CMP("f_op1", native.f_op1, c->f_op1); AB_CMP("f_op2", native.f_op2, c->f_op2);
    AB_CMP("f_res", native.f_res, c->f_res); AB_CMP("f_bits", native.f_bits, c->f_bits);
    AB_CMP("f_cf_override", native.f_cf_override, c->f_cf_override); AB_CMP("f_of_override", native.f_of_override, c->f_of_override);
    AB_CMP("f_cf", native.f_cf, c->f_cf); AB_CMP("f_of", native.f_of, c->f_of);
    for (unsigned i = 0; i < 8; ++i)
        if (!ab_same_double(native.st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &native.st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st slot %u (lo word)", i); bad++; ab_report_mismatch(w, (uint32_t)a, (uint32_t)b, &o);
        }
    for (unsigned i = 0; i < AB_WIN; i += 4) {
        uint32_t a, b; memcpy(&a, stack_native + i, 4); memcpy(&b, stack_guest + i, 4);
        if (a != b) { bad++; char w[48]; snprintf(w, sizeof w, "stack[esp%+d]", (int)(lo + i - E)); ab_report_mismatch(w, a, b, &o); }
    }
    if (nbytes) {
        x_guest_read_pages(ab_nodes_buf.before, nodes, nbytes);            /* now the guest's result */
        for (unsigned i = 0; i < nbytes; i += 4) {
            uint32_t a, b; memcpy(&a, ab_nodes_buf.native + i, 4); memcpy(&b, ab_nodes_buf.before + i, 4);
            if (a != b) { bad++; char w[48]; snprintf(w, sizeof w, "node %u +%02X", i / 32u, i % 32u); ab_report_mismatch(w, a, b, &o); break; }
        }
    }
#undef AB_CMP
    __atomic_store_n(&ab_nodes_busy, 0, __ATOMIC_RELEASE);
    ab_budget(c, guest_backedges);
    ab_count(&o); AB_ADD(AB_VERIFIED, 1);
    if (bad) AB_ADD(AB_MISMATCHED, 1);
    return 1;
}

void xv_native_aim_blend_report(unsigned frames)
{
    unsigned n[AB_COUNTERS];
    for (unsigned i = 0; i < AB_COUNTERS; ++i) n[i] = __atomic_exchange_n(&ab_counter[i], 0u, __ATOMIC_RELAXED);
    uint64_t native_us = __atomic_exchange_n(&ab_native_us, 0, __ATOMIC_RELAXED), guest_us = __atomic_exchange_n(&ab_guest_us, 0, __ATOMIC_RELAXED);
    if (!n[AB_CALLS]) return;
    char timing[96] = "";
    if (n[AB_TIMED_NATIVE])
        snprintf(timing, sizeof timing, "; us/call native %.2f", (double)native_us / n[AB_TIMED_NATIVE]);
    if (n[AB_TIMED_GUEST])
        snprintf(timing + strlen(timing), sizeof timing - strlen(timing), "%s guest %.2f", n[AB_TIMED_NATIVE] ? "" : "; us/call", (double)guest_us / n[AB_TIMED_GUEST]);
    XK_LOG("[native-aim-blend] %u frames: calls %u native %u verified %u mismatched %u (total mismatches %u); nodes %u rotated %u translated %u; "
           "declined compressed %u exit %u fpu %u alias %u align %u other %u%s\n",
           frames, n[AB_CALLS], n[AB_NATIVE], n[AB_VERIFIED], n[AB_MISMATCHED], __atomic_load_n(&ab_mismatch_total, __ATOMIC_RELAXED),
           n[AB_NODES], n[AB_ROTATED], n[AB_TRANSLATED], n[AB_DECLINED + AB_DECLINE_COMPRESSED], n[AB_DECLINED + AB_DECLINE_EXIT],
           n[AB_DECLINED + AB_DECLINE_FPU], n[AB_DECLINED + AB_DECLINE_ALIAS], n[AB_DECLINED + AB_DECLINE_ALIGN],
           n[AB_DECLINED + AB_DECLINE_OTHER], timing);
}
