/* xk_native_effects.c - native Halo CE (Xbox 3925) helpers of the effects render pass and the material setup
 * (XV_NATIVE_EFFECTS). docs/native-effects.md.
 *
 * f_0005E270 (the scene's effects pass: 5DFF0 sorts the effects, then f_00066510 per effect, recursively) and the
 * model material path share a set of small, hot helpers. Seven of them are native here, each one whole function
 * behind a hook at its entry:
 *   f_0007E530  vertex-shader constants from 52-byte transform records (scale x 3x3 + translation), then
 *               SetVertexShaderConstant(-36, 0x278258, 3n)
 *   f_0007E420  the two lights of a model: 7DBE0 (inline) per light, 11 constant rows, SetVertexShaderConstant(-79, 11)
 *   f_00056F20  texture-coordinate animation of a shader map: three calls of the periodic function 173F20 (the
 *               guest's body), the rotation (fcos / fsin as the lifted code computes them), the two output rows
 *   f_00080360  a shader map's bitmap for a stage: the animated sequence (1290E0 inline), 80250 inline (325C0, the
 *               texture cache, stays the guest's body), SetTexture, the width/height words at 0x278A9C
 *   f_00011B60, f_00011610, f_00011BD0  float colour -> D3DCOLOR (x 255, fistp under the guest's control word)
 * They are hand-written from the lifted code and reproduce everything a caller or a later instruction can observe:
 * every guest store (address, width, value, order where reads can see it), the registers at return, the lazy-flag
 * record exactly as the last flag-writing instruction leaves it, the x87 stack slots the function pushes (values, TOP,
 * status word bits the lifted code writes), the back-edge budget (X_PREEMPT at the same back-edges), the D3D HLE
 * entries with the same registers and stack, and the guest callees (325C0, 173F20, the constant-pack prefix)
 * called with the full context as the lifted body calls them. Floats are the lifted code's doubles with the same
 * operation order (-ffp-contract=off: no fused multiply-add); fistp is the x87 rounding control's rounding. Integer
 * loads are one translation (X_M32), float loads/stores page-split (x87_load_f32 / x87_store_f32); a record that lies
 * in one page is translated once (hview) - the same host bytes either way. Unproven layouts are declined: an unaligned
 * esp for the colour packers, an object-job context.
 *
 * XV_NATIVE_EFFECTS build flag (hooks: tools/patch_native_effects_hooks.py puts a wrapper in front of each hooked body,
 * renamed f_XXXXXXXX_body). Env XV_NATIVE_EFFECTS: 0 off (default XV_NATIVE_EFFECTS_DEFAULT), 1 verify, 2 native.
 * Verify: the journal variant runs first (every guest store logged with its old bytes; the D3D HLE entries it makes
 * are observed through xd3d.c's tap), its results are recorded, its stores undone and the stack window around esp
 * restored, then the lifted body runs on the same state (HLE entries observed); registers, flags, x87 state, budget,
 * the stack window, every byte the native stored and the HLE call streams (name, registers, arguments, and a hash of
 * the constant/program data they read) are compared. The guest's result is kept. Both runs get an unbounded
 * back-edge budget; the guest's consumption is applied afterwards (xv_preempt at the end: a scheduling point only).
 * XV_NATIVE_EFFECTS_TIME=1: ns clock per hooked call (mode 0 the lifted body, 2 the native, 1 both).
 * Acting set: by default 7E530, 56F20, 11B60, 11610, 11BD0 (7E420 and 80360 are hooked and verified but their natives
 * are not faster in the game); XV_NATIVE_EFFECTS_FUNCS=all, or <hex,...>: only these act (the others run the lifted
 * body; with XV_NATIVE_EFFECTS_TIME=1 they are timed as guest).
 * Report: [native-effects] every 60 frames (recomp/kernel/xd3d.c's weak report chain).
 *
 * Threads: no __thread (emutls on vitasdk), no mutable statics a call uses: the journal, the tap logs and the window
 * copies live in the verify call's frame or heap. Shared: the counters (relaxed atomics), the mode words (set once)
 * and the verify claim (one reference run at a time; a second context meanwhile runs the lifted body, counted as
 * skipped). */
#include "xk.h"
#include "../xv_phase.h"
#include "../xv_x87reg.h"
#include "xk_object_jobs.h"
#include "xv_recomp_protos.h"     /* the stage's: XV_HLE_CALL, the HLE entry points (recomp/ is on the include path) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__vita__)
#include <time.h>
#endif

#ifndef XV_NATIVE_EFFECTS_DEFAULT
#define XV_NATIVE_EFFECTS_DEFAULT 0
#endif

/* ---- the verify journal (per call) --------------------------------------------------------------------------- */
typedef struct { uint8_t *host; uint32_t addr, n; uint8_t old[8], now[8]; } nx_jent;
typedef struct nx_journal { nx_jent *e; unsigned n, cap; int overflow; nx_jent inl[256]; } nx_journal;
static __attribute__((noinline)) nx_jent *nx_jgrow(nx_journal *J)
{
    if (J->n < 256u) { J->e = J->inl; J->cap = 256u; return &J->e[J->n++]; }
    unsigned cap = J->cap < 1024u ? 1024u : J->cap * 2u;
    nx_jent *e = J->e == J->inl ? malloc(cap * sizeof *e) : realloc(J->e, cap * sizeof *e);
    if (!e) { J->overflow = 1; return 0; }
    if (J->e == J->inl) memcpy(e, J->inl, sizeof J->inl);
    J->e = e; J->cap = cap; return &J->e[J->n++];
}
static inline void nx_jlog_host(nx_journal *J, uint32_t addr, uint8_t *host, unsigned n)
{
    nx_jent *e = J->n < J->cap ? &J->e[J->n++] : nx_jgrow(J);
    if (!e) return;
    e->host = host; e->addr = addr; e->n = n; memcpy(e->old, host, n);
}
/* A store through x_guest_write (x87 stores): one translation when it fits the page, otherwise a split entry (host
 * 0: saved, restored and compared through x_guest_read_pages / x_guest_write_pages, one value for the compare). */
static void nx_jlog_guest(nx_journal *J, uint32_t a, unsigned n)
{
    if (n <= 4096u - (a & 0xFFFu)) { nx_jlog_host(J, a, (uint8_t *)g_xram + X_PT[a >> 12] + (a & 0xFFFu), n); return; }
    nx_jent *e = J->n < J->cap ? &J->e[J->n++] : nx_jgrow(J);
    if (!e) return;
    e->host = 0; e->addr = a; e->n = n; x_guest_read_pages(e->old, a, n);
}
static inline void nx_jread(const nx_jent *e, uint8_t *out) { if (e->host) memcpy(out, e->host, e->n); else x_guest_read_pages(out, e->addr, e->n); }
static inline void nx_jwrite(const nx_jent *e, const uint8_t *in) { if (e->host) memcpy(e->host, in, e->n); else x_guest_write_pages(e->addr, in, e->n); }
static void nx_jfree(nx_journal *J) { if (J->e && J->e != J->inl) free(J->e); J->e = 0; J->n = J->cap = 0; }

/* ---- guest memory as the lifted code addresses it ------------------------------------------------------------- */
/* The calling thread's table (X_PT: TPIDRURW on the Vita, the host thread's pointer in the harness), taken once per
 * call like the shard preamble's xpt_. Integer accesses are one translation (X_M32: an access that straddles a page end
 * reads/writes the next host bytes, as the lifted code does); float accesses are page-split (x87_load_f32 /
 * x87_store_f32 / x87_store_i32 through x_guest_read / x_guest_write). Stores log their old bytes when J is set. */
typedef struct { uint8_t *ram; const uint32_t *pt; nx_journal *J; } hmem;
#define HM_INIT(J_) const hmem hm_ = { g_xram, X_PT, (J_) }; const hmem *const m = &hm_
static inline __attribute__((always_inline)) uint8_t *hm_p(const hmem *m, uint32_t a) { return m->ram + m->pt[a >> 12] + (a & 0xFFFu); }
static inline __attribute__((always_inline)) uint32_t hm_r32(const hmem *m, uint32_t a) { uint32_t v; memcpy(&v, hm_p(m, a), 4); return v; }
static inline __attribute__((always_inline)) uint16_t hm_r16(const hmem *m, uint32_t a) { uint16_t v; memcpy(&v, hm_p(m, a), 2); return v; }
static inline __attribute__((always_inline)) uint8_t hm_r8(const hmem *m, uint32_t a) { return *hm_p(m, a); }
static inline __attribute__((always_inline)) void hm_w32(const hmem *m, uint32_t a, uint32_t v) { uint8_t *p = hm_p(m, a); if (m->J) nx_jlog_host(m->J, a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void hm_w16(const hmem *m, uint32_t a, uint16_t v) { uint8_t *p = hm_p(m, a); if (m->J) nx_jlog_host(m->J, a, p, 2); memcpy(p, &v, 2); }
static inline __attribute__((always_inline)) void hm_w8(const hmem *m, uint32_t a, uint8_t v) { uint8_t *p = hm_p(m, a); if (m->J) nx_jlog_host(m->J, a, p, 1); *p = v; }
/* X_IMG* (constant image addresses): through the table under the render view, else the flat image base */
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define HM_IMG(m, a) hm_p((m), (a))
#else
#define HM_IMG(m, a) ((uint8_t *)X_IMG_BASE + (uint32_t)(a))
#endif
static inline __attribute__((always_inline)) uint32_t hm_rimg32(const hmem *m, uint32_t a) { uint32_t v; memcpy(&v, HM_IMG(m, a), 4); return v; }
static inline __attribute__((always_inline)) void hm_wimg32(const hmem *m, uint32_t a, uint32_t v) { uint8_t *p = HM_IMG(m, a); if (m->J) nx_jlog_host(m->J, a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void hm_wimg16(const hmem *m, uint32_t a, uint16_t v) { uint8_t *p = HM_IMG(m, a); if (m->J) nx_jlog_host(m->J, a, p, 2); memcpy(p, &v, 2); }
static inline __attribute__((always_inline)) double hm_rf(const hmem *m, uint32_t a)         /* x87_load_f32 */
{ float f; if ((a & 0xFFFu) <= 0xFFCu) memcpy(&f, hm_p(m, a), 4); else x_guest_read_pages(&f, a, 4); return (double)f; }
static inline __attribute__((always_inline)) void hm_wbytes(const hmem *m, uint32_t a, const void *v, unsigned n)   /* x_guest_write */
{
    if (n <= 4096u - (a & 0xFFFu)) { uint8_t *p = hm_p(m, a); if (m->J) nx_jlog_host(m->J, a, p, n); memcpy(p, v, n); }
    else { if (m->J) nx_jlog_guest(m->J, a, n); x_guest_write_pages(a, v, n); }
}
static inline __attribute__((always_inline)) float hm_wf(const hmem *m, uint32_t a, double v) { float f = (float)v; hm_wbytes(m, a, &f, 4); return f; }   /* x87_store_f32 */
static inline __attribute__((always_inline)) uint32_t hx_i32(double v) { return (v >= -2147483648.0 && v <= 2147483647.0) ? (uint32_t)(int32_t)v : 0x80000000u; }
static inline __attribute__((always_inline)) uint32_t hm_wi32(const hmem *m, uint32_t a, double v) { uint32_t r = hx_i32(v); hm_wbytes(m, a, &r, 4); return r; }   /* x87_store_i32 */
/* A record view: the host base of guest [lo, lo + len) when the range lies in one page (then every integer and float
 * access inside it is the same host location the lifted code's single or page-split access reaches), else 0 and the
 * accessors translate each access. Taken once per call (and again after a guest call), as the 1721B0 native does per
 * frame and record: the table entry a scene thread translates through changes only when a page is mapped. */
typedef struct { uint8_t *h; uint32_t lo, len; } hview;
static inline __attribute__((always_inline)) hview hv_make(const hmem *m, uint32_t lo, uint32_t len)
{ hview v = { 0, lo, len }; if ((lo & 0xFFFu) + len <= 4096u) v.h = hm_p(m, lo); return v; }
#define HV_OK(v, a, n) ((v)->h && (uint32_t)((a) - (v)->lo) <= (v)->len - (n))
static inline __attribute__((always_inline)) uint32_t hv_r32(const hmem *m, const hview *v, uint32_t a)
{ if (HV_OK(v, a, 4)) { uint32_t x; memcpy(&x, v->h + (a - v->lo), 4); return x; } return hm_r32(m, a); }
static inline __attribute__((always_inline)) uint16_t hv_r16(const hmem *m, const hview *v, uint32_t a)
{ if (HV_OK(v, a, 2)) { uint16_t x; memcpy(&x, v->h + (a - v->lo), 2); return x; } return hm_r16(m, a); }
static inline __attribute__((always_inline)) double hv_rf(const hmem *m, const hview *v, uint32_t a)
{ if (HV_OK(v, a, 4)) { float x; memcpy(&x, v->h + (a - v->lo), 4); return (double)x; } return hm_rf(m, a); }
static inline __attribute__((always_inline)) void hv_w32(const hmem *m, const hview *v, uint32_t a, uint32_t x)
{ if (HV_OK(v, a, 4)) { uint8_t *p = v->h + (a - v->lo); if (m->J) nx_jlog_host(m->J, a, p, 4); memcpy(p, &x, 4); } else hm_w32(m, a, x); }
static inline __attribute__((always_inline)) void hv_wf(const hmem *m, const hview *v, uint32_t a, double d)
{ if (HV_OK(v, a, 4)) { float x = (float)d; uint8_t *p = v->h + (a - v->lo); if (m->J) nx_jlog_host(m->J, a, p, 4); memcpy(p, &x, 4); } else (void)hm_wf(m, a, d); }
/* x87_compare's condition codes (one VFP compare on Thumb-2, as xv_x87reg.h's x87r_compare) */
static inline __attribute__((always_inline)) uint16_t hx_cc(double a, double b)
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
    return (a != a || b != b) ? 0x4500 : a < b ? 0x0100 : a == b ? 0x4000 : 0;
#endif
}
/* x87_round's value as far as fistp can tell (the integer): round to nearest even exactly (no libm call and no FP
 * environment round trip), floor / ceil / trunc under the other rounding controls. nearbyint(v) and this differ only
 * in the sign of a zero, which no integer store sees. */
static inline __attribute__((always_inline)) double hx_rint(double v)
{
    double a = fabs(v);
    if (!(a < 4503599627370496.0)) return v;                    /* integral already, or NaN/inf: unchanged */
    double r = (a + 4503599627370496.0) - 4503599627370496.0;  /* 2^52: the addition rounds to an integer, half to even */
    return v < 0 ? -r : r;
}
static inline __attribute__((always_inline)) double hx_round(uint16_t fcw, double v)
{
    switch ((fcw >> 10) & 3u) { case 0: return hx_rint(v); case 1: return floor(v); case 2: return ceil(v); default: return trunc(v); }
}
/* x_shl32 with a constant count 1..31: the result and its flag record */
typedef struct { uint32_t kind, op1, op2, res, bits, cfo, cf, ofo, of; } hflags;
static inline __attribute__((always_inline)) void hf_set(hflags *f, uint32_t kind, uint32_t a, uint32_t b, uint32_t r, uint32_t bits)
{ f->kind = kind; f->op1 = a; f->op2 = b; f->res = r; f->bits = bits; f->cfo = 0; f->ofo = 0; }
static inline __attribute__((always_inline)) uint32_t hf_shl32(hflags *f, uint32_t v, uint32_t n)
{ uint32_t r = v << n; hf_set(f, XK_LOGIC, 0, 0, r, 32); f->cfo = 1; f->cf = (v >> (32u - n)) & 1u; f->ofo = 1; f->of = (r >> 31) ^ f->cf; return r; }
static inline __attribute__((always_inline)) void hf_load(hflags *f, const xctx *c)
{ f->kind = c->f_kind; f->op1 = c->f_op1; f->op2 = c->f_op2; f->res = c->f_res; f->bits = c->f_bits; f->cfo = c->f_cf_override; f->cf = c->f_cf; f->ofo = c->f_of_override; f->of = c->f_of; }
static inline __attribute__((always_inline)) void hf_store(const hflags *f, xctx *c)
{ c->f_kind = f->kind; c->f_op1 = f->op1; c->f_op2 = f->op2; c->f_res = f->res; c->f_bits = f->bits; c->f_cf_override = f->cfo; c->f_cf = f->cf; c->f_of_override = f->ofo; c->f_of = f->of; }
/* XF_C of a record (the carry an `inc` keeps) */
static inline __attribute__((always_inline)) uint32_t hf_cf(const hflags *f)
{
    if (f->kind == XK_EXPLICIT) return f->res & 1u;
    if (f->cfo) return f->cf;
    uint32_t mk = f->bits == 32 ? 0xFFFFFFFFu : ((1u << f->bits) - 1u), a = f->op1 & mk, b = f->op2 & mk, r = f->res & mk;
    switch (f->kind) { case XK_ADD: return r < a; case XK_ADC: return f->cf ? (r <= a) : (r < a); case XK_SUB: return a < b; case XK_SBB: return f->cf ? (a <= b) : (a < b); default: return 0; }
}
/* the slot st(i) at a function's depth d below its entry TOP: st[(fsp0 - d + i) & 7] */
#define HX_SLOT(fsp0, k) c->st[((fsp0) - (k)) & 7u]           /* the k-th push of the function (k >= 1) */
/* XV_HLE_CALL on the context (the registers must be in c) */
#define HX_HLE(addr, fn) do { XV_HLE_CALL(addr, fn); } while (0)
/* X_PREEMPT at the back-edge, with the context current (the caller stores/reloads its locals around it) */
#define HX_BACKEDGE(pre, SAVE, LOAD) do { if (--(pre) <= 0) { SAVE; c->preempt = (pre); xv_preempt(c); (pre) = c->preempt; LOAD; } } while (0)

/* ---- f_0007E530: vertex-shader constants c[-36..]: for each of n (int16 at [esi+4]) 52-byte records at [esi] - a
 * scale and a 3x3 matrix, a translation - the scaled rows (m[0][k] * s, m[1][k] * s, m[2][k] * s, t[k]) into 0x278258
 * (48 bytes a record), then SetVertexShaderConstant(-36, 0x278258, 3n). Loop: jl back-edge, X_PREEMPT; the
 * constant-pack prefix (XV_NATIVE_CONSTANT_PACK) is called where the lifted body calls it. ------------------------ */
static void hx_0007E530(xctx *restrict c, nx_journal *J)
{
    XV_PHASE_SCOPE(c, 26u);
    HM_INIT(J);
    uint32_t eax = c->r[0], ecx = c->r[1], edx = 0, esp = c->r[4];
    const uint32_t esi = c->r[6], edi0 = c->r[7], E = esp, fsp0 = c->fsp;
    int32_t pre = c->preempt;
    hflags f; hf_load(&f, c);
    double A = 0, B = 0; int touched = 0;
    uint16_t n16 = hm_r16(m, esi + 4u);
    hf_set(&f, XK_SUB, n16, 0u, (uint16_t)(n16 - 0u), 16);                         /* cmp [esi+4],dx */
    if ((int16_t)n16 > 0) {                                                         /* jle 7E5BA */
        esp -= 4u; hm_w32(m, esp, edi0);                                            /* push edi */
#ifdef XV_NATIVE_CONSTANT_PACK
        { extern int xv_constant_pack_prefix(xctx *);
          c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[4] = esp; c->r[7] = edi0; hf_store(&f, c); c->preempt = pre;
          (void)xv_constant_pack_prefix(c);
          eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; esp = c->r[4]; hf_load(&f, c); pre = c->preempt; }
#endif
        const hview Dv = hv_make(m, esi, 8u);
        for (;;) {                                                                  /* 7E540 */
            const uint32_t base = hv_r32(m, &Dv, esi);                              /* mov edi,[esi] */
            eax = (uint32_t)(int32_t)(int16_t)edx;                                  /* movsx eax,dx */
            { int64_t p = (int64_t)(int32_t)eax * 0x34; ecx = (uint32_t)p;          /* imul ecx,34h */
              hf_set(&f, XK_LOGIC, 0, 0, ecx, 32); f.cfo = f.ofo = 1; f.cf = f.of = (p != (int64_t)(int32_t)ecx); }
            const hview Sv = hv_make(m, ecx + base, 0x34u);
            A = hv_rf(m, &Sv, ecx + base);                                          /* fld [ecx+edi] */
            B = A * hv_rf(m, &Sv, ecx + base + 4u);                                 /* fld st(0); fmul [ecx+edi+4] */
            ecx += base;
            eax = eax * 3u;
            eax = hf_shl32(&f, eax, 4u) + 0x278258u;
            { uint32_t cf = hf_cf(&f); f.cfo = 1; f.cf = cf; } edx += 1u;             /* inc edx */
            const hview Tv = hv_make(m, eax, 0x30u);
            hv_wf(m, &Tv, eax, B);
            B = A * hv_rf(m, &Sv, ecx + 0x10u); hv_wf(m, &Tv, eax + 0x4u, B);
            B = A * hv_rf(m, &Sv, ecx + 0x1Cu); hv_wf(m, &Tv, eax + 0x8u, B);
            hv_w32(m, &Tv, eax + 0xCu, hv_r32(m, &Sv, ecx + 0x28u));
            B = A * hv_rf(m, &Sv, ecx + 0x8u);  hv_wf(m, &Tv, eax + 0x10u, B);
            B = A * hv_rf(m, &Sv, ecx + 0x14u); hv_wf(m, &Tv, eax + 0x14u, B);
            B = A * hv_rf(m, &Sv, ecx + 0x20u); hv_wf(m, &Tv, eax + 0x18u, B);
            hv_w32(m, &Tv, eax + 0x1Cu, hv_r32(m, &Sv, ecx + 0x2Cu));
            B = A * hv_rf(m, &Sv, ecx + 0xCu);  hv_wf(m, &Tv, eax + 0x20u, B);
            B = A * hv_rf(m, &Sv, ecx + 0x18u); hv_wf(m, &Tv, eax + 0x24u, B);
            A = A * hv_rf(m, &Sv, ecx + 0x24u); hv_wf(m, &Tv, eax + 0x28u, A);      /* fmul [ecx+24h] on st(0) = the scale's slot */
            ecx = hv_r32(m, &Sv, ecx + 0x30u); hv_w32(m, &Tv, eax + 0x2Cu, ecx);
            touched = 1;
            { uint16_t a = (uint16_t)edx, b = hv_r16(m, &Dv, esi + 4u); hf_set(&f, XK_SUB, a, b, (uint16_t)(a - b), 16);   /* cmp dx,[esi+4] */
              uint32_t sf = ((uint16_t)(a - b) >> 15) & 1u, of = (((a ^ b) & (a ^ (uint16_t)(a - b))) >> 15) & 1u;
              if (sf == of) break; }                                                 /* jl 7E540 */
            HX_BACKEDGE(pre, (c->r[0] = eax, c->r[1] = ecx, c->r[2] = edx, c->r[4] = esp, c->r[7] = base, hf_store(&f, c),
                              HX_SLOT(fsp0, 1) = A, HX_SLOT(fsp0, 2) = B), (void)0);
        }
        c->r[7] = hm_r32(m, esp); esp += 4u;                                          /* pop edi */
    } else c->r[7] = edi0;
    /* 7E5BA */
    eax = (uint32_t)(int32_t)(int16_t)hm_r16(m, esi + 4u);
    edx = eax * 3u;
    esp -= 4u; hm_w32(m, esp, edx); esp -= 4u; hm_w32(m, esp, 0x278258u); esp -= 4u; hm_w32(m, esp, 0xFFFFFFDCu);
    esp -= 4u; hm_w32(m, esp, 0x7E5CEu);
    if (touched) { HX_SLOT(fsp0, 1) = A; HX_SLOT(fsp0, 2) = B; }
    c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[4] = esp; hf_store(&f, c); c->preempt = pre;
    HX_HLE(0x183F90u, xv_hle_D3DDevice_SetVertexShaderConstant);
    c->r[4] += 4u;                                                                  /* ret */
    (void)E;
}

/* ---- f_00011B60: D3DCOLOR from four floats at [arg] (a r g b in [0,1]): each * 255 (the constant stored at [esp+4]
 * and loaded back), fistp under the control word, shifted and or-ed; eax = the colour. ret. esp must be 4-aligned
 * (the hook runs the lifted body otherwise: its stack slots then straddle pages with different translations). --- */
static void hx_00011B60(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4], fsp0 = c->fsp;
    const hview Fv = hv_make(m, E - 0x1Cu, 0x24u);
    hv_w32(m, &Fv, E - 0x1Cu, c->r[3]);                                               /* push ebx */
    hv_w32(m, &Fv, E - 0x18u, 0x437F0000u);                                           /* mov [esp+4],255.0 */
    const uint32_t p = hv_r32(m, &Fv, E + 4u);                                        /* mov edx,[esp+20h] */
    const hview Pv = hv_make(m, p, 16u);
    const double f0 = hv_rf(m, &Pv, p), f1 = hv_rf(m, &Pv, p + 4u), f2 = hv_rf(m, &Pv, p + 8u), f3 = hv_rf(m, &Pv, p + 0xCu), k = hv_rf(m, &Fv, E - 0x18u);
    const double s1 = f0 * k, s2 = f1 * k, s3 = f2 * k, s4 = f3 * k;               /* fmul st(4..1),st */
    const uint16_t cw = c->fcw;
    const uint32_t i3 = hx_i32(hx_round(cw, s4)), i2 = hx_i32(hx_round(cw, s3)), i1 = hx_i32(hx_round(cw, s2)), i0 = hx_i32(hx_round(cw, s1));
    hv_w32(m, &Fv, E - 0x8u, i3); hv_w32(m, &Fv, E - 0xCu, i2); hv_w32(m, &Fv, E - 0x10u, i1); hv_w32(m, &Fv, E - 0x14u, i0);   /* fistp x4 (esp 4-aligned) */
    hflags f;
    uint32_t ebx = hf_shl32(&f, i2, 8u), ecx = hf_shl32(&f, i1, 16u), eax = hf_shl32(&f, i0, 24u);
    uint32_t edx = i3 | ebx | ecx | eax;
    hv_w32(m, &Fv, E - 0x4u, edx);                                                    /* mov [esp+18h],edx; mov eax,[esp+18h] */
    HX_SLOT(fsp0, 1) = s1; HX_SLOT(fsp0, 2) = s2; HX_SLOT(fsp0, 3) = s3; HX_SLOT(fsp0, 4) = s4; HX_SLOT(fsp0, 5) = k;
    c->r[0] = edx; c->r[1] = ecx; c->r[2] = edx; c->r[4] = E + 4u;                  /* ebx: popped (its own slot) */
    hf_store(&f, c);
}

/* ---- f_00011610: D3DCOLOR from a float alpha (arg 1) and three floats at [arg 2]: each * 255, fistp; the low three
 * masked to a byte, all shifted into place through [esp] (read-modify-write, one slot); eax = the colour. ret.
 * esp 4-aligned (as 11B60). ----------------------------------------------------------------------------------- */
static void hx_00011610(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4], fsp0 = c->fsp;
    const hview Fv = hv_make(m, E - 8u, 0x14u);
    hv_w32(m, &Fv, E - 0x4u, 0x437F0000u);                                            /* mov [esp+4],255.0 */
    const uint32_t p = hv_r32(m, &Fv, E + 8u);                                        /* mov edx,[esp+10h] */
    const hview Pv = hv_make(m, p, 12u);
    const double a = hv_rf(m, &Fv, E + 4u), p0 = hv_rf(m, &Pv, p), p1 = hv_rf(m, &Pv, p + 4u), p2 = hv_rf(m, &Pv, p + 8u), k = hv_rf(m, &Fv, E - 0x4u);
    const double s1 = a * k, s2 = p0 * k, s3 = p1 * k, s4 = p2 * k;
    const uint16_t cw = c->fcw;
    const uint32_t v4 = hx_i32(hx_round(cw, s4)), v3 = hx_i32(hx_round(cw, s3)), v2 = hx_i32(hx_round(cw, s2)), v1 = hx_i32(hx_round(cw, s1));
    hflags f;
    uint32_t edx = v4 & 0xFFu;
    edx |= hf_shl32(&f, v3 & 0xFFu, 8u);
    edx |= hf_shl32(&f, v2 & 0xFFu, 16u);
    edx |= hf_shl32(&f, v1, 24u);
    hv_w32(m, &Fv, E - 0x8u, edx);                                                    /* the slot's final value */
    HX_SLOT(fsp0, 1) = s1; HX_SLOT(fsp0, 2) = s2; HX_SLOT(fsp0, 3) = s3; HX_SLOT(fsp0, 4) = s4; HX_SLOT(fsp0, 5) = k;
    c->r[0] = edx; c->r[2] = edx; c->r[4] = E + 4u;
    hf_store(&f, c);
}
/* ---- f_0007E420: vertex-shader constants c[-79..-69] for the two lights of a model (11 rows at [esp+8]):
 * [1E0B18] (a float) against [1F0A68]: not greater (or NaN) -> the rows from the lights: per light (int16 count at
 * [arg+40h] against the index) the light's constants through 7DBE0 (index -1: zeros), two 0x18-byte records from
 * [arg+0Ch]/[arg+10h] (else zeros), the three words at [arg]; greater -> zeros and the float three times. Then
 * SetVertexShaderConstant(-79, rows, 11). ret 4. Registers, flags, x87 slots, every stack word as the lifted body. */
typedef struct { uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; } hregs;
/* f_0007DBE0 (x87-regs body): eax = light index (-1: none), cx = slot, edx = rows; the slot's 48 bytes. ret. */
static inline __attribute__((always_inline)) void hx_7dbe0(xctx *restrict c, const hmem *m, const hview *Fv, hregs *R, hflags *f, const uint32_t fsp0,
                                                           double *x1, double *x2, int *xw)
{
    uint32_t eax = R->eax, ecx = R->ecx, edx = R->edx, esp = R->esp, esi = R->esi, edi = R->edi;
    hf_set(f, XK_SUB, eax, 0xFFFFFFFFu, eax + 1u, 32);                                  /* cmp eax,-1 */
    esp -= 4u; hv_w32(m, Fv, esp, edi);                                                 /* push edi */
    if (eax == 0xFFFFFFFFu) {                                                           /* je 7DC88 */
        eax = (uint32_t)(int32_t)(int16_t)ecx;
        ecx = hf_shl32(f, eax * 3u, 4u);
        edx += ecx;
        eax = 0; ecx = 0xCu; edi = edx;
        c->r[0] = eax; c->r[1] = ecx; c->r[7] = edi;                                   /* rep stosd: the runtime's own (df as the context has it) */
        if (m->J) { uint32_t lo = c->df ? edi - 44u : edi; for (unsigned k = 0; k < 12; ++k) nx_jlog_guest(m->J, lo + 4u * k, 4); }
        x_str_stos(c, 4, X_STR_REP);
        ecx = c->r[1]; edi = c->r[7];
        hv_w32(m, Fv, edx + 0x1Cu, eax); hv_w32(m, Fv, edx + 0x2Cu, 0x3F800000u);
        edi = hv_r32(m, Fv, esp); esp += 4u;                                            /* pop edi */
        R->eax = eax; R->ecx = ecx; R->edx = edx; R->esp = esp + 4u; R->edi = edi;      /* ret */
        return;
    }
    { int64_t p = (int64_t)(int32_t)eax * 0x38; eax = (uint32_t)p;                     /* imul eax,38h */
      hf_set(f, XK_LOGIC, 0, 0, eax, 32); f->cfo = f->ofo = 1; f->cf = f->of = (p != (int64_t)(int32_t)eax); }
    ecx = (uint32_t)(int32_t)(int16_t)ecx;
    esp -= 4u; hv_w32(m, Fv, esp, esi);                                                 /* push esi */
    eax += 0x2FCF64u;
    ecx = hf_shl32(f, ecx * 3u, 4u);
    ecx += edx;
    const hview Lv = hv_make(m, eax, 0x38u);
    edx = eax + 4u;
    edi = hv_r32(m, &Lv, edx); hv_w32(m, Fv, ecx, edi);
    edi = hv_r32(m, &Lv, edx + 4u); hv_w32(m, Fv, ecx + 4u, edi);
    edx = hv_r32(m, &Lv, edx + 8u); hv_w32(m, Fv, ecx + 8u, edx);
    edx = eax + 0x10u;
    double a = hv_rf(m, &Lv, eax + 0x34u);                                              /* slot 1 */
    esi = ecx + 0x10u;
    double b = hm_rf(m, 0x1F0A78u) / (a * a);                                           /* fld st(0); fmul st,st(1); fdivr [1F0A78]: slot 2 */
    hv_wf(m, Fv, ecx + 0xCu, b);
    edi = hv_r32(m, &Lv, edx); hv_w32(m, Fv, esi, edi);
    edi = hv_r32(m, &Lv, edx + 4u); hv_w32(m, Fv, esi + 4u, edi);
    edx = hv_r32(m, &Lv, edx + 8u); hv_w32(m, Fv, esi + 8u, edx);
    edx = eax + 0x28u;
    edi = hv_r32(m, &Lv, edx); esi = ecx + 0x20u; hv_w32(m, Fv, esi, edi);
    edi = hv_r32(m, &Lv, edx + 4u); hv_w32(m, Fv, esi + 4u, edi);
    edx = hv_r32(m, &Lv, edx + 8u); hv_w32(m, Fv, esi + 8u, edx);
    edx = hv_r32(m, &Lv, eax);
    { uint32_t v = hm_r32(m, edx + 0x1Cu); hf_set(f, XK_SUB, v, 0xBF800000u, v - 0xBF800000u, 32); }   /* cmp [edx+1Ch],-1.0 */
    esi = hv_r32(m, Fv, esp); esp += 4u;                                                /* pop esi */
    if (f->res != 0) {                                                                  /* jne: 7DC5D */
        double v = hm_rf(m, edx + 0x1Cu);
        edi = hv_r32(m, Fv, esp); esp += 4u;                                            /* pop edi */
        v = v - hm_rf(m, edx + 0x20u);
        v = hm_rf(m, 0x1F0A78u) / v;
        hv_wf(m, Fv, ecx + 0x1Cu, v);
        eax = hm_r32(m, eax);
        v = v * hm_rf(m, eax + 0x20u);
        v = -v;
        hv_wf(m, Fv, ecx + 0x2Cu, v);
        a = v;
    } else {                                                                            /* 7DC78 */
        hv_w32(m, Fv, ecx + 0x1Cu, 0); hv_w32(m, Fv, ecx + 0x2Cu, 0x3F800000u);
        edi = hv_r32(m, Fv, esp); esp += 4u;
    }
    *x1 = a; *x2 = b; *xw = 1;
    (void)fsp0;
    R->eax = eax; R->ecx = ecx; R->edx = edx; R->esp = esp + 4u; R->esi = esi; R->edi = edi;
}
static void hx_0007E420(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4], fsp0 = c->fsp;
    hregs R = { c->r[0], c->r[1], c->r[2], c->r[3], E, c->r[5], c->r[6], c->r[7] };
    const uint32_t ebp0 = R.ebp, edi0 = R.edi;
    int32_t pre = c->preempt;
    hflags f; hf_load(&f, c);
    double x1 = HX_SLOT(fsp0, 1), x2 = HX_SLOT(fsp0, 2); int xw = 0;
    uint16_t fsw = c->fsw;
    const hview Fv = hv_make(m, E - 0xCCu, 0xD4u);
    x1 = hm_rf(m, 0x1E0B18u); xw = 1;                                                   /* fld [1E0B18] */
    R.esp = E - 0xB0u;
    { double z = hm_rf(m, 0x1F0A68u); uint16_t cc = (x1 != x1 || z != z) ? 0x4500 : x1 < z ? 0x0100 : x1 == z ? 0x4000 : 0;   /* fcomp */
      fsw = (uint16_t)((fsw & ~0x4700u) | cc | (((fsp0 - 1u) & 7u) << 11)); }
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, R.ebp);                                               /* push ebp */
    R.ebp = hv_r32(m, &Fv, R.esp + 0xB8u);                                                   /* mov ebp,[esp+0B8h] */
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, R.edi);                                               /* push edi: E - 0xB8 */
    R.eax = (R.eax & 0xFFFF0000u) | fsw;                                                /* fnstsw ax */
    hf_set(&f, XK_LOGIC, 0, 0, (uint8_t)((fsw >> 8) & 0x41u), 8);                       /* test ah,41h */
    if (!((fsw >> 8) & 0x41u)) {                                                        /* greater: 7E442 */
        x1 = hm_rf(m, 0x1E0B18u);                                                       /* fld [1E0B18] */
        R.ecx = 0x2Cu;
        hf_set(&f, XK_LOGIC, R.eax, R.eax, 0, 32); R.eax = 0;                           /* xor eax,eax */
        R.edi = R.esp + 8u;
        c->r[0] = R.eax; c->r[1] = R.ecx; c->r[7] = R.edi;
        if (m->J) { uint32_t lo = c->df ? R.edi - 0xACu : R.edi; for (unsigned k = 0; k < 0x2C; ++k) nx_jlog_guest(m->J, lo + 4u * k, 4); }
        x_str_stos(c, 4, X_STR_REP);
        R.ecx = c->r[1]; R.edi = c->r[7];
        hv_wf(m, &Fv, R.esp + 0xB0u, x1); hv_wf(m, &Fv, R.esp + 0xACu, x1); hv_wf(m, &Fv, R.esp + 0xA8u, x1);   /* fst, fst, fstp */
    } else {                                                                            /* 7E46F */
        R.esp -= 4u; hv_w32(m, &Fv, R.esp, R.ebx);                                           /* push ebx */
        R.esp -= 4u; hv_w32(m, &Fv, R.esp, R.esi);                                           /* push esi: E - 0xC0 */
        R.esi = 0; R.edi = R.ebp + 0x44u;
        const hview Av = hv_make(m, R.ebp, 0x4Cu);
        for (;;) {                                                                      /* 7E476 */
            uint16_t n = hv_r16(m, &Av, R.ebp + 0x40u), si = (uint16_t)R.esi, d = (uint16_t)(n - si);
            hf_set(&f, XK_SUB, n, si, d, 16);                                           /* cmp [ebp+40h],si */
            uint32_t zf = d == 0, sf = d >> 15, of = (((n ^ si) & (n ^ d)) >> 15) & 1u;
            if (zf || sf != of) { hf_set(&f, XK_LOGIC, R.eax, 0xFFFFFFFFu, 0xFFFFFFFFu, 32); R.eax = 0xFFFFFFFFu; }   /* or eax,-1 */
            else R.eax = hv_r32(m, &Av, R.edi);
            R.edx = R.esp + 0x10u; R.ecx = R.esi;
            R.esp -= 4u; hv_w32(m, &Fv, R.esp, 0x7E48Eu);                                    /* call 7DBE0 */
            hx_7dbe0(c, m, &Fv, &R, &f, fsp0, &x1, &x2, &xw);
            { uint32_t cf = hf_cf(&f); f.cfo = 1; f.cf = cf; } R.esi += 1u;              /* inc esi */
            R.edi += 4u;
            { uint16_t a = (uint16_t)R.esi, dd = (uint16_t)(a - 2u); hf_set(&f, XK_SUB, a, 2u, dd, 16);   /* cmp si,2 */
              uint32_t sf2 = dd >> 15, of2 = (((a ^ 2u) & (a ^ dd)) >> 15) & 1u;
              if (sf2 == of2) break; }
            HX_BACKEDGE(pre, (c->r[0] = R.eax, c->r[1] = R.ecx, c->r[2] = R.edx, c->r[3] = R.ebx, c->r[4] = R.esp, c->r[5] = R.ebp, c->r[6] = R.esi, c->r[7] = R.edi,
                              hf_store(&f, c), c->fsw = fsw, xw ? (HX_SLOT(fsp0, 1) = x1, HX_SLOT(fsp0, 2) = x2) : 0),
                        (R.eax = c->r[0], R.ecx = c->r[1], R.edx = c->r[2], R.ebx = c->r[3], R.esp = c->r[4], R.ebp = c->r[5], R.esi = c->r[6], R.edi = c->r[7]));
        }
        R.edx = 0; R.ebx = R.esp + 0x70u; R.esi = R.ebp + 0x10u;                        /* 7E498 */
        for (;;) {                                                                      /* 7E4A1 */
            uint16_t n = hv_r16(m, &Av, R.ebp + 0xCu), dx = (uint16_t)R.edx, d = (uint16_t)(n - dx);
            hf_set(&f, XK_SUB, n, dx, d, 16);                                           /* cmp [ebp+0Ch],dx */
            uint32_t zf = d == 0, sf = d >> 15, of = (((n ^ dx) & (n ^ d)) >> 15) & 1u;
            int copy = 0;
            if (!(zf || sf != of)) { hf_set(&f, XK_LOGIC, 0, 0, R.esi, 32); copy = R.esi != 0; }   /* test esi,esi */
            if (copy) {
                R.eax = R.esi + 0xCu;
                R.edi = hv_r32(m, &Av, R.eax); R.ecx = R.ebx; hv_w32(m, &Fv, R.ecx, R.edi);
                R.edi = hv_r32(m, &Av, R.eax + 4u); R.eax = hv_r32(m, &Av, R.eax + 8u); hv_w32(m, &Fv, R.ecx + 4u, R.edi); hv_w32(m, &Fv, R.ecx + 8u, R.eax);
                R.eax = R.esi;
                R.edi = hv_r32(m, &Av, R.eax); R.ecx = R.ebx + 0x10u; hv_w32(m, &Fv, R.ecx, R.edi);
                R.edi = hv_r32(m, &Av, R.eax + 4u); R.eax = hv_r32(m, &Av, R.eax + 8u); hv_w32(m, &Fv, R.ecx + 4u, R.edi); hv_w32(m, &Fv, R.ecx + 8u, R.eax);
            } else {                                                                    /* 7E4D7 */
                R.ecx = 8u;
                hf_set(&f, XK_LOGIC, R.eax, R.eax, 0, 32); R.eax = 0;                   /* xor eax,eax */
                R.edi = R.ebx;
                c->r[0] = R.eax; c->r[1] = R.ecx; c->r[7] = R.edi;
                if (m->J) { uint32_t lo = c->df ? R.edi - 28u : R.edi; for (unsigned k = 0; k < 8; ++k) nx_jlog_guest(m->J, lo + 4u * k, 4); }
                x_str_stos(c, 4, X_STR_REP);
                R.ecx = c->r[1]; R.edi = c->r[7];
            }
            { uint32_t cf = hf_cf(&f); f.cfo = 1; f.cf = cf; } R.edx += 1u;              /* 7E4E2: inc edx */
            R.esi += 0x18u; R.ebx += 0x20u;
            { uint16_t a = (uint16_t)R.edx, dd = (uint16_t)(a - 2u); hf_set(&f, XK_SUB, a, 2u, dd, 16);
              uint32_t sf2 = dd >> 15, of2 = (((a ^ 2u) & (a ^ dd)) >> 15) & 1u;
              if (sf2 == of2) break; }
            HX_BACKEDGE(pre, (c->r[0] = R.eax, c->r[1] = R.ecx, c->r[2] = R.edx, c->r[3] = R.ebx, c->r[4] = R.esp, c->r[5] = R.ebp, c->r[6] = R.esi, c->r[7] = R.edi,
                              hf_store(&f, c), c->fsw = fsw, xw ? (HX_SLOT(fsp0, 1) = x1, HX_SLOT(fsp0, 2) = x2) : 0),
                        (R.eax = c->r[0], R.ecx = c->r[1], R.edx = c->r[2], R.ebx = c->r[3], R.esp = c->r[4], R.ebp = c->r[5], R.esi = c->r[6], R.edi = c->r[7]));
        }
        R.ecx = hv_r32(m, &Av, R.ebp); R.edx = hv_r32(m, &Av, R.ebp + 4u); R.eax = hv_r32(m, &Av, R.ebp + 8u);   /* 7E4EF */
        R.esi = hv_r32(m, &Fv, R.esp); R.esp += 4u;                                     /* pop esi */
        hv_w32(m, &Fv, R.esp + 0xACu, R.ecx); hv_w32(m, &Fv, R.esp + 0xB0u, R.edx); hv_w32(m, &Fv, R.esp + 0xB4u, R.eax);
        R.ebx = hv_r32(m, &Fv, R.esp); R.esp += 4u;                                     /* pop ebx */
    }
    /* 7E50F */
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, 0xBu);
    R.ecx = R.esp + 0xCu;
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, R.ecx);
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, 0xFFFFFFB1u);
    R.esp -= 4u; hv_w32(m, &Fv, R.esp, 0x7E51Du);
    c->r[0] = R.eax; c->r[1] = R.ecx; c->r[2] = R.edx; c->r[3] = R.ebx; c->r[4] = R.esp; c->r[5] = R.ebp; c->r[6] = R.esi; c->r[7] = R.edi;
    hf_store(&f, c); c->fsw = fsw; c->preempt = pre;
    if (xw) { HX_SLOT(fsp0, 1) = x1; HX_SLOT(fsp0, 2) = x2; }
    HX_HLE(0x183F90u, xv_hle_D3DDevice_SetVertexShaderConstant);
    c->r[7] = hv_r32(m, &Fv, c->r[4]); c->r[4] += 4u;                                 /* pop edi */
    c->r[5] = hv_r32(m, &Fv, c->r[4]); c->r[4] += 4u;                                 /* pop ebp */
    c->r[4] += 0xB0u + 8u;                                                              /* add esp,0B0h; ret 4 */
    (void)ebp0; (void)edi0;
}

/* ---- f_00056F20: texture-coordinate animation of a shader map (esi: the map's animation block, three function
 * records of 0x10 bytes; ecx: the owner's function values or 0; stack: six floats; ebx, edi: the two output rows).
 * Per axis the period ([esi+4/14/24h], 1.0 when it equals [1F0A68]) and the owner's scale, then three calls of the
 * periodic function 173F20 (the guest's: it stays lifted) on (time + phase) / period, the rotation (cos/sin of the
 * third, 1/0 when it equals [1F0A68]) and the two output rows. ret 18h.
 * x87: the logical slots 1..4 (st[(base - k) & 7]) live in locals, written back before each call and at the exit
 * and reloaded after each call, as the stage's x87-regs body does; when 173F20 returns with another depth than one
 * push, the body calls xv_x87reg_miss and continues in its memory lowering: here the base moves so that the depth
 * matches the context's TOP, which is the memory lowering's addressing. ------------------------------------------ */
static void hx_00056F20(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4]; uint32_t S = E - 0x14u, esi = c->r[6], ebx = c->r[3], edi = c->r[7];
    uint32_t eax = c->r[0], ecx = c->r[1], edx = c->r[2], base = c->fsp;
    uint16_t fsw = c->fsw;
    hflags f; hf_load(&f, c);
    double x1 = c->st[(base - 1u) & 7u], x2 = c->st[(base - 2u) & 7u], x3 = c->st[(base - 3u) & 7u], x4 = c->st[(base - 4u) & 7u];
    /* views: the frame from the pushes of the calls to the six arguments, the animation block, the constants page */
    hview F = hv_make(m, S - 12u, 0x48u), A = hv_make(m, esi, 0x38u);
    const hview K = hv_make(m, 0x1F0A68u, 0xDCu);
#define N56_CMP(v, depth) do { const double z_ = hv_rf(m, &K, 0x1F0A68u); \
        fsw = (uint16_t)((fsw & ~0x4700u) | hx_cc((v), z_) | (((base - (depth)) & 7u) << 11)); } while (0)
#define N56_TESTAH44() do { eax = (eax & 0xFFFF0000u) | fsw; hf_set(&f, XK_LOGIC, 0, 0, (uint8_t)((fsw >> 8) & 0x44u), 8); } while (0)
#define N56_PF() (((fsw >> 8) & 0x44u) != 0x40u && ((fsw >> 8) & 0x44u) != 0x04u)     /* parity of ah & 44h: even */
#define N56_SYNC_OUT(d) (c->st[(base - 1u) & 7u] = x1, c->st[(base - 2u) & 7u] = x2, c->st[(base - 3u) & 7u] = x3, c->st[(base - 4u) & 7u] = x4, \
                         c->fsp = (base - (d)) & 7u, c->fsw = fsw)
#define N56_CALL(ret, ip) do { hv_w32(m, &F, S - 12u, (ret)); \
        c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = S - 12u; c->r[6] = esi; c->r[7] = edi; hf_store(&f, c); N56_SYNC_OUT(0u); \
        f_00173F20(c); \
        if (__builtin_expect(c->fsp != ((base - 1u) & 7u), 0)) { if (xv_x87reg_miss) xv_x87reg_miss(c, (ip)); base = (c->fsp + 1u) & 7u; } \
        x1 = c->st[(base - 1u) & 7u]; x2 = c->st[(base - 2u) & 7u]; x3 = c->st[(base - 3u) & 7u]; x4 = c->st[(base - 4u) & 7u]; fsw = c->fsw; \
        eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; S = c->r[4]; esi = c->r[6]; edi = c->r[7]; hf_load(&f, c); \
        F = hv_make(m, S - 12u, 0x48u); A = hv_make(m, esi, 0x38u); } while (0)
    /* the three periods */
    x1 = hv_rf(m, &A, esi + 4u);                                                        /* fld [esi+4] */
    N56_CMP(x1, 1u); N56_TESTAH44();                                                    /* fcomp [1F0A68]; fnstsw ax; test ah,44h */
    x1 = N56_PF() ? hv_rf(m, &A, esi + 4u) : hv_rf(m, &K, 0x1F0A78u);                   /* jp: the period, else 1.0 (slot 1) */
    x2 = hv_rf(m, &A, esi + 0x14u);
    N56_CMP(x2, 2u); N56_TESTAH44();
    if (N56_PF()) { eax = hv_r32(m, &A, esi + 0x14u); hv_w32(m, &F, S + 0xCu, eax); } else hv_w32(m, &F, S + 0xCu, 0x3F800000u);
    x2 = hv_rf(m, &A, esi + 0x24u);
    N56_CMP(x2, 2u); N56_TESTAH44();
    if (N56_PF()) { edx = hv_r32(m, &A, esi + 0x24u); hv_w32(m, &F, S + 0x10u, edx); } else hv_w32(m, &F, S + 0x10u, 0x3F800000u);
    /* the owner's values: [ecx+4] + (index - 1) * 4 per axis, 1.0 without an owner or an index */
    hf_set(&f, XK_LOGIC, 0, 0, ecx, 32);                                                /* test ecx,ecx */
    if (ecx) {
        uint16_t ax = hv_r16(m, &A, esi); eax = (eax & 0xFFFF0000u) | ax;
        hf_set(&f, XK_LOGIC, 0, 0, ax, 16);                                             /* test ax,ax */
        ecx = hm_r32(m, ecx + 4u);
        if (ax) { eax = (uint32_t)(int32_t)(int16_t)ax; x2 = hm_rf(m, ecx + eax * 4u - 4u); hv_wf(m, &F, S + 4u, x2); }
        else hv_w32(m, &F, S + 4u, 0x3F800000u);
        ax = hv_r16(m, &A, esi + 0x10u); eax = (eax & 0xFFFF0000u) | ax;
        hf_set(&f, XK_LOGIC, 0, 0, ax, 16);
        if (ax) { edx = (uint32_t)(int32_t)(int16_t)ax; x2 = hm_rf(m, ecx + edx * 4u - 4u); hv_wf(m, &F, S, x2); }
        else hv_w32(m, &F, S, 0x3F800000u);
        ax = hv_r16(m, &A, esi + 0x20u); eax = (eax & 0xFFFF0000u) | ax;
        hf_set(&f, XK_LOGIC, 0, 0, ax, 16);
        if (ax) { eax = (uint32_t)(int32_t)(int16_t)ax; x2 = hm_rf(m, ecx + eax * 4u - 4u); hv_wf(m, &F, S + 8u, x2); }
        else hv_w32(m, &F, S + 8u, 0x3F800000u);
    } else {
        hv_w32(m, &F, S, 0x3F800000u); hv_w32(m, &F, S + 4u, 0x3F800000u); hv_w32(m, &F, S + 8u, 0x3F800000u);
    }
    /* 56FF0: first axis */
    x2 = hv_rf(m, &F, S + 0x2Cu);                                                       /* fld [esp+2Ch] (depth 2) */
    hv_w32(m, &F, S - 4u, ecx);                                                         /* push ecx */
    x2 = x2 + hv_rf(m, &A, esi + 8u);
    ecx = hv_r16(m, &A, esi + 2u);                                                      /* xor ecx,ecx; mov cx,[esi+2] */
    x2 = x2 / x1;
    hv_wf(m, &F, S - 4u, x2);                                                           /* fstp [esp] */
    hv_w32(m, &F, S - 8u, ecx);                                                         /* push ecx; fstp st(0) (depth 0) */
    N56_CALL(0x5700Bu, 0x57006u);
    /* second axis (depth 1: the first function value) */
    x1 = x1 * hv_rf(m, &A, esi + 0xCu);
    edx = hv_r16(m, &A, esi + 0x12u);                                                   /* xor edx,edx; mov dx,[esi+12h] */
    hv_w32(m, &F, S - 4u, ecx);                                                         /* push ecx */
    x1 = x1 * hv_rf(m, &F, S + 4u);                                                     /* fmul [esp+8] */
    hv_wf(m, &F, S + 4u, x1);                                                           /* fstp [esp+8] (depth 0) */
    x1 = hv_rf(m, &F, S + 0x2Cu);                                                       /* fld [esp+30h] */
    x1 = x1 + hv_rf(m, &A, esi + 0x18u);
    x1 = x1 / hv_rf(m, &F, S + 0xCu);
    hv_wf(m, &F, S - 4u, x1);                                                           /* fstp [esp] */
    hv_w32(m, &F, S - 8u, edx);                                                         /* push edx */
    N56_CALL(0x57031u, 0x5702Cu);
    /* third axis */
    x1 = x1 * hv_rf(m, &A, esi + 0x1Cu);
    eax = hv_r16(m, &A, esi + 0x22u);                                                   /* xor eax,eax; mov ax,[esi+22h] */
    x1 = x1 * hv_rf(m, &F, S);                                                          /* fmul [esp] */
    hv_w32(m, &F, S - 4u, ecx);                                                         /* push ecx */
    hv_wf(m, &F, S, x1);                                                                /* fstp [esp+4] */
    x1 = hv_rf(m, &F, S + 0x2Cu);                                                       /* fld [esp+30h] */
    x1 = x1 + hv_rf(m, &A, esi + 0x28u);
    x1 = x1 / hv_rf(m, &F, S + 0x10u);                                                  /* fdiv [esp+14h] */
    hv_wf(m, &F, S - 4u, x1);
    hv_w32(m, &F, S - 8u, eax);                                                         /* push eax */
    N56_CALL(0x57057u, 0x57052u);
    /* the rotation (depth 1) */
    x1 = x1 * hv_rf(m, &A, esi + 0x2Cu);
    x1 = x1 * hv_rf(m, &F, S + 8u);                                                     /* fmul [esp+8] */
    x2 = hv_rf(m, &F, S + 0x20u);                                                       /* fld [esp+20h] (depth 2) */
    x2 = x2 - hv_rf(m, &A, esi + 0x30u);
    x2 = x2 + hv_rf(m, &F, S + 4u);
    hv_wf(m, &F, S + 4u, x2);                                                           /* fstp [esp+4] */
    x2 = hv_rf(m, &F, S + 0x24u);
    x2 = x2 - hv_rf(m, &A, esi + 0x34u);
    x2 = x2 + hv_rf(m, &F, S);
    hv_wf(m, &F, S, x2);                                                                /* fstp [esp] */
    x1 = x1 + hv_rf(m, &F, S + 0x28u);                                                  /* fadd [esp+28h] */
    N56_CMP(x1, 1u); N56_TESTAH44();                                                    /* fcom [1F0A68]; fnstsw; test ah,44h */
    if (N56_PF()) {                                                                     /* jnp 570A1 not taken */
        x1 = x1 * hv_rf(m, &K, 0x1F0B40u);
        hv_wf(m, &F, S + 0x2Cu, x1);                                                    /* fst [esp+2Ch] */
        x1 = cos(x1);                                                                   /* fcos */
        x2 = sin(hv_rf(m, &F, S + 0x2Cu));                                              /* fld [esp+2Ch]; fsin */
    } else {                                                                            /* 570A1: fstp st(0); fld 1.0; fld [1F0A68] */
        x1 = hv_rf(m, &K, 0x1F0A78u);
        x2 = hv_rf(m, &K, 0x1F0A68u);
    }
    /* 570AF: the two rows (depth 2: x1 = cos, x2 = sin) */
    const hview B = hv_make(m, ebx, 0x10u), D = hv_make(m, edi, 0x10u);
    hv_w32(m, &B, ebx + 8u, 0);
    x3 = x1 * hv_rf(m, &F, S + 0x18u);                                                  /* fld st(1); fmul [esp+18h] */
    hv_wf(m, &B, ebx, x3);
    x3 = hv_rf(m, &F, S + 0x1Cu) * x2;                                                  /* fld [esp+1Ch]; fmul st,st(1) */
    x3 = -x3;
    hv_wf(m, &B, ebx + 4u, x3);
    x3 = x1 * hv_rf(m, &F, S + 4u);                                                     /* fld st(1); fmul [esp+4] */
    x4 = x2 * hv_rf(m, &F, S);                                                          /* fld st(1); fmul [esp] */
    x3 = x3 - x4;                                                                       /* fsubp st(1),st */
    x3 = x3 + hv_rf(m, &A, esi + 0x30u);
    hv_wf(m, &B, ebx + 0xCu, x3);
    hv_w32(m, &D, edi + 8u, 0);
    x3 = hv_rf(m, &F, S + 0x18u) * x2;                                                  /* fld [esp+18h]; fmul st,st(1) */
    hv_wf(m, &D, edi, x3);
    x3 = x1 * hv_rf(m, &F, S + 0x1Cu);                                                  /* fld st(1); fmul [esp+1Ch] */
    hv_wf(m, &D, edi + 4u, x3);
    { double t = x2; x2 = x1; x1 = t; }                                                 /* fxch */
    x2 = x2 * hv_rf(m, &F, S);
    { double t = x2; x2 = x1; x1 = t; }                                                 /* fxch */
    x2 = x2 * hv_rf(m, &F, S + 4u);
    x1 = x1 + x2;                                                                       /* faddp st(1),st */
    x1 = x1 + hv_rf(m, &A, esi + 0x34u);
    hv_wf(m, &D, edi + 0xCu, x1);                                                       /* fstp (depth 0) */
    c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = S + 0x30u; c->r[6] = esi; c->r[7] = edi; hf_store(&f, c); N56_SYNC_OUT(0u);
#undef N56_CMP
#undef N56_TESTAH44
#undef N56_PF
#undef N56_SYNC_OUT
#undef N56_CALL
    (void)E;
}

/* ---- f_00080250: bind a bitmap to a stage (eax: the bitmap or 0; arg: the stage): 325C0 (the texture cache, the
 * guest's own body) for the texture, then SetTexture(stage, texture); al = 1. ret 4. ------------------------------ */
static void hx_00080250(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4], eax = c->r[0];
    hflags f; hf_load(&f, c);
    hf_set(&f, XK_LOGIC, 0, 0, eax, 32);                                                /* test eax,eax */
    if (eax) {
        hm_w32(m, E - 4u, 1u); hm_w32(m, E - 8u, 1u); hm_w32(m, E - 12u, 0x8025Du);      /* push 1; push 1; call 325C0 */
        c->r[4] = E - 12u; hf_store(&f, c);
        f_000325C0(c);
        uint32_t esp = c->r[4];
        esp -= 4u; hm_w32(m, esp, c->r[0]);                                             /* push eax */
        c->r[0] = (uint32_t)(int32_t)(int16_t)hm_r16(m, esp + 8u);                       /* movsx eax,word ptr [esp+8] */
        esp -= 4u; hm_w32(m, esp, c->r[0]);
        esp -= 4u; hm_w32(m, esp, 0x80269u);
        c->r[4] = esp;
        HX_HLE(0x181950u, xv_hle_D3DDevice_SetTexture);
        c->r[0] = (c->r[0] & 0xFFFFFF00u) | 1u;                                         /* mov al,1 */
        c->r[4] += 8u;                                                                  /* ret 4 */
        return;
    }
    hf_store(&f, c);
    c->r[0] = (eax & 0xFFFFFF00u) | 1u; c->r[4] = E + 8u;
}

/* ---- f_00080360: a shader map's bitmap for a stage (ecx: the bitmap tag or -1; args: stage, bitmap index, sequence
 * index, frame): the animated bitmap (1290E0 inline: the tag's bitmap group, [+60h] bitmaps of 48 bytes at [+64h];
 * frame modulo the count) when its sequence matches, through 80250; else the default bitmap of the sequence from the
 * shader globals [2E3608]. The bitmap's width/height words to [278A9C]/[278A9E]; eax = 278A9C or 0. ret 10h. ---- */
static void hx_00080360(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4];
    uint32_t eax = c->r[0], ecx = c->r[1], edx = c->r[2], ebx = c->r[3], ebp = c->r[5], esi = c->r[6], edi = c->r[7];
    hflags f; hf_load(&f, c);
    hm_w32(m, E - 4u, ebx); hm_w32(m, E - 8u, ebp);                                     /* push ebx; push ebp */
    ebp = (ebp & 0xFFFF0000u) | hm_r16(m, E + 8u);                                      /* mov bp,[esp+10h] */
    hm_w32(m, E - 12u, esi);                                                            /* push esi */
    ebx &= 0xFFFFFF00u;                                                                 /* xor bl,bl */
    hf_set(&f, XK_SUB, ecx, 0xFFFFFFFFu, ecx + 1u, 32);                                 /* cmp ecx,-1 */
    hm_w32(m, E - 16u, edi);                                                            /* push edi */
    uint32_t esp = E - 16u;
    edi = hm_rimg32(m, 0x39CE24u);                                                      /* mov edi,[39CE24] */
    int found = 0;
    if (ecx != 0xFFFFFFFFu) {                                                           /* 80376 */
        eax = hf_shl32(&f, ecx & 0xFFFFu, 5u);
        eax = hm_r32(m, eax + edi + 0x14u);
        esi = hm_r32(m, eax + 0x60u);
        hf_set(&f, XK_LOGIC, 0, 0, esi, 32);                                            /* test esi,esi; jle */
        if ((int32_t)esi > 0) {
            eax = (uint32_t)(int32_t)(int16_t)hm_r16(m, esp + 0x20u);                    /* movsx eax,word ptr [esp+20h] */
            edx = (int32_t)eax < 0 ? 0xFFFFFFFFu : 0u;                                   /* cdq */
            { int32_t q = (int32_t)eax / (int32_t)esi, r = (int32_t)eax % (int32_t)esi; eax = (uint32_t)q; edx = (uint32_t)r; }   /* idiv esi (esi > 0) */
            eax = ecx;
            esp -= 4u; hm_w32(m, esp, 0x8039Au);                                        /* call 1290E0 */
            {   uint32_t cx = hm_rimg32(m, 0x39CE24u);                                  /* 1290E0 */
                eax = hf_shl32(&f, eax & 0xFFFFu, 5u);
                cx = hm_r32(m, eax + cx + 0x14u);
                eax = 0; ecx = cx;
                hf_set(&f, XK_LOGIC, 0, 0, ecx, 32);                                    /* test ecx,ecx */
                if (ecx) {
                    uint16_t dx = (uint16_t)edx; hf_set(&f, XK_LOGIC, 0, 0, dx, 16);     /* test dx,dx */
                    if (!(dx & 0x8000u)) {                                              /* jl */
                        hm_w32(m, esp - 4u, esi);                                       /* push esi */
                        uint32_t n = hm_r32(m, ecx + 0x60u);
                        edx = (uint32_t)(int32_t)(int16_t)dx;
                        hf_set(&f, XK_SUB, edx, n, edx - n, 32);                        /* cmp edx,esi */
                        esi = hm_r32(m, esp - 4u);                                      /* pop esi */
                        uint32_t d = edx - n, sf = d >> 31, of = ((edx ^ n) & (edx ^ d)) >> 31;
                        if (sf != of) {                                                 /* jge */
                            eax = edx * 3u; edx = hm_r32(m, ecx + 0x64u);
                            eax = hf_shl32(&f, eax, 4u) + edx;
                        }
                    }
                }
                esp += 4u;                                                              /* ret */
            }
            esi = eax;
            { uint16_t a = hm_r16(m, esi + 0xAu), b = (uint16_t)ebp; hf_set(&f, XK_SUB, a, b, (uint16_t)(a - b), 16);   /* cmp [esi+0Ah],bp */
              if (a == b) {                                                             /* 803A2 */
                  ecx = hm_r32(m, esp + 0x14u);
                  esp -= 4u; hm_w32(m, esp, ecx);                                       /* push ecx */
                  esp -= 4u; hm_w32(m, esp, 0x803ACu);                                  /* call 80250 */
                  c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = esp; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi; hf_store(&f, c);
                  hx_00080250(c, J);
                  eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; esp = c->r[4]; ebp = c->r[5]; esi = c->r[6]; edi = c->r[7]; hf_load(&f, c);
                  edx = (edx & 0xFFFF0000u) | hm_r16(m, esi + 4u); hm_wimg16(m, 0x278A9Cu, (uint16_t)edx);
                  eax = (eax & 0xFFFF0000u) | hm_r16(m, esi + 6u); hm_wimg16(m, 0x278A9Eu, (uint16_t)eax);
                  found = 1;
              } }
        }
    }
    if (!found) {                                                                       /* 803C3 */
        edx = hm_rimg32(m, 0x2E3608u);
        ecx = hf_shl32(&f, (uint32_t)(int32_t)(int16_t)ebp, 4u);
        eax = hm_r32(m, ecx + edx + 0xB8u);
        hf_set(&f, XK_SUB, eax, 0xFFFFFFFFu, eax + 1u, 32);                             /* cmp eax,-1 */
        if (eax != 0xFFFFFFFFu) {
            eax = hf_shl32(&f, eax & 0xFFFFu, 5u);
            eax = hm_r32(m, eax + edi + 0x14u);
            hf_set(&f, XK_LOGIC, 0, 0, eax, 32);                                        /* test eax,eax */
            if (eax) {
                ecx = (ecx & 0xFFFF0000u) | hm_r16(m, esp + 0x1Cu);                     /* mov cx,[esp+1Ch] */
                hf_set(&f, XK_LOGIC, 0, 0, (uint16_t)ecx, 16);                          /* test cx,cx; jl */
                if (!(ecx & 0x8000u)) {
                    edx = hm_r32(m, eax + 0x60u);
                    ecx = (uint32_t)(int32_t)(int16_t)ecx;
                    hf_set(&f, XK_SUB, ecx, edx, ecx - edx, 32);                        /* cmp ecx,edx; jge */
                    uint32_t d = ecx - edx, sf = d >> 31, of = ((ecx ^ edx) & (ecx ^ d)) >> 31;
                    if (sf != of) {
                        esi = ecx * 3u; ecx = hm_r32(m, eax + 0x64u);
                        esi = hf_shl32(&f, esi, 4u);
                        { uint32_t r = esi + ecx; hf_set(&f, XK_ADD, esi, ecx, r, 32); esi = r; }   /* add esi,ecx; je */
                        if (esi) {
                            esp -= 4u; hm_w32(m, esp, 1u); esp -= 4u; hm_w32(m, esp, 1u);
                            eax = esi;
                            esp -= 4u; hm_w32(m, esp, 0x80417u);                        /* call 325C0 */
                            c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = esp; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi; hf_store(&f, c);
                            f_000325C0(c);
                            eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; esp = c->r[4]; ebp = c->r[5]; esi = c->r[6]; edi = c->r[7]; hf_load(&f, c);
                            esp -= 4u; hm_w32(m, esp, eax);                             /* push eax */
                            eax = (uint32_t)(int32_t)(int16_t)hm_r16(m, esp + 0x18u);   /* movsx eax,word ptr [esp+18h] */
                            esp -= 4u; hm_w32(m, esp, eax);
                            esp -= 4u; hm_w32(m, esp, 0x80423u);
                            c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = esp; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi; hf_store(&f, c);
                            HX_HLE(0x181950u, xv_hle_D3DDevice_SetTexture);
                            eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; esp = c->r[4]; ebp = c->r[5]; esi = c->r[6]; edi = c->r[7];
                            ecx = (ecx & 0xFFFF0000u) | hm_r16(m, esi + 4u); hm_wimg16(m, 0x278A9Cu, (uint16_t)ecx);
                            edx = (edx & 0xFFFF0000u) | hm_r16(m, esi + 6u); hm_wimg16(m, 0x278A9Eu, (uint16_t)edx);
                            found = 1;
                        }
                    }
                }
            }
        }
    }
    if (found) ebx = (ebx & 0xFFFFFF00u) | 1u;                                          /* 80439: mov bl,1 */
    { uint8_t bl = (uint8_t)ebx, r = (uint8_t)(0u - bl); hf_set(&f, XK_SUB, 0, bl, r, 8); ebx = (ebx & 0xFFFFFF00u) | r; }   /* neg bl */
    edi = hm_r32(m, esp); esi = hm_r32(m, esp + 4u); ebp = hm_r32(m, esp + 8u); esp += 12u;   /* pop edi; pop esi; pop ebp */
    { uint32_t cf = hf_cf(&f), r = ebx - ebx - cf; hf_set(&f, XK_SBB, ebx, ebx, r, 32); f.cf = cf; ebx = r; }   /* sbb ebx,ebx */
    ebx &= 0x278A9Cu;
    eax = ebx;
    ebx = hm_r32(m, esp); esp += 4u;                                                    /* pop ebx */
    c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = esp + 0x14u; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi;
    hf_store(&f, c);
}

/* ---- f_00011BD0: D3DCOLOR (alpha 0) from three floats at [arg] (r g b): each * 255, fistp; masked, shifted and or-ed
 * through [esp] (one slot); eax = the colour. ret. esp 4-aligned (as 11B60). ------------------------------------- */
static void hx_00011BD0(xctx *restrict c, nx_journal *J)
{
    HM_INIT(J);
    const uint32_t E = c->r[4], fsp0 = c->fsp;
    const hview Fv = hv_make(m, E - 8u, 0x10u);
    hv_w32(m, &Fv, E - 0x4u, 0x437F0000u);                                            /* mov [esp+4],255.0 */
    const uint32_t p = hv_r32(m, &Fv, E + 4u);                                        /* mov edx,[esp+0Ch] */
    const hview Pv = hv_make(m, p, 12u);
    const double p0 = hv_rf(m, &Pv, p), p1 = hv_rf(m, &Pv, p + 4u), p2 = hv_rf(m, &Pv, p + 8u), k = hv_rf(m, &Fv, E - 0x4u);
    const double s1 = p0 * k, s2 = p1 * k, s3 = p2 * k;                               /* fmul st(3),st; fmul st(2),st; fmulp */
    const uint16_t cw = c->fcw;
    const uint32_t v3 = hx_i32(hx_round(cw, s3)), v2 = hx_i32(hx_round(cw, s2)), v1 = hx_i32(hx_round(cw, s1));
    hflags f; hf_load(&f, c);
    uint32_t edx = v3 & 0xFFu;
    edx |= hf_shl32(&f, v2 & 0xFFu, 8u);
    edx |= hf_shl32(&f, v1 & 0xFFu, 16u);
    hv_w32(m, &Fv, E - 0x8u, edx);                                                    /* the slot's final value */
    HX_SLOT(fsp0, 1) = s1; HX_SLOT(fsp0, 2) = s2; HX_SLOT(fsp0, 3) = s3; HX_SLOT(fsp0, 4) = k;
    c->r[0] = edx; c->r[2] = edx; c->r[4] = E + 4u;
    hf_store(&f, c);
}

/* ---- modes, counters ------------------------------------------------------------------------------------------ */
enum { NXC_CALLS, NXC_NATIVE, NXC_VERIFIED, NXC_MISMATCHED, NXC_SKIPPED, NXC_DECLINED, NXC_JOVERFLOW, NXC_DIVERGED, NXC_TGUEST, NXC_TNATIVE, NXC_N };
typedef struct {
    uint32_t addr; const char *name;
    void (*body)(xctx *restrict);                 /* the lifted body (f_XXXXXXXX_body behind the hook) */
    void (*hx)(xctx *restrict, nx_journal *);      /* the native (journal 0: plain) */
    uint32_t below, above;                         /* the verify stack window [esp - below, esp + above) */
    int align4;                                    /* esp must be 4-aligned */
    int callee_state;                              /* calls a guest function with state of its own (325C0: the texture cache) */
    unsigned cnt[NXC_N]; uint64_t ns_guest, ns_native;
    int enabled;                                   /* acts (the default set, or XV_NATIVE_EFFECTS_FUNCS) */
    int dflt;                                      /* in the default set */
} nx_desc;
static int nx_mode_value = -1, nx_timing_value = -1;
static unsigned nx_mismatch_total;
#define NX_ADD(d, i, v) __atomic_fetch_add(&(d)->cnt[i], (unsigned)(v), __ATOMIC_RELAXED)
/* the per-call counters of the fast path: a relaxed load and store (no locked read-modify-write; two threads may
 * lose an increment now and then, which only a statistic sees) */
#define NX_BUMP(d, i) __atomic_store_n(&(d)->cnt[i], __atomic_load_n(&(d)->cnt[i], __ATOMIC_RELAXED) + 1u, __ATOMIC_RELAXED)
#ifdef __vita__
static inline uint64_t nx_ns(void) { return xk_os_monotonic_us() * 1000u; }
#else
static inline uint64_t nx_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
#endif

/* The hooked functions: the native, the verify stack window [esp - below, esp + above) (the frames of the function
 * and of its callees - 173F20's CRT frame is the deepest - and the argument slots), and whether esp must be 4-aligned
 * (the hook runs the lifted body otherwise). */
#define NX_HOOKS(X) \
    X(0007E530, "7E530 vs-constant matrices", 0x400, 0x40, 0, 0, 1) \
    X(0007E420, "7E420 vs-constant lights",   0x400, 0x40, 0, 0, 0) \
    X(00056F20, "56F20 texture animation",    0x1000, 0x40, 0, 0, 1) \
    X(00080360, "80360 texture bind",         0x400, 0x40, 0, 1, 0) \
    X(00011B60, "11B60 colour pack",          0x100, 0x40, 1, 0, 1) \
    X(00011610, "11610 colour pack",          0x100, 0x40, 1, 0, 1) \
    X(00011BD0, "11BD0 colour pack",          0x100, 0x40, 1, 0, 1)
#define NX_DECL_BODY(a, nm, b, ab, al, cs, on) void f_##a##_body(xctx *restrict c);
NX_HOOKS(NX_DECL_BODY)
#define NX_DESC(a, nm, b, ab, al, cs, on) static nx_desc nxd_##a = { 0x##a##u, nm, f_##a##_body, hx_##a, b, ab, al, cs, {0}, 0, 0, 1, on };
NX_HOOKS(NX_DESC)
#define NX_DESC_PTR(a, nm, b, ab, al, cs, on) &nxd_##a,
static nx_desc *const nx_all[] = { NX_HOOKS(NX_DESC_PTR) };
#define NX_COUNT (sizeof nx_all / sizeof nx_all[0])

/* The mode and the acting set, read once from the environment. Two threads may do this at the same time: each computes
 * the same values and stores them (relaxed atomics; an early call that sees a stale flag runs the lifted body or the
 * native, both exact). Default set: the hooks whose native is faster in the game (the NX_HOOKS `on` column: 7E420 and
 * 80360 break even or lose on x86 and the Pi 4, docs/native-effects.md); XV_NATIVE_EFFECTS_FUNCS=all or a hex list. */
static int nx_mode(void)
{
    int mode = __atomic_load_n(&nx_mode_value, __ATOMIC_ACQUIRE);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_EFFECTS"); mode = e ? atoi(e) : XV_NATIVE_EFFECTS_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        const char *f = getenv("XV_NATIVE_EFFECTS_FUNCS");
        unsigned on = 0, n_on = 0;
        for (unsigned i = 0; i < NX_COUNT; ++i) if (nx_all[i]->dflt) on |= 1u << i;
        if (f && !strcmp(f, "all")) on = (1u << NX_COUNT) - 1u;
        else if (f && *f) {
            on = 0;
            for (const char *p = f; *p; ) {
                char *end; unsigned long v = strtoul(p, &end, 16);
                if (end == p) { ++p; continue; }
                for (unsigned i = 0; i < NX_COUNT; ++i) if (nx_all[i]->addr == v) on |= 1u << i;
                p = end;
            }
        }
        char names[128]; int k = 0; names[0] = 0;
        for (unsigned i = 0; i < NX_COUNT; ++i) {
            __atomic_store_n(&nx_all[i]->enabled, (int)((on >> i) & 1u), __ATOMIC_RELAXED);
            if ((on >> i) & 1u) { n_on++; if (k < (int)sizeof names - 8) k += snprintf(names + k, sizeof names - (size_t)k, " %.5s", nx_all[i]->name); }
        }
        XK_LOG("[native-effects] %u hooked functions, %u acting (%s):%s: %s\n", (unsigned)NX_COUNT, n_on,
               f && *f ? "XV_NATIVE_EFFECTS_FUNCS" : "default set", names,
               mode == 2 ? "native" : mode == 1 ? "verify (native with a journal, undone, then the lifted body; its result kept)" : "off");
        __atomic_store_n(&nx_mode_value, mode, __ATOMIC_RELEASE);
    }
    return mode;
}
static int nx_timing(void)
{
    int t = __atomic_load_n(&nx_timing_value, __ATOMIC_RELAXED);
    if (t < 0) { const char *e = getenv("XV_NATIVE_EFFECTS_TIME"); t = e && atoi(e) != 0; __atomic_store_n(&nx_timing_value, t, __ATOMIC_RELAXED); }
    return t;
}
/* Tests: select the mode directly (bypassing the environment). */
void xv_native_effects_force(int mode, int timing) { __atomic_store_n(&nx_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); __atomic_store_n(&nx_timing_value, timing, __ATOMIC_RELAXED); }

/* ---- verify: the HLE tap -------------------------------------------------------------------------------------- */
/* recomp/kernel/xd3d.c (built with XV_NATIVE_63C00): every D3D HLE entry calls xv_hle_tap(c, name) when set. The
 * previous tap (63C00's, which filters by its own context) is chained. */
extern void (*volatile xv_hle_tap)(xctx *, const char *) __attribute__((weak));
typedef struct { const char *name; uint32_t r[8], args[4], data; } nx_hle_rec;
typedef struct { unsigned n; nx_hle_rec rec[64]; } nx_hle_log;
static xctx *volatile nx_ref_ctx;
static nx_hle_log *volatile nx_tap_log;
static void (*volatile nx_tap_prev)(xctx *, const char *);
/* NaN payloads: which operand's payload a product or sum propagates is the host compiler's operand order (the lifted
 * bodies built -O0 and -O2 already differ), so a NaN float word against a NaN float word is equal (and counted). The
 * constant data SetVertexShaderConstant reads is hashed with NaN words made one (the runtime turns them into 0). */
static int nx_nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static unsigned nx_nan_words;
static uint32_t nx_hash(uint32_t a, uint32_t bytes, int floats)   /* FNV-1a of guest bytes (the data an HLE entry reads) */
{
    uint32_t h = 2166136261u; uint8_t buf[256];
    while (bytes) { uint32_t n = bytes > sizeof buf ? (uint32_t)sizeof buf : bytes; x_guest_read_pages(buf, a, n);
        if (floats) for (uint32_t i = 0; i + 4 <= n; i += 4) { uint32_t w; memcpy(&w, buf + i, 4); if (nx_nan32(w)) { w = 0x7FC00000u; memcpy(buf + i, &w, 4); } }
        for (uint32_t i = 0; i < n; ++i) h = (h ^ buf[i]) * 16777619u; a += n; bytes -= n; }
    return h;
}
static void nx_tap(xctx *c, const char *name)
{
    void (*prev)(xctx *, const char *) = __atomic_load_n((void (**)(xctx *, const char *))&nx_tap_prev, __ATOMIC_ACQUIRE);
    if (prev) prev(c, name);
    if (c != __atomic_load_n((xctx **)&nx_ref_ctx, __ATOMIC_RELAXED)) return;   /* only the claimant's own store can match */
    nx_hle_log *log = nx_tap_log;
    if (!log) return;
    if (log->n >= 64u) { log->n++; return; }
    nx_hle_rec *r = &log->rec[log->n++];
    r->name = name; memcpy(r->r, c->r, sizeof r->r);
    for (unsigned i = 0; i < 4; ++i) { uint32_t w; x_guest_read_pages(&w, c->r[4] + 4u + 4u * i, 4); r->args[i] = w; }
    r->data = 0;
    if (!strcmp(name, "D3DDevice_SetVertexShaderConstant") && r->args[2] <= 192u) r->data = nx_hash(r->args[1], r->args[2] * 16u, 1);
    else if (!strcmp(name, "D3DDevice_SetPixelShaderProgram") && r->args[0]) r->data = nx_hash(r->args[0], 0xF0u, 0);
}
static int nx_tap_ready(void)
{
    if (!&xv_hle_tap) return 0;
    void (*cur)(xctx *, const char *) = xv_hle_tap;
    /* called with the claim held, so one installer at a time; the chained tap is published before the tap itself */
    if (cur != nx_tap) { __atomic_store_n((void (**)(xctx *, const char *))&nx_tap_prev, cur, __ATOMIC_RELEASE);
                         __atomic_store_n((void (**)(xctx *, const char *))&xv_hle_tap, nx_tap, __ATOMIC_RELEASE); }
    return 1;
}
static int nx_claim(xctx *c)
{
    xctx *expected = 0;
    return __atomic_compare_exchange_n((xctx **)&nx_ref_ctx, &expected, c, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static void nx_release(void) { __atomic_store_n((xctx **)&nx_ref_ctx, (xctx *)0, __ATOMIC_RELEASE); }

/* ---- verify: compare ------------------------------------------------------------------------------------------ */
typedef struct { unsigned n; char line[8][200]; } nx_why;
#define NX_WHY(w, ...) do { if ((w)->n < 8u) snprintf((w)->line[(w)->n], sizeof (w)->line[0], __VA_ARGS__); (w)->n++; } while (0)
static int nx_same_double(double a, double b) { uint64_t x, y; memcpy(&x, &a, 8); memcpy(&y, &b, 8); return x == y || (a != a && b != b); }
static void nx_cmp_ctx(const xctx *n, const xctx *g, nx_why *w)
{
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) if (n->r[i] != g->r[i]) {
        if (nx_nan32(n->r[i]) && nx_nan32(g->r[i])) { __atomic_fetch_add(&nx_nan_words, 1u, __ATOMIC_RELAXED); continue; }   /* a NaN float word loaded as an integer */
        NX_WHY(w, "%s native %08X guest %08X", rn[i], n->r[i], g->r[i]); }
#define NX_CMPF(f) if (n->f != g->f) NX_WHY(w, #f " native %08X guest %08X", (unsigned)n->f, (unsigned)g->f);
    NX_CMPF(df) NX_CMPF(f_kind) NX_CMPF(f_op1) NX_CMPF(f_op2) NX_CMPF(f_res) NX_CMPF(f_bits) NX_CMPF(f_cf_override) NX_CMPF(f_cf)
    NX_CMPF(f_of_override) NX_CMPF(f_of) NX_CMPF(fsp) NX_CMPF(fsw) NX_CMPF(fcw) NX_CMPF(preempt) NX_CMPF(fs_base) NX_CMPF(scratch)
    NX_CMPF(eip_hint)
#undef NX_CMPF
    for (unsigned i = 0; i < 8; ++i) if (!nx_same_double(n->st[i], g->st[i])) NX_WHY(w, "st slot %u native %a guest %a", i, n->st[i], g->st[i]);
    if (memcmp(n->mm, g->mm, sizeof n->mm)) NX_WHY(w, "mmx registers differ");
    if (memcmp(n->xmm, g->xmm, sizeof n->xmm)) NX_WHY(w, "xmm registers differ");
    if (n->fiber != g->fiber) NX_WHY(w, "fiber differs");
}
static void nx_cmp_hle(const nx_hle_log *a, const nx_hle_log *b, nx_why *w)
{
    if (a->n != b->n) { NX_WHY(w, "HLE calls native %u guest %u", a->n, b->n); return; }
    for (unsigned i = 0; i < a->n && i < 64u; ++i) {
        const nx_hle_rec *x = &a->rec[i], *y = &b->rec[i];
        if (strcmp(x->name, y->name)) { NX_WHY(w, "HLE %u: native %s guest %s", i, x->name, y->name); continue; }
        { int rd = 0; for (unsigned k = 0; k < 8; ++k) if (x->r[k] != y->r[k] && !(nx_nan32(x->r[k]) && nx_nan32(y->r[k]))) rd = 1;
          if (rd) NX_WHY(w, "HLE %u %s: registers differ (esp %08X/%08X ecx %08X/%08X edx %08X/%08X)", i, x->name,
                         x->r[4], y->r[4], x->r[1], y->r[1], x->r[2], y->r[2]); }
        if (memcmp(x->args, y->args, sizeof x->args)) NX_WHY(w, "HLE %u %s: arguments %08X %08X %08X / %08X %08X %08X", i, x->name,
                                                              x->args[0], x->args[1], x->args[2], y->args[0], y->args[1], y->args[2]);
        if (x->data != y->data) NX_WHY(w, "HLE %u %s: data hash %08X guest %08X", i, x->name, x->data, y->data);
    }
}

/* One verified call: the journal variant, undone; the lifted body; everything compared; the body's result kept. */
static void nx_verify(xctx *c, nx_desc *d)
{
    if (!nx_claim(c)) { NX_ADD(d, NXC_SKIPPED, 1); d->body(c); return; }
    const int tapped = nx_tap_ready();
    const xctx in = *c;
    const uint32_t lo = c->r[4] - d->below, len = d->below + d->above;
    uint8_t *snap = malloc(2u * len); nx_journal J; memset(&J, 0, sizeof J); J.e = J.inl; J.cap = 256u;
    nx_hle_log *hn = calloc(1, sizeof *hn), *hg = calloc(1, sizeof *hg);
    if (!snap || !hn || !hg) { free(snap); free(hn); free(hg); nx_release(); NX_ADD(d, NXC_SKIPPED, 1); d->body(c); return; }
    uint8_t *after = snap + len;
    x_guest_read_pages(snap, lo, len);
    const int32_t budget = 1 << 30;
    /* the native, journaled */
    c->preempt = budget;
    uint64_t t0 = nx_timing() ? nx_ns() : 0;
    nx_tap_log = hn; d->hx(c, &J); nx_tap_log = 0;
    uint64_t t1 = t0 ? nx_ns() : 0;
    xctx nat = *c;
    x_guest_read_pages(after, lo, len);
    for (unsigned i = 0; i < J.n; ++i) nx_jread(&J.e[i], J.e[i].now);
    const int overflow = J.overflow;
    /* undo */
    for (unsigned i = J.n; i-- > 0; ) nx_jwrite(&J.e[i], J.e[i].old);
    x_guest_write_pages(lo, snap, len);
    *c = in; c->preempt = budget;
    /* the lifted body */
    uint64_t t2 = t0 ? nx_ns() : 0;
    nx_tap_log = hg; d->body(c); nx_tap_log = 0;
    uint64_t t3 = t0 ? nx_ns() : 0;
    nx_release();
    /* compare (both budgets start at `budget`) */
    nx_why w; w.n = 0;
    if (d->callee_state && nat.preempt != c->preempt) {
        /* 325C0 ran its texture-load path in one run and not in the other (the native's run left the texture resident):
         * the two runs did not start from the same state, nothing to compare. The guest's result stands. */
        NX_ADD(d, NXC_DIVERGED, 1);
        const int32_t used0 = budget - c->preempt; c->preempt = in.preempt - used0; if (c->preempt <= 0) xv_preempt(c);
        nx_jfree(&J); free(snap); free(hn); free(hg); return;
    }
    nx_cmp_ctx(&nat, c, &w);
    { uint8_t *now = malloc(len); unsigned nan = 0;
      if (now) { x_guest_read_pages(now, lo, len);
          for (uint32_t i = 0; i < len; i += 4) {
              uint32_t u = 0, v = 0, k = len - i < 4 ? len - i : 4; memcpy(&u, after + i, k); memcpy(&v, now + i, k);
              if (u == v) continue;
              if (k == 4 && nx_nan32(u) && nx_nan32(v)) { nan++; continue; }
              { int ok = 1;                              /* a float at any byte offset: each differing byte in a NaN window on both sides */
                for (uint32_t p = i; p < i + k && ok; ++p) {
                    if (after[p] == now[p]) continue;
                    int cov = 0;
                    for (uint32_t q = p >= 3 ? p - 3 : 0; q <= p && q + 4 <= len && !cov; ++q) { uint32_t x, y; memcpy(&x, after + q, 4); memcpy(&y, now + q, 4); cov = nx_nan32(x) && nx_nan32(y); }
                    ok = cov; }
                if (ok) { nan++; continue; } }
              NX_WHY(&w, "stack word %08X native %08X guest %08X", lo + i, u, v); break; }
          free(now); }
      for (unsigned i = 0; i < J.n; ++i) {
          uint8_t cur[8]; nx_jread(&J.e[i], cur);
          if (memcmp(J.e[i].now, cur, J.e[i].n)) { uint32_t a = 0, b = 0; memcpy(&a, J.e[i].now, J.e[i].n > 4 ? 4 : J.e[i].n); memcpy(&b, cur, J.e[i].n > 4 ? 4 : J.e[i].n);
              if (J.e[i].n == 4 && nx_nan32(a) && nx_nan32(b)) { nan++; continue; }
              NX_WHY(&w, "store %u at %08X (%u bytes) native %08X guest now %08X", i, J.e[i].addr, J.e[i].n, a, b); break; } }
      if (nan) __atomic_fetch_add(&nx_nan_words, nan, __ATOMIC_RELAXED); }
    if (tapped) nx_cmp_hle(hn, hg, &w);
    if (overflow) { NX_ADD(d, NXC_JOVERFLOW, 1); }
    /* the guest's result stands: its back-edge consumption applied to the caller's budget */
    const int32_t used = budget - c->preempt;
    c->preempt = in.preempt - used;
    if (c->preempt <= 0) xv_preempt(c);
    NX_ADD(d, NXC_VERIFIED, 1);
    if (w.n) {
        NX_ADD(d, NXC_MISMATCHED, 1);
        unsigned k = __atomic_fetch_add(&nx_mismatch_total, 1u, __ATOMIC_RELAXED);
        if (k < 16u) {
            XK_LOG("[native-effects] MISMATCH %s (esp %08X, %u stores, %u/%u HLE calls): %u differences\n", d->name, in.r[4], J.n, hn->n, hg->n, w.n);
            for (unsigned i = 0; i < w.n && i < 8u; ++i) XK_LOG("[native-effects]   %s\n", w.line[i]);
        }
    }
    if (t0) { __atomic_fetch_add(&d->ns_native, t1 - t0, __ATOMIC_RELAXED); __atomic_fetch_add(&d->ns_guest, t3 - t2, __ATOMIC_RELAXED);
              NX_ADD(d, NXC_TNATIVE, 1); NX_ADD(d, NXC_TGUEST, 1); }
    nx_jfree(&J); free(snap); free(hn); free(hg);
}

static inline int nx_hook(xctx *c, nx_desc *d)
{
    const int mode = __builtin_expect(nx_mode_value >= 0, 1) ? nx_mode_value : nx_mode();
    if (mode == 0 || !__atomic_load_n(&d->enabled, __ATOMIC_RELAXED)) {
        if (__builtin_expect(nx_timing_value == 0, 1) || !nx_timing()) return 0;
        uint64_t t0 = nx_ns(); d->body(c); uint64_t t1 = nx_ns();
        __atomic_fetch_add(&d->ns_guest, t1 - t0, __ATOMIC_RELAXED); NX_ADD(d, NXC_TGUEST, 1); NX_ADD(d, NXC_CALLS, 1);
        return 1;
    }
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if (xv_is_object_job(c)) { NX_ADD(d, NXC_DECLINED, 1); return 0; }
#endif
    if (d->align4 && (c->r[4] & 3u)) { NX_ADD(d, NXC_DECLINED, 1); return 0; }
    if (mode == 2) {
        NX_BUMP(d, NXC_NATIVE);
        if (__builtin_expect(nx_timing_value == 0, 1)) { d->hx(c, 0); return 1; }
        uint64_t t0 = nx_ns(); d->hx(c, 0); uint64_t t1 = nx_ns();
        __atomic_fetch_add(&d->ns_native, t1 - t0, __ATOMIC_RELAXED); NX_ADD(d, NXC_TNATIVE, 1);
        return 1;
    }
    NX_ADD(d, NXC_CALLS, 1);
    nx_verify(c, d);
    return 1;
}
#define NX_HOOK_FN(a, nm, b, ab, al, cs, on) int xv_native_effects_##a(xctx *c) { return nx_hook(c, &nxd_##a); }
NX_HOOKS(NX_HOOK_FN)
#if defined(XV_NATIVE_EFFECTS_TEST)
/* tools/tests/native_effects.c: the journal's completeness - run the journaled native and undo it (no window restore);
 * the arena must then equal the state before, except for what guest callees and HLE stand-ins wrote. */
int xv_native_effects_undo_test(uint32_t addr, xctx *c)
{
    for (unsigned i = 0; i < NX_COUNT; ++i) if (nx_all[i]->addr == addr) {
        nx_journal J; memset(&J, 0, sizeof J); J.e = J.inl; J.cap = 256u;
        nx_all[i]->hx(c, &J);
        for (unsigned k = J.n; k-- > 0; ) nx_jwrite(&J.e[k], J.e[k].old);
        int ov = J.overflow; nx_jfree(&J); return ov ? -1 : 1;
    }
    return 0;
}
#endif

void xv_native_effects_report(unsigned frames)
{
    if (__atomic_load_n(&nx_mode_value, __ATOMIC_RELAXED) < 0 && __atomic_load_n(&nx_timing_value, __ATOMIC_RELAXED) <= 0) return;
    unsigned tot[NXC_N] = {0}; char line[900]; int n = 0;
    for (unsigned i = 0; i < NX_COUNT; ++i) {
        nx_desc *d = nx_all[i]; unsigned v[NXC_N];
        for (unsigned k = 0; k < NXC_N; ++k) { v[k] = __atomic_exchange_n(&d->cnt[k], 0u, __ATOMIC_RELAXED); tot[k] += v[k]; }
        uint64_t gn = __atomic_exchange_n(&d->ns_guest, 0u, __ATOMIC_RELAXED), nn = __atomic_exchange_n(&d->ns_native, 0u, __ATOMIC_RELAXED);
        v[NXC_CALLS] += v[NXC_NATIVE]; tot[NXC_CALLS] += v[NXC_NATIVE];
        if (!v[NXC_CALLS] && !v[NXC_DECLINED]) continue;
        if (n < (int)sizeof line - 160) n += snprintf(line + n, sizeof line - n, " %X %u", d->addr, v[NXC_CALLS]);
        if (v[NXC_MISMATCHED] && n < (int)sizeof line - 160) n += snprintf(line + n, sizeof line - n, " MISMATCHED %u", v[NXC_MISMATCHED]);
        if ((v[NXC_TGUEST] || v[NXC_TNATIVE]) && n < (int)sizeof line - 160)
            n += snprintf(line + n, sizeof line - n, " (us/call native %.3f guest %.3f; ms/frame %.3f/%.3f)", v[NXC_TNATIVE] ? nn / 1000.0 / v[NXC_TNATIVE] : 0.0,
                          v[NXC_TGUEST] ? gn / 1000.0 / v[NXC_TGUEST] : 0.0, frames ? nn / 1e6 / frames : 0.0, frames ? gn / 1e6 / frames : 0.0);
    }
    if (!tot[NXC_CALLS] && !tot[NXC_DECLINED]) return;
    XK_LOG("[native-effects] %u frames: calls %u native %u verified %u mismatched %u (total mismatches %u) skipped %u declined %u journal-overflow %u callee-diverged %u nan-words %u;%s\n",
           frames, tot[NXC_CALLS], tot[NXC_NATIVE], tot[NXC_VERIFIED], tot[NXC_MISMATCHED], __atomic_load_n(&nx_mismatch_total, __ATOMIC_RELAXED),
           tot[NXC_SKIPPED], tot[NXC_DECLINED], tot[NXC_JOVERFLOW], tot[NXC_DIVERGED], __atomic_exchange_n(&nx_nan_words, 0u, __ATOMIC_RELAXED), line);
}
