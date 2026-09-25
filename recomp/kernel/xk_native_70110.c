/* xk_native_70110.c - native Halo CE (Xbox 3925) material setup f_00070110 (XV_NATIVE_70110).
 *
 * f_00070110 sets up one material for the model and structure passes of the scene: the render states of the material
 * (z enable, z bias, cull mode and nine SetRenderState_Simple writes with their shadow words at 0x18F464..0x18F4A8), four
 * texture stages (f_00080360 binds each, then 5-6 SetTextureState_Deferred per stage), the per-material vertex constants
 * (x87 math on the material and the camera globals at 0x2FC6C8.., f_00056F20, SetVertexShaderConstant), the fog colours
 * (0x2FC8A8.., f_000118D0 / f_00011B60 / f_00011BD0) and the draw (f_0006F340, f_0007A960, a second pass when the
 * material asks for one, f_000736F0). In a30 on the Pi 4 harness with the Vita play settings it runs ~244 times per
 * frame on the scene helper, and its own body (not its callees) is the largest single guest function there: 8.9 % of
 * the helper's cycles (docs/native-70110.md). This unit is that body, native; every callee stays the translated guest
 * function and every D3D call the same HLE function, called directly.
 *
 * Structure. n70_core is a transliteration of the lifted body (one statement per lifted statement, with the x86 address
 * and instruction), drafted by a generator from the lift and reviewed; what differs from the lift is where state lives:
 *  - the eight registers, the lazy-flag record (kind, operands, result, width, both override cells, both stale cf/of
 *    cells), the x87 status word and the x87 slots are locals, written exactly where the lift writes them. The x87 depth
 *    is static at every statement: slot j holds c->st[(T0 + j) & 7], T0 the entry top (re-derived from c->fsp after
 *    every call), so st(i) at depth d is slot (i - d) & 7;
 *  - at every site the generator's dirty-state analysis lists the fields written since the context was last stored or
 *    loaded: exactly those are stored before a translated callee, a hook, the preemption call and the exit, and the
 *    context is loaded back after (unread loads are dropped by the compiler); an HLE call stores the written ones of
 *    ecx/edx/esp (what the six setters read) and reloads esp (eax after SetVertexShaderConstant);
 *  - the 32 `fnstsw ax; test ah,MASK; jcc` chains branch on the compare's condition codes (the masks 0x44/0x41/0x05 see
 *    only C3/C2/C0); the eax/fsw/flag statements stay and are dropped where nothing reads them;
 *  - guest memory is addressed as the lift addresses it (single translation for integer accesses like X_M32,
 *    page-split for x87 float accesses like x87_load_f32 / x87_store_f32) through the calling thread's page table,
 *    taken once per call; the frame window [E - 0x100, E + 0x20) (E: the entry esp), the material ([E+4], ebp from
 *    70118 on) and the image pages the body addresses by constant (0x18F, 0x1F0, 0x232, 0x2E3, 0x2FC) are translated
 *    once per call (their page-table entries do not change during a call: a stack page in use and the image pages are
 *    never remapped, the render view retargets at scene boundaries); every read happens at the lift's point, every
 *    write in the lift's order;
 *  - calls are numbered sites (N70_SITES below): the 19 translated callees, the 40 HLE calls, the two f_00056F20 units
 *    with the XV_MODEL_UV memo, the XV_MODEL_FOG memo's begin and end, the four XV_NATIVE_MATERIAL_SAMPLER groups and the
 *    one back-edge (70321 -> 7030D, X_PREEMPT). The memos and the sampler groups are called as the lift calls them
 *    (their presence in the lift is passed in `cfg` by the hook, compiled with the shard's flags).
 * Three instances: FAST (mode 2 with the window and the material each in one page: esp is E minus the lift's stack
 * offset at every point and is checked after every call, so frame slots and material fields are one load), GEN (mode 2
 * otherwise, or after a FAST call returned an unexpected esp/ebp: every access translated like the lift's) and DRY
 * (verify: stops at the next site with the predicted state in a private context, the frame window in a shadow and other
 * writes in an overlay; resumes after a site from the actual state).
 *
 * Declines (the translation runs; before any write but the four callee-saved pushes, which the translation then
 * repeats with the same values): the prologue's rare paths (the 6EFC0 path of a set [2E3520]+0A8h, [2E3528] == 1,
 * [ebp+24h] == 3), an object-job context, esp not 4-aligned or the window near the ends of the address space, the
 * window over an image page the body addresses by constant.
 *
 * Threads: the owner and the scene helper (through its own page table, the render view) may both be in 70110 at once.
 * No __thread and no shared mutable state (vitasdk's __thread is emutls, which the Vita's threads do not keep apart:
 * docs/native-1721b0.md); a verify session lives in its call's heap block and reaches the tapped guest copy as an argument.
 *
 * XV_NATIVE_70110 build flag (hook and tapped copy: tools/patch_native_70110_hooks.py). Env XV_NATIVE_70110: 0 off
 * (default XV_NATIVE_70110_DEFAULT), 1 verify (the guest copy with taps at every site runs; at each site the native's
 * prediction, made from the actual state at the previous site, is compared: registers, flags, x87, preempt budget, the
 * frame window, the render-state shadow words and every other native write; the guest's result stands), 2 native.
 * XV_REC_AB=<frames> with XV_NATIVE_70110 in XV_REC_AB_KNOBS alternates 0/2 (runtime/xv_record_opt.h).
 * XV_NATIVE_70110_TIME=1: us/call of the call (inclusive of the callees) in modes 0 and 2. [native-70110] lines every
 * 60 frames (recomp/kernel/xd3d.c's weak report chain). */
#include "xk.h"
/* xv_x86rt.h comes through xk.h (one path: the unit is also compiled from a copy by tools/test_native_70110.py) */
#include "../xv_phase.h"
#include "xk_object_jobs.h"
#include "../../runtime/xv_record_opt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_NATIVE_70110_DEFAULT
#define XV_NATIVE_70110_DEFAULT 0
#endif

/* The hook passes what the shard's lift contains (tools/patch_native_70110_hooks.py computes it from the shard's flags). */
enum { N70_CFG_UV = 1u, N70_CFG_FOG = 2u, N70_CFG_SAMPLER = 4u };
enum { N70_WLO = 0x100u, N70_WHI = 0x20u, N70_WSIZE = N70_WLO + N70_WHI };   /* frame window [E - WLO, E + WHI) */
enum { N70_EXIT = 1000, N70_DECLINE = 1001 };
enum { N70_OW_MAX = 48 };
/* site kinds, as the tapped guest copy reports them (key: the return address pushed before the call; the fog memo's
 * begin/end: 0x70A42/0x70D07; the back-edge: 0x70321; a sampler group: its number) */
enum { N70_K_CALL = 1, N70_K_HLE, N70_K_UV, N70_K_FOG_BEGIN, N70_K_FOG_END, N70_K_PREEMPT, N70_K_SAMPLER };

/* ---- guest functions and HLE ---------------------------------------------------------------------------------- */
void f_00080360(xctx *); void f_000B5130(xctx *); void f_00173F20(xctx *); void f_000111A0(xctx *);
void f_000621E0(xctx *); void f_000658D0(xctx *); void f_00056F20(xctx *); void f_000118D0(xctx *);
void f_00011B60(xctx *); void f_00011BD0(xctx *); void f_0006F340(xctx *); void f_0007A960(xctx *);
void f_000736F0(xctx *);
void f_00070110_body(xctx *);                 /* the translation (the shard's body, renamed by the hook tool) */
void f_00070110_vbody(xctx *, void *);        /* its copy with verify taps at every site */
void xv_hle_D3DDevice_SetRenderState_ZEnable(xctx *); void xv_hle_D3DDevice_SetRenderState_Simple(xctx *);
void xv_hle_D3DDevice_SetRenderState_ZBias(xctx *); void xv_hle_D3DDevice_SetRenderState_CullMode(xctx *);
void xv_hle_D3DDevice_SetTextureState_Deferred(xctx *); void xv_hle_D3DDevice_SetVertexShaderConstant(xctx *);
extern int xk_model_uv_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *, unsigned, unsigned *) __attribute__((weak));
extern void xk_model_uv_end(xctx *, unsigned) __attribute__((weak));
extern int xk_model_fog_begin(xctx *, const uint8_t *, const uint32_t *, const uint8_t *, unsigned *) __attribute__((weak));
extern void xk_model_fog_end(xctx *, unsigned) __attribute__((weak));
extern int xv_material_sampler_try(xctx *, unsigned) __attribute__((weak));
extern volatile uint32_t xv_cur_fn;
extern int xv_hle_timing; extern unsigned xv_hle_timed_calls;
void xv_hle_time_add(const char *, uint64_t);
uint64_t xk_os_monotonic_us(void);

/* XV_HLE_CALL as the lift expands it for a D3D HLE (no proxy: not an xk_ function; an object job is declined at
 * entry): the profiler's current function, the XV_HLE_TIMING branch. */
static __attribute__((noinline)) void n70_hle_timed(xctx *c, void (*fn)(xctx *), const char *name)
{ xv_hle_timed_calls++; uint64_t t0 = xk_os_monotonic_us(); fn(c); xv_hle_time_add(name, xk_os_monotonic_us() - t0); }
static inline __attribute__((always_inline)) void n70_hle_call(xctx *c, uint32_t addr, void (*fn)(xctx *), const char *name)
{
    uint32_t saved = xv_cur_fn; xv_cur_fn = 0x80000000u | addr;
    if (__builtin_expect(xv_hle_timing != 0, 0)) n70_hle_timed(c, fn, name); else fn(c);
    xv_cur_fn = saved;
}

/* ---- guest memory -----------------------------------------------------------------------------------------------
 * ram/pt: the arena and the calling thread's page table (X_PT: TPIDRURW on the Vita, xv_host_page_table on the host,
 * the scene helper's render view), taken once per call. w0/w1: the host addresses of the window's (at most two) pages,
 * so that host(wlo + o) = o < wcut ? w0 + o : w1 + (o - wcut). ki_* / kf_*: the image pages the body addresses by
 * constant, as X_IMG* (integer) and as x87_load_f32 (through the table) translate them (the same with XV_RENDER_VIEW).
 * Dry (verify) runs: the window is `shadow`, other writes go to the overlay `ow`, reads see both. */
typedef struct { uint32_t addr; uint8_t size; uint8_t bytes[4]; } n70_ow;
typedef struct n70_env {
    uint8_t *ram; const uint32_t *pt; const uint8_t *imgb;
    uint32_t E, wlo, wcut; uint8_t *w0, *w1;
    uint8_t *ki_18f, *ki_232, *ki_2e3, *ki_2fc, *kf_1f0, *kf_2fc;
    uint8_t *shadow; n70_ow *ow; unsigned nw; int ovf, odd;
    unsigned utok, ftok;      /* the XV_MODEL_UV / XV_MODEL_FOG tokens (the lift's function-scope locals) */
    xv_phase_scope scope;     /* the lift's phase scope (id 24) */
} n70_env;
#define N70_H(e, a) ((e)->ram + (e)->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))
/* the shard's per-function xpt_/imgb_ (the memos get them as arguments): the thread's table and image base with
 * XV_THREAD_PAGE_TABLE (the shard defines g_xpt as X_PT and g_img_base as X_IMG_BASE), else the globals */
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
#define N70_XPT X_PT
#define N70_IMGB X_IMG_BASE
#else
#define N70_XPT g_xpt
#define N70_IMGB g_img_base
#endif

static inline __attribute__((always_inline)) void n70_map_window(n70_env *e)
{
    const uint32_t lo = e->wlo, hi = lo + N70_WSIZE - 1u;
    e->w0 = N70_H(e, lo);
    e->wcut = (lo >> 12) == (hi >> 12) ? N70_WSIZE : 0x1000u - (lo & 0xFFFu);
    e->w1 = N70_H(e, (lo | 0xFFFu) + 1u);
}
static inline __attribute__((always_inline)) void n70_map(n70_env *e)
{
    n70_map_window(e);
    e->ki_18f = (uint8_t *)&X_IMG8(0x18F000u); e->ki_232 = (uint8_t *)&X_IMG8(0x232000u);
    e->ki_2e3 = (uint8_t *)&X_IMG8(0x2E3000u); e->ki_2fc = (uint8_t *)&X_IMG8(0x2FC000u);
    e->kf_1f0 = N70_H(e, 0x1F0000u); e->kf_2fc = N70_H(e, 0x2FC000u);
}
static inline __attribute__((always_inline)) uint32_t n70_ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) uint16_t n70_ld16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline __attribute__((always_inline)) void n70_st32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) float n70_ldf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
/* esp-relative accesses: through the window when they lie in it, else translated (never in the game) */
static inline __attribute__((always_inline)) uint8_t *n70_sh(const n70_env *e, uint32_t a)
{
    const uint32_t o = a - e->wlo;
    if (__builtin_expect(o < N70_WSIZE, 1)) return o < e->wcut ? e->w0 + o : e->w1 + (o - e->wcut);
    return N70_H(e, a);
}
/* x87 float load/store, page-split when the float crosses a page end (x_guest_read / x_guest_write) */
static __attribute__((noinline)) double n70_ldf_split(const n70_env *e, uint32_t a)
{
    uint8_t b[4]; for (unsigned i = 0; i < 4; ++i) b[i] = *N70_H(e, a + i);
    return (double)n70_ldf(b);
}
static __attribute__((noinline)) void n70_stf_split(const n70_env *e, uint32_t a, float v)
{
    uint8_t b[4]; memcpy(b, &v, 4); for (unsigned i = 0; i < 4; ++i) *N70_H(e, a + i) = b[i];
}
static inline __attribute__((always_inline)) double n70_ldf_g(const n70_env *e, uint32_t a)
{ return (a & 0xFFFu) <= 0xFFCu ? (double)n70_ldf(N70_H(e, a)) : n70_ldf_split(e, a); }
static inline __attribute__((always_inline)) void n70_stf_g(const n70_env *e, uint32_t a, double d)
{ float v = (float)d; if ((a & 0xFFFu) <= 0xFFCu) memcpy(N70_H(e, a), &v, 4); else n70_stf_split(e, a, v); }
static inline __attribute__((always_inline)) double n70_ldf_s(const n70_env *e, uint32_t a)
{ return (a & 0xFFFu) <= 0xFFCu ? (double)n70_ldf(n70_sh(e, a)) : n70_ldf_split(e, a); }
static inline __attribute__((always_inline)) void n70_stf_s(const n70_env *e, uint32_t a, double d)
{ float v = (float)d; if ((a & 0xFFFu) <= 0xFFCu) memcpy(n70_sh(e, a), &v, 4); else n70_stf_split(e, a, v); }

/* Dry (verify) accesses, byte by byte: the window from the shadow, then the overlay (newest first), then guest memory
 * (host: the single translation of an integer access, or NULL for a page-split float access). An integer access across
 * a page end, or one partly in the window, marks the segment odd (the compare is skipped; never in the game). */
static __attribute__((noinline)) void n70_dry_read(n70_env *e, uint32_t a, void *dst, unsigned n, const uint8_t *host)
{
    uint8_t *d = dst; unsigned inwin = 0;
    if (host && (a & 0xFFFu) + n > 0x1000u) e->odd = 1;
    for (unsigned i = 0; i < n; ++i) {
        const uint32_t b = a + i, o = b - e->wlo;
        if (o < N70_WSIZE) { d[i] = e->shadow[o]; inwin++; continue; }
        unsigned j = e->nw; int hit = 0;
        while (j-- > 0) if (b - e->ow[j].addr < e->ow[j].size) { d[i] = e->ow[j].bytes[b - e->ow[j].addr]; hit = 1; break; }
        if (!hit) d[i] = host ? host[i] : *N70_H(e, b);
    }
    if (inwin && inwin != n) e->odd = 1;
}
static __attribute__((noinline)) void n70_dry_write(n70_env *e, uint32_t a, const void *src, unsigned n, int integer)
{
    const uint8_t *s = src; unsigned inwin = 0;
    if (integer && (a & 0xFFFu) + n > 0x1000u) e->odd = 1;
    for (unsigned i = 0; i < n; ++i) { const uint32_t o = a + i - e->wlo; if (o < N70_WSIZE) { e->shadow[o] = s[i]; inwin++; } }
    if (inwin == n) return;
    if (inwin) { e->odd = 1; return; }
    if (e->nw == N70_OW_MAX) { e->ovf = 1; return; }
    n70_ow *w = &e->ow[e->nw++]; w->addr = a; w->size = (uint8_t)n; memcpy(w->bytes, s, n);
}
static inline __attribute__((always_inline)) uint32_t n70_dry_r(n70_env *e, uint32_t a, unsigned n, const uint8_t *host)
{ uint32_t v = 0; n70_dry_read(e, a, &v, n, host); return v; }   /* little-endian host (ARM, x86) */
static inline __attribute__((always_inline)) double n70_dry_ldf(n70_env *e, uint32_t a)
{ float v; n70_dry_read(e, a, &v, 4, NULL); return (double)v; }
static inline __attribute__((always_inline)) void n70_dry_stf(n70_env *e, uint32_t a, double d)
{ float v = (float)d; n70_dry_write(e, a, &v, 4, 0); }
static inline __attribute__((always_inline)) void n70_dry_w(n70_env *e, uint32_t a, uint32_t v, unsigned n)
{ n70_dry_write(e, a, &v, n, 1); }

/* constant image addresses: the page pointer (folded at compile time) */
#define N70_KI(a) ((((a) >> 12) == 0x18Fu ? e->ki_18f : ((a) >> 12) == 0x232u ? e->ki_232 : ((a) >> 12) == 0x2E3u ? e->ki_2e3 : e->ki_2fc) + ((a) & 0xFFFu))
#define N70_KF(a) ((((a) >> 12) == 0x1F0u ? e->kf_1f0 : e->kf_2fc) + ((a) & 0xFFFu))

/* ---- the macro language of the transliteration ------------------------------------------------------------------
 * Three instances of n70_core (M): FAST (mode 2, the frame window in one page: esp is E minus the offset the lift's stack
 * discipline gives at every point, checked after every call, so a frame slot is w0 + a constant), GEN (mode 2 otherwise,
 * or after a FAST call returned an unexpected esp: every access translated like the lift's), DRY (verify). */
enum { N70_M_FAST, N70_M_GEN, N70_M_DRY };
enum { N70_BAIL = 2000 };
#define DRY (M == N70_M_DRY)
#define FAST (M == N70_M_FAST)
#define N70_SP(a) (FAST ? w0 + ((uint32_t)(a) - (E - N70_WLO)) : N70_H(e, (a)))
#define SM32(a) (DRY ? n70_dry_r(e, (a), 4, N70_H(e, (a))) : n70_ld32(N70_SP(a)))
#define SW32(a, v) do { uint32_t a__ = (a), v__ = (v); if (DRY) n70_dry_w(e, a__, v__, 4); else n70_st32(N70_SP(a__), v__); } while (0)
#define SLDF(a) (DRY ? n70_dry_ldf(e, (a)) : FAST ? (double)n70_ldf(N70_SP(a)) : n70_ldf_g(e, (a)))
#define SSTF(a, v) do { uint32_t a__ = (a); double v__ = (v); if (DRY) n70_dry_stf(e, a__, v__); \
                        else if (FAST) { float f__ = (float)v__; memcpy(N70_SP(a__), &f__, 4); } else n70_stf_g(e, a__, v__); } while (0)
#define M32(a) (DRY ? n70_dry_r(e, (a), 4, N70_H(e, (a))) : n70_ld32(N70_H(e, (a))))
#define M16(a) ((uint16_t)(DRY ? n70_dry_r(e, (a), 2, N70_H(e, (a))) : n70_ld16(N70_H(e, (a)))))
#define M8(a) ((uint8_t)(DRY ? n70_dry_r(e, (a), 1, N70_H(e, (a))) : *N70_H(e, (a))))
#define LDF(a) (DRY ? n70_dry_ldf(e, (a)) : n70_ldf_g(e, (a)))
#define K32(a) (DRY ? n70_dry_r(e, (a), 4, N70_KI(a)) : n70_ld32(N70_KI(a)))
#define K16(a) ((uint16_t)(DRY ? n70_dry_r(e, (a), 2, N70_KI(a)) : n70_ld16(N70_KI(a))))
#define K8(a) ((uint8_t)(DRY ? n70_dry_r(e, (a), 1, N70_KI(a)) : *N70_KI(a)))
#define KW32(a, v) do { uint32_t v__ = (v); if (DRY) n70_dry_w(e, (a), v__, 4); else n70_st32(N70_KI(a), v__); } while (0)
/* x87 constant loads go through the table like x87_load_f32 (the page is translated once; the address is 4-aligned) */
#define KLDF(a) (DRY ? n70_dry_ldf(e, (a)) : (double)n70_ldf(N70_KF(a)))
/* the material (ebp from 70118 on: [E+4], restored by every callee): FAST reads it from one page, checked where ebp is
 * loaded (a material across a page end runs GEN from the start) and after every call (else GEN after the site) */
enum { N70_MLEN = 0x180u };
#define MB_SET() do { if (FAST) { if ((ebp & 0xFFFu) + N70_MLEN > 0x1000u) return N70_BAIL + 0; mb = N70_H(e, ebp); EBP0 = ebp; } } while (0)
#define MB32(k) (FAST ? n70_ld32(mb + (k)) : M32(ebp + (k)))
#define MB16(k) (FAST ? n70_ld16(mb + (k)) : M16(ebp + (k)))
#define MB8(k) (FAST ? mb[k] : M8(ebp + (k)))
#define MBF(k) (FAST ? (double)n70_ldf(mb + (k)) : LDF(ebp + (k)))
#define PUSH(v) do { uint32_t p__ = (v); esp -= 4u; SW32(esp, p__); } while (0)
#define POP() (esp += 4u, SM32(esp - 4u))
/* partial registers */
#define R16(r) ((uint16_t)(r))
#define R8L(r) ((uint8_t)(r))
#define R8H(r) ((uint8_t)((r) >> 8))
#define SET16(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))
#define SET8L(r, v) ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))
/* lazy flags: X_FLAGS / X_FLAGS_C on the locals; XF_* as xv_x86rt.h evaluates them */
#define FLAGS(kind, op1, op2, res, bits) do { fk = (kind); fa = (uint32_t)(op1); fb = (uint32_t)(op2); fr = (uint32_t)(res); fw = (bits); fco = 0; foo = 0; } while (0)
#define FLAGS_C(kind, op1, op2, res, bits, cfin) do { FLAGS(kind, op1, op2, res, bits); fcf = (cfin); } while (0)
static inline __attribute__((always_inline)) uint32_t n70_fmask(uint32_t w) { return w == 32 ? 0xFFFFFFFFu : ((1u << w) - 1u); }
static inline __attribute__((always_inline)) uint32_t n70_fz(uint32_t k, uint32_t r, uint32_t w) { return k == XK_EXPLICIT ? (r >> 6) & 1u : (r & n70_fmask(w)) == 0; }
static inline __attribute__((always_inline)) uint32_t n70_fs(uint32_t k, uint32_t r, uint32_t w) { return k == XK_EXPLICIT ? (r >> 7) & 1u : (r >> (w - 1)) & 1u; }
static inline __attribute__((always_inline)) uint32_t n70_fp(uint32_t k, uint32_t r)
{ if (k == XK_EXPLICIT) return (r >> 2) & 1u; uint32_t v = r & 0xFFu; v ^= v >> 4; return ((0x6996u >> (v & 0xFu)) & 1u) ^ 1u; }
static inline __attribute__((always_inline)) uint32_t n70_fc(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t w, uint32_t co, uint32_t cf)
{
    if (k == XK_EXPLICIT) return r & 1u;
    if (co) return cf;
    const uint32_t m = n70_fmask(w); a &= m; b &= m; r &= m;
    switch (k) { case XK_ADD: return r < a; case XK_ADC: return cf ? (r <= a) : (r < a); case XK_SUB: return a < b; case XK_SBB: return cf ? (a <= b) : (a < b); default: return 0; }
}
static inline __attribute__((always_inline)) uint32_t n70_fo(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t w, uint32_t oo, uint32_t of)
{
    if (k == XK_EXPLICIT) return (r >> 11) & 1u;
    if (oo) return of;
    switch (k) { case XK_ADD: case XK_ADC: return (((a ^ r) & (b ^ r)) >> (w - 1)) & 1u; case XK_SUB: case XK_SBB: return (((a ^ b) & (a ^ r)) >> (w - 1)) & 1u; default: return 0; }
}
#define FZ n70_fz(fk, fr, fw)
#define FS n70_fs(fk, fr, fw)
#define FP n70_fp(fk, fr)
#define FC n70_fc(fk, fa, fb, fr, fw, fco, fcf)
#define FO n70_fo(fk, fa, fb, fr, fw, foo, fof)
/* shr r/m8, imm (x_shr8) */
#define SHR8(v, n) n70_shr8((v), (n), &fk, &fa, &fb, &fr, &fw, &fco, &fcf, &foo, &fof)
static inline __attribute__((always_inline)) uint8_t n70_shr8(uint8_t v, uint32_t n, uint32_t *fk, uint32_t *fa, uint32_t *fb, uint32_t *fr,
                                                             uint32_t *fw, uint32_t *fco, uint32_t *fcf, uint32_t *foo, uint32_t *fof)
{
    n &= 31; if (!n) return v;
    const uint8_t r = n >= 8 ? 0 : (uint8_t)((uint32_t)v >> n);
    *fk = XK_LOGIC; *fa = 0; *fb = 0; *fr = r; *fw = 8; *fco = 1; *fcf = n <= 8 ? ((uint32_t)v >> (n - 1)) & 1u : 0;
    *foo = 1; *fof = ((uint32_t)v >> 7) & 1u;
    return r;
}
/* x87: the depth d (pushes since the entry) is static at every statement (tools' generator); slot j of the locals xs[]
 * holds c->st[(T0 + j) & 7] (T0: the entry top, re-derived from c->fsp after every call), so st(i) at depth d is
 * xs[(i - d) & 7]; the status word is a local. XD(j) marks a slot write for the generator's dirty analysis. */
#define XS(j) xs[j]
#define XD(j)
#define FCOMT(off, a, b) (cc = n70_cc((a), (b)), fsw = (uint16_t)((fsw & ~0x4700u) | cc | (((T0 + (off)) & 7u) << 11)))
/* the flags of `fnstsw ax; test ah,mask` after a compare with condition codes cc (one of 0, 0x100, 0x4000, 0x4500):
 * ZF (flag 'Z') or PF ('P') of (cc >> 8) & mask, spelled per value so that the compiler folds it to a compare of cc */
static inline __attribute__((always_inline)) uint32_t n70_parity_even(uint32_t v) { v &= 0xFFu; v ^= v >> 4; return ((0x6996u >> (v & 0xFu)) & 1u) ^ 1u; }
static inline __attribute__((always_inline)) int n70_ccj(uint32_t cc, uint32_t mask, char flag)
{
    const uint32_t r = cc == 0x4500u ? 0x45u & mask : cc == 0x0100u ? 0x01u & mask : cc == 0x4000u ? 0x40u & mask : 0u;
    return flag == 'Z' ? r == 0 : (int)n70_parity_even(r);
}
static inline __attribute__((always_inline)) uint32_t n70_cc(double a, double b)
{
    uint32_t cc;
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    /* x87_compare's own form on the Vita (xv_x86rt.h): one quiet VFP compare, the same condition codes */
    uint32_t result;
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"
                     "mov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\t"
                     "it vs\n\tmovvs %0, #17664"
                     : "=r"(result) : "w"(a), "w"(b) : "cc");
    cc = result;
#else
    if (isnan(a) || isnan(b)) cc = 0x4500; else if (a < b) cc = 0x0100; else if (a == b) cc = 0x4000; else cc = 0;
#endif
    return cc;
}
/* state to the context and back: the generator lists the fields written since the last store/load at every site */
#define N70_UNPAREN(...) __VA_ARGS__
#define SYNCL(list) do { N70_UNPAREN list } while (0)
#define ST_NONE
#define ST_eax c->r[0] = eax;
#define ST_ecx c->r[1] = ecx;
#define ST_edx c->r[2] = edx;
#define ST_ebx c->r[3] = ebx;
#define ST_esp c->r[4] = esp;
#define ST_ebp c->r[5] = ebp;
#define ST_esi c->r[6] = esi;
#define ST_edi c->r[7] = edi;
#define ST_fk c->f_kind = fk;
#define ST_fa c->f_op1 = fa;
#define ST_fb c->f_op2 = fb;
#define ST_fr c->f_res = fr;
#define ST_fw c->f_bits = fw;
#define ST_fco c->f_cf_override = fco;
#define ST_foo c->f_of_override = foo;
#define ST_fcf c->f_cf = fcf;
#define ST_fof c->f_of = fof;
#define ST_fsw c->fsw = fsw;
#define ST_x0 c->st[T0 & 7u] = xs[0];
#define ST_x1 c->st[(T0 + 1u) & 7u] = xs[1];
#define ST_x2 c->st[(T0 + 2u) & 7u] = xs[2];
#define ST_x3 c->st[(T0 + 3u) & 7u] = xs[3];
#define ST_x4 c->st[(T0 + 4u) & 7u] = xs[4];
#define ST_x5 c->st[(T0 + 5u) & 7u] = xs[5];
#define ST_x6 c->st[(T0 + 6u) & 7u] = xs[6];
#define ST_x7 c->st[(T0 + 7u) & 7u] = xs[7];
/* store the listed fields and the top at depth d; load everything back (the compiler drops the loads nothing reads) */
#define SYNC_AT(d, list) do { SYNCL(list); c->fsp = (T0 - (uint32_t)(d)) & 7u; } while (0)
#define LOAD_REGS() (eax = c->r[0], ecx = c->r[1], edx = c->r[2], ebx = c->r[3], esp = c->r[4], ebp = c->r[5], esi = c->r[6], edi = c->r[7])
#define LOAD_AT(d) do { LOAD_REGS(); fk = c->f_kind; fa = c->f_op1; fb = c->f_op2; fr = c->f_res; fw = c->f_bits; fco = c->f_cf_override; \
        fcf = c->f_cf; foo = c->f_of_override; fof = c->f_of; fsw = c->fsw; T0 = (c->fsp + (uint32_t)(d)) & 7u; \
        xs[0] = c->st[T0]; xs[1] = c->st[(T0 + 1u) & 7u]; xs[2] = c->st[(T0 + 2u) & 7u]; xs[3] = c->st[(T0 + 3u) & 7u]; \
        xs[4] = c->st[(T0 + 4u) & 7u]; xs[5] = c->st[(T0 + 5u) & 7u]; xs[6] = c->st[(T0 + 6u) & 7u]; xs[7] = c->st[(T0 + 7u) & 7u]; } while (0)
#define REMAP() do { } while (0)
/* FAST: esp after a site must be E - off (the lift's stack discipline) and ebp the material; else the call continues in
 * GEN after the site (the state is in the context: just loaded) */
#define ESP_CHECK(k, off) do { if (FAST) { if (esp != E - (uint32_t)(off) || ebp != EBP0) return N70_BAIL + (k); esp = E - (uint32_t)(off); ebp = EBP0; } } while (0)
/* sites. Mode 2 performs the call; a dry run stops before it (returns the site, state stored) and resumes after it
 * (R_k: the actual state loaded, `outcome` the hook's result); GEN resumes the same way after a FAST bail. */
#define CALL(k, fn, off, d, da, list) do { SYNC_AT(d, list); if (DRY) return k; fn(c); if (0) { R_##k:; } LOAD_AT(da); ESP_CHECK(k, off); } while (0)
/* An HLE call reads ecx/edx and its stack arguments and returns esp (eax for SetVertexShaderConstant); mode 2 stores the
 * written ones of those and reloads esp/eax (verify compares the whole state at every HLE call and checks that nothing
 * else changed). */
#define N70_EAX(name) (__builtin_strcmp(#name, "D3DDevice_SetVertexShaderConstant") == 0)
#define HLE(k, addr, name, off, d, full, hle) do { if (DRY) { SYNC_AT(d, full); return k; } \
        SYNCL(hle); n70_hle_call(c, addr, xv_hle_##name, "xv_hle_" #name); \
        if (N70_EAX(name)) eax = c->r[0]; \
        esp = FAST ? E - (uint32_t)(off) : c->r[4]; \
        if (0) { R_##k: LOAD_AT(d); } } while (0)
#define UVCALL(k, variant, off, d, da, list) do { SYNC_AT(d, list); if (DRY) return k; \
        if (!(cfg & N70_CFG_UV) || !xk_model_uv_begin(c, g_xram, e->pt, e->imgb, (variant), &e->utok)) { \
            f_00056F20(c); if (cfg & N70_CFG_UV) xk_model_uv_end(c, e->utok); } \
        if (0) { R_##k:; } LOAD_AT(da); ESP_CHECK(k, off); } while (0)
#define FOG_BEGIN(k, target, off, d, da, list) do { if (cfg & N70_CFG_FOG) { int h__; SYNC_AT(d, list); if (DRY) return k; \
        h__ = xk_model_fog_begin(c, g_xram, e->pt, e->imgb, &e->ftok); \
        if (0) { R_##k: h__ = outcome; } LOAD_AT(da); ESP_CHECK(k, off); if (h__) goto target; } } while (0)
#define FOG_END(k, off, d, da, list) do { if (cfg & N70_CFG_FOG) { SYNC_AT(d, list); if (DRY) return k; xk_model_fog_end(c, e->ftok); \
        if (0) { R_##k:; } LOAD_AT(da); ESP_CHECK(k, off); } } while (0)
#define SAMPLER(k, g, skip, off, d, da, list) do { if (cfg & N70_CFG_SAMPLER) { int h__; SYNC_AT(d, list); if (DRY) return k; \
        h__ = xv_material_sampler_try(c, (g)); \
        if (0) { R_##k: h__ = outcome; } LOAD_AT(da); ESP_CHECK(k, off); if (h__) goto skip; } } while (0)
#define PREEMPT(k, off, d, da, list) do { if (DRY) { SYNC_AT(d, list); return k; } \
        if (--c->preempt <= 0) { SYNC_AT(d, list); xv_preempt(c); LOAD_AT(da); ESP_CHECK(k, off); } if (0) { R_##k: LOAD_AT(da); } } while (0)
#define EXIT(d, list) do { SYNC_AT(d, list); return N70_EXIT; } while (0)
#define DECLINE() do { return N70_DECLINE; } while (0)
/* the lift's XV_PHASE_SCOPE(c, 24u), opened where no decline can follow (L_00070265) and closed by the caller */
#define SCOPE_OPEN() do { if (!DRY && xv_phase_enabled) xv_phase_begin(&e->scope, c, 24u); } while (0)

/* ---- the transliteration of f_00070110 -------------------------------------------------------------------------
 * One statement per lifted statement (x86 address and instruction in the comment); see the header for the macros. */
static inline __attribute__((always_inline)) int n70_core(xctx *restrict c, n70_env *restrict e, const unsigned cfg,
                                                         const int resume, const int outcome, const int M)
{
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi, fk, fa, fb, fr, fw, fco, fcf, foo, fof, T0;
    uint16_t fsw;
    uint32_t cc = 0, EBP0 = 0;
    double xs[8];
    uint8_t *w0 = e->w0, *mb = NULL;
    (void)outcome; (void)cfg; (void)w0; (void)mb; (void)EBP0;
    LOAD_AT(0);
    T0 = c->fsp;   /* the entry top as the lift keeps it (masked by its first push) */
    const uint32_t E = FAST ? esp : e->E;   /* FAST: the entry esp itself, so frame offsets fold */
    if (!FAST) switch (resume) {
    case 0: break;
    case 1: goto R_1;
    case 2: goto R_2;
    case 3: goto R_3;
    case 4: goto R_4;
    case 5: goto R_5;
    case 6: goto R_6;
    case 7: goto R_7;
    case 8: goto R_8;
    case 9: goto R_9;
    case 10: goto R_10;
    case 11: goto R_11;
    case 12: goto R_12;
    case 13: goto R_13;
    case 14: goto R_14;
    case 15: goto R_15;
    case 16: goto R_16;
    case 17: goto R_17;
    case 18: goto R_18;
    case 19: goto R_19;
    case 20: goto R_20;
    case 21: goto R_21;
    case 22: goto R_22;
    case 23: goto R_23;
    case 24: goto R_24;
    case 25: goto R_25;
    case 26: goto R_26;
    case 27: goto R_27;
    case 28: goto R_28;
    case 29: goto R_29;
    case 30: goto R_30;
    case 31: goto R_31;
    case 32: goto R_32;
    case 33: goto R_33;
    case 34: goto R_34;
    case 35: goto R_35;
    case 36: goto R_36;
    case 37: goto R_37;
    case 38: goto R_38;
    case 39: goto R_39;
    case 40: goto R_40;
    case 41: goto R_41;
    case 42: goto R_42;
    case 43: goto R_43;
    case 44: goto R_44;
    case 45: goto R_45;
    case 46: goto R_46;
    case 47: goto R_47;
    case 48: goto R_48;
    case 49: goto R_49;
    case 50: goto R_50;
    case 51: goto R_51;
    case 52: goto R_52;
    case 53: goto R_53;
    case 54: goto R_54;
    case 55: goto R_55;
    case 56: goto R_56;
    case 57: goto R_57;
    case 58: goto R_58;
    case 59: goto R_59;
    case 60: goto R_60;
    case 61: goto R_61;
    case 62: goto R_62;
    case 63: goto R_63;
    case 64: goto R_64;
    case 65: goto R_65;
    case 66: goto R_66;
    case 67: goto R_67;
    case 68: goto R_68;
    default: return N70_DECLINE;
    }
L_00070110:
    /* 00070110  sub esp,0A0h */
    { uint32_t a_ = esp, b_ = 0xA0u; uint32_t r_ = (uint32_t)(a_ - b_); esp = r_; }
    /* 00070116  push ebx */
    PUSH(ebx);
    /* 00070117  push ebp */
    PUSH(ebp);
    /* 00070118  mov ebp,[esp+0ACh] */
    ebp = SM32((esp+0xACu)); MB_SET();
    /* 0007011F  push esi */
    PUSH(esi);
    /* 00070120  mov esi,[esp+0B4h] */
    esi = SM32((esp+0xB4u));
    /* 00070127  push edi */
    PUSH(edi);
    /* 00070128  mov edi,ds:[2E3520h] */
    edi = K32(0x2E3520u);
    /* 0007012E  mov ecx,[edi+0A8h] */
    ecx = M32((edi+0xA8u));
    /* 00070134  test ecx,ecx */
    { uint32_t r_ = ecx & ecx; FLAGS(XK_LOGIC, 0, 0, r_, 32); }
    /* 00070136  je near ptr 000701C8h */
    if (FZ) goto L_000701C8;
L_0007013C:
    /* 0007013C  cmp word ptr [ecx+24h],0Ah */
    { uint16_t a_ = M16((ecx+0x24u)), b_ = 0xAu; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 00070141  jne short 00070171h */
    if (!FZ) goto L_00070171;
L_00070143:
    /* 00070143  mov ax,[ecx+2Ch] */
    SET16(eax, M16((ecx+0x2Cu)));
    /* 00070147  cmp ax,1 */
    { uint16_t a_ = R16(eax), b_ = 0x1u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 0007014B  jl short 00070171h */
    if ((FS!=FO)) goto L_00070171;
L_0007014D:
    /* 0007014D  cmp ax,4 */
    { uint16_t a_ = R16(eax), b_ = 0x4u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 00070151  jg short 00070171h */
    if ((!FZ&&(FS==FO))) goto L_00070171;
L_00070153:
    /* 00070153  mov edx,[edi+0B0h] */
    edx = M32((edi+0xB0u));
    /* 00070159  test edx,edx */
    { uint32_t r_ = edx & edx; FLAGS(XK_LOGIC, 0, 0, r_, 32); }
    /* 0007015B  je short 00070171h */
    if (FZ) goto L_00070171;
L_0007015D:
    /* 0007015D  movsx eax,ax */
    eax = (uint32_t)((int16_t)R16(eax));
    /* 00070160  fld dword ptr [edx+eax*4-4] */
    XS(7) = LDF((edx+(eax*4)+0xFFFFFFFCu)); XD(7);
    /* 00070164  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 0007016A  fnstsw ax */
    SET16(eax, fsw);
    /* 0007016C  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 0007016F  jnp short 000701C8h */
    if (!n70_ccj(cc, 0x44u, 'P')) goto L_000701C8;
L_00070171: DECLINE();
L_000701C8:
    /* 000701C8  mov ax,ds:[2E3528h] */
    SET16(eax, K16(0x2E3528u));
    /* 000701CE  cmp ax,1 */
    { uint16_t a_ = R16(eax), b_ = 0x1u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 000701D2  jne short 00070220h */
    if (!FZ) goto L_00070220;
L_000701D4: DECLINE();
L_00070220:
    /* 00070220  test ax,ax */
    { uint16_t r_ = R16(eax) & R16(eax); FLAGS(XK_LOGIC, 0, 0, r_, 16); }
    /* 00070223  jne near ptr 00070EE8h */
    if (FZ == 0) DECLINE();
L_00070229:
    /* 00070229  cmp word ptr [ebp+24h],3 */
    { uint16_t a_ = MB16(0x24u), b_ = 0x3u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 0007022E  jne short 00070265h */
    if (!FZ) goto L_00070265;
L_00070230: DECLINE();
L_00070265:
    SCOPE_OPEN();
    /* 00070265  fld dword ptr [edi+0B4h] */
    XS(7) = LDF((edi+0xB4u)); XD(7);
    /* 0007026B  mov bl,[ebp+28h] */
    SET8L(ebx, MB8(0x28u));
    /* 0007026E  fsub dword ptr ds:[2FC6C8h] */
    XS(7) = XS(7) - KLDF(0x2FC6C8u); XD(7);
    /* 00070274  shr bl,3 */
    SET8L(ebx, SHR8(R8L(ebx), 0x3u));
    /* 00070277  fld dword ptr [edi+0B8h] */
    XS(6) = LDF((edi+0xB8u)); XD(6);
    /* 0007027D  and bl,1 */
    { uint8_t a_ = R8L(ebx), b_ = 0x1u; uint8_t r_ = (uint8_t)(a_ & b_); SET8L(ebx, r_); }
    /* 00070280  fsub dword ptr ds:[2FC6CCh] */
    XS(6) = XS(6) - KLDF(0x2FC6CCu); XD(6);
    /* 00070286  fld dword ptr [edi+0BCh] */
    XS(5) = LDF((edi+0xBCu)); XD(5);
    /* 0007028C  fsub dword ptr ds:[2FC6D0h] */
    XS(5) = XS(5) - KLDF(0x2FC6D0u); XD(5);
    /* 00070292  fxch */
    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);
    /* 00070294  fmul dword ptr ds:[2FC6D8h] */
    XS(5) = XS(5) * KLDF(0x2FC6D8u); XD(5);
    /* 0007029A  fxch */
    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);
    /* 0007029C  fmul dword ptr ds:[2FC6DCh] */
    XS(5) = XS(5) * KLDF(0x2FC6DCu); XD(5);
    /* 000702A2  faddp */
    XS(6) = XS(6) + XS(5); XD(6);
    /* pop */
    /* 000702A4  fxch */
    { double t_ = XS(6); XS(6) = XS(7); XS(7) = t_; } XD(6); XD(7);
    /* 000702A6  fmul dword ptr ds:[2FC6D4h] */
    XS(6) = XS(6) * KLDF(0x2FC6D4u); XD(6);
    /* 000702AC  faddp */
    XS(7) = XS(7) + XS(6); XD(7);
    /* pop */
    /* 000702AE  fstp dword ptr [esp+48h] */
    SSTF((esp+0x48u), XS(7));
    /* pop */
    /* 000702B2  fld dword ptr [ebp+140h] */
    XS(7) = MBF(0x140u); XD(7);
    /* 000702B8  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 000702BE  fnstsw ax */
    SET16(eax, fsw);
    /* 000702C0  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000702C3  jnp short 00070305h */
    if (!n70_ccj(cc, 0x44u, 'P')) goto L_00070305;
L_000702C5:
    /* 000702C5  fld dword ptr [esp+48h] */
    XS(7) = SLDF((esp+0x48u)); XD(7);
    /* 000702C9  fsub dword ptr [ebp+140h] */
    XS(7) = XS(7) - MBF(0x140u); XD(7);
    /* 000702CF  fld dword ptr [ebp+13Ch] */
    XS(6) = MBF(0x13Cu); XD(6);
    /* 000702D5  fsub dword ptr [ebp+140h] */
    XS(6) = XS(6) - MBF(0x140u); XD(6);
    /* 000702DB  fdivp */
    XS(7) = XS(7) / XS(6); XD(7);
    /* pop */
    /* 000702DD  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 000702E3  fnstsw ax */
    SET16(eax, fsw);
    /* 000702E5  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000702E8  jp short 000702F6h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_000702F6;
L_000702EA:
    /* 000702EA  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 000702EC  mov dword ptr [esp+10h],0 */
    SW32((esp+0x10u), 0x0u);
    /* 000702F4  jmp short 0007030Dh */
    goto L_0007030D;
L_000702F6:
    /* 000702F6  fcom dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* 000702FC  fnstsw ax */
    SET16(eax, fsw);
    /* 000702FE  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070301  jne short 0007031Dh */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_0007031D;
L_00070303:
    /* 00070303  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070305  mov dword ptr [esp+10h],3F800000h */
    SW32((esp+0x10u), 0x3F800000u);
    goto L_0007030D;
L_00070305:
    /* 00070305  mov dword ptr [esp+10h],3F800000h */
    SW32((esp+0x10u), 0x3F800000u);
L_0007030D:
    /* 0007030D  test byte ptr [edi],8 */
    { uint8_t r_ = M8((edi)) & 0x8u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070310  je short 00070323h */
    if (FZ) goto L_00070323;
L_00070312:
    /* 00070312  push 0 */
    PUSH(0x0u);
    /* 00070314  call 00182D50h */
    PUSH(0x70319u);
    HLE(1, 0x182D50u, D3DDevice_SetRenderState_ZEnable, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 00070319  push 0 */
    PUSH(0x0u);
    /* 0007031B  jmp short 00070368h */
    goto L_00070368;
L_0007031D:
    /* 0007031D  fstp dword ptr [esp+10h] */
    SSTF((esp+0x10u), XS(7));
    /* pop */
    /* 00070321  jmp short 0007030Dh */
    PREEMPT(2, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7));
    goto L_0007030D;
L_00070323:
    /* 00070323  push 1 */
    PUSH(0x1u);
    /* 00070325  call 00182D50h */
    PUSH(0x7032Au);
    HLE(3, 0x182D50u, D3DDevice_SetRenderState_ZEnable, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 0007032A  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 0007032C  test bl,bl */
    { uint8_t r_ = R8L(ebx) & R8L(ebx); FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 0007032E  sete cl */
    SET8L(ecx, FZ ? 1 : 0);
    /* 00070331  mov edi,ecx */
    edi = ecx;
    /* 00070333  mov edx,edi */
    edx = edi;
    /* 00070335  mov ecx,4035Ch */
    ecx = 0x4035Cu;
    /* 0007033A  call 001820A0h */
    PUSH(0x7033Fu);
    HLE(4, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 0007033F  mov edx,203h */
    edx = 0x203u;
    /* 00070344  mov ecx,40354h */
    ecx = 0x40354u;
    /* 00070349  mov ds:[18F480h],edi */
    KW32(0x18F480u, edi);
    /* 0007034F  call 001820A0h */
    PUSH(0x70354u);
    HLE(5, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 00070354  mov dl,bl */
    SET8L(edx, R8L(ebx));
    /* 00070356  neg dl */
    { uint8_t a_ = R8L(edx); uint8_t r_ = (uint8_t)(0 - a_); FLAGS(XK_SUB, 0, a_, r_, 8); SET8L(edx, r_); }
    /* 00070358  mov dword ptr ds:[18F464h],203h */
    KW32(0x18F464u, 0x203u);
    /* 00070362  sbb edx,edx */
    { uint32_t a_ = edx, b_ = edx; uint32_t cf_ = FC; uint32_t r_ = (uint32_t)(a_ - b_ - cf_); FLAGS_C(XK_SBB, a_, b_, r_, 32, cf_); edx = r_; }
    /* 00070364  and edx,8 */
    { uint32_t a_ = edx, b_ = 0x8u; uint32_t r_ = (uint32_t)(a_ & b_); FLAGS(XK_LOGIC, a_, b_, r_, 32); edx = r_; }
    /* 00070367  push edx */
    PUSH(edx);
L_00070368:
    /* 00070368  call 00182440h */
    PUSH(0x7036Du);
    HLE(6, 0x182440u, D3DDevice_SetRenderState_ZBias, 0xb0, 0, (ST_eax ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_edx ST_esp));
    /* 0007036D  push 901h */
    PUSH(0x901u);
    /* 00070372  call 00182230h */
    PUSH(0x70377u);
    HLE(7, 0x182230u, D3DDevice_SetRenderState_CullMode, 0xb0, 0, (ST_eax ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_esp));
    /* 00070377  mov edx,10101h */
    edx = 0x10101u;
    /* 0007037C  mov ecx,40358h */
    ecx = 0x40358u;
    /* 00070381  call 001820A0h */
    PUSH(0x70386u);
    HLE(8, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 00070386  movzx edi,bl */
    edi = (uint32_t)((uint8_t)R8L(ebx));
    /* 00070389  mov edx,edi */
    edx = edi;
    /* 0007038B  mov ecx,40304h */
    ecx = 0x40304u;
    /* 00070390  mov dword ptr ds:[18F48Ch],10101h */
    KW32(0x18F48Cu, 0x10101u);
    /* 0007039A  call 001820A0h */
    PUSH(0x7039Fu);
    HLE(9, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 0007039F  mov edx,302h */
    edx = 0x302u;
    /* 000703A4  mov ecx,40344h */
    ecx = 0x40344u;
    /* 000703A9  mov ds:[18F46Ch],edi */
    KW32(0x18F46Cu, edi);
    /* 000703AF  call 001820A0h */
    PUSH(0x703B4u);
    HLE(10, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 000703B4  mov edx,303h */
    edx = 0x303u;
    /* 000703B9  mov ecx,40348h */
    ecx = 0x40348u;
    /* 000703BE  mov dword ptr ds:[18F478h],302h */
    KW32(0x18F478u, 0x302u);
    /* 000703C8  call 001820A0h */
    PUSH(0x703CDu);
    HLE(11, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 000703CD  mov edx,8006h */
    edx = 0x8006u;
    /* 000703D2  mov ecx,40350h */
    ecx = 0x40350u;
    /* 000703D7  mov dword ptr ds:[18F47Ch],303h */
    KW32(0x18F47Cu, 0x303u);
    /* 000703E1  call 001820A0h */
    PUSH(0x703E6u);
    HLE(12, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 000703E6  test bl,bl */
    { uint8_t r_ = R8L(ebx) & R8L(ebx); FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000703E8  mov dword ptr ds:[18F4A8h],8006h */
    KW32(0x18F4A8u, 0x8006u);
    /* 000703F2  jne short 00070401h */
    if (!FZ) goto L_00070401;
L_000703F4:
    /* 000703F4  test byte ptr [ebp+28h],4 */
    { uint8_t r_ = MB8(0x28u) & 0x4u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000703F8  jne short 00070401h */
    if (!FZ) goto L_00070401;
L_000703FA:
    /* 000703FA  mov edi,1 */
    edi = 0x1u;
    /* 000703FF  jmp short 00070403h */
    goto L_00070403;
L_00070401:
    /* 00070401  xor edi,edi */
    { uint32_t a_ = edi, b_ = edi; uint32_t r_ = (uint32_t)(a_ ^ b_); FLAGS(XK_LOGIC, a_, b_, r_, 32); edi = r_; }
L_00070403:
    /* 00070403  mov edx,edi */
    edx = edi;
    /* 00070405  mov ecx,40300h */
    ecx = 0x40300u;
    /* 0007040A  call 001820A0h */
    PUSH(0x7040Fu);
    HLE(13, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 0007040F  mov edx,7Fh */
    edx = 0x7Fu;
    /* 00070414  mov ecx,40340h */
    ecx = 0x40340u;
    /* 00070419  mov ds:[18F470h],edi */
    KW32(0x18F470u, edi);
    /* 0007041F  call 001820A0h */
    PUSH(0x70424u);
    HLE(14, 0x1820A0u, D3DDevice_SetRenderState_Simple, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7), (ST_ecx ST_edx ST_esp));
    /* 00070424  push esi */
    PUSH(esi);
    /* 00070425  push 1 */
    PUSH(0x1u);
    /* 00070427  push 0 */
    PUSH(0x0u);
    /* 00070429  mov dword ptr ds:[18F474h],7Fh */
    KW32(0x18F474u, 0x7Fu);
    /* 00070433  mov ecx,[ebp+0B0h] */
    ecx = MB32(0xB0u);
    /* 00070439  push 0 */
    PUSH(0x0u);
    /* 0007043B  call 00080360h */
    PUSH(0x70440u);
    CALL(15, f_00080360, 0xb0, 0, 0, (ST_eax ST_ecx ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x5 ST_x6 ST_x7));
    SAMPLER(16, 0u, S_16, 0xb0, 0, 0, (ST_NONE));
    /* 00070440  push 1 */
    PUSH(0x1u);
    /* 00070442  mov edx,0Ah */
    edx = 0xAu;
    /* 00070447  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070449  call 00182150h */
    PUSH(0x7044Eu);
    HLE(17, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 0007044E  push 1 */
    PUSH(0x1u);
    /* 00070450  mov edx,0Bh */
    edx = 0xBu;
    /* 00070455  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070457  call 00182150h */
    PUSH(0x7045Cu);
    HLE(18, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 0007045C  push 2 */
    PUSH(0x2u);
    /* 0007045E  mov edx,0Dh */
    edx = 0xDu;
    /* 00070463  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070465  call 00182150h */
    PUSH(0x7046Au);
    HLE(19, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 0007046A  push 2 */
    PUSH(0x2u);
    /* 0007046C  mov edx,0Eh */
    edx = 0xEu;
    /* 00070471  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070473  call 00182150h */
    PUSH(0x70478u);
    HLE(20, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070478  push 2 */
    PUSH(0x2u);
    /* 0007047A  mov edx,0Fh */
    edx = 0xFu;
    /* 0007047F  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070481  call 00182150h */
    PUSH(0x70486u);
    HLE(21, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
S_16:;
    /* 00070486  mov ecx,[ebp+0E8h] */
    ecx = MB32(0xE8u);
    /* 0007048C  push esi */
    PUSH(esi);
    /* 0007048D  push 2 */
    PUSH(0x2u);
    /* 0007048F  push 0 */
    PUSH(0x0u);
    /* 00070491  push 1 */
    PUSH(0x1u);
    /* 00070493  call 00080360h */
    PUSH(0x70498u);
    CALL(22, f_00080360, 0xb0, 0, 0, (ST_ecx ST_esp));
    SAMPLER(23, 1u, S_23, 0xb0, 0, 0, (ST_NONE));
    /* 00070498  push 1 */
    PUSH(0x1u);
    /* 0007049A  mov edx,0Ah */
    edx = 0xAu;
    /* 0007049F  mov ecx,1 */
    ecx = 0x1u;
    /* 000704A4  call 00182150h */
    PUSH(0x704A9u);
    HLE(24, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000704A9  push 1 */
    PUSH(0x1u);
    /* 000704AB  mov edx,0Bh */
    edx = 0xBu;
    /* 000704B0  mov ecx,1 */
    ecx = 0x1u;
    /* 000704B5  call 00182150h */
    PUSH(0x704BAu);
    HLE(25, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000704BA  push 2 */
    PUSH(0x2u);
    /* 000704BC  mov edx,0Dh */
    edx = 0xDu;
    /* 000704C1  mov ecx,1 */
    ecx = 0x1u;
    /* 000704C6  call 00182150h */
    PUSH(0x704CBu);
    HLE(26, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000704CB  push 2 */
    PUSH(0x2u);
    /* 000704CD  mov edx,0Eh */
    edx = 0xEu;
    /* 000704D2  mov ecx,1 */
    ecx = 0x1u;
    /* 000704D7  call 00182150h */
    PUSH(0x704DCu);
    HLE(27, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000704DC  push 2 */
    PUSH(0x2u);
    /* 000704DE  mov edx,0Fh */
    edx = 0xFu;
    /* 000704E3  mov ecx,1 */
    ecx = 0x1u;
    /* 000704E8  call 00182150h */
    PUSH(0x704EDu);
    HLE(28, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
S_23:;
    /* 000704ED  mov ecx,[ebp+0C8h] */
    ecx = MB32(0xC8u);
    /* 000704F3  push esi */
    PUSH(esi);
    /* 000704F4  push 1 */
    PUSH(0x1u);
    /* 000704F6  push 0 */
    PUSH(0x0u);
    /* 000704F8  push 2 */
    PUSH(0x2u);
    /* 000704FA  call 00080360h */
    PUSH(0x704FFu);
    CALL(29, f_00080360, 0xb0, 0, 0, (ST_ecx ST_esp));
    SAMPLER(30, 2u, S_30, 0xb0, 0, 0, (ST_NONE));
    /* 000704FF  push 1 */
    PUSH(0x1u);
    /* 00070501  mov edx,0Ah */
    edx = 0xAu;
    /* 00070506  mov ecx,2 */
    ecx = 0x2u;
    /* 0007050B  call 00182150h */
    PUSH(0x70510u);
    HLE(31, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070510  push 1 */
    PUSH(0x1u);
    /* 00070512  mov edx,0Bh */
    edx = 0xBu;
    /* 00070517  mov ecx,2 */
    ecx = 0x2u;
    /* 0007051C  call 00182150h */
    PUSH(0x70521u);
    HLE(32, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070521  push 2 */
    PUSH(0x2u);
    /* 00070523  mov edx,0Dh */
    edx = 0xDu;
    /* 00070528  mov ecx,2 */
    ecx = 0x2u;
    /* 0007052D  call 00182150h */
    PUSH(0x70532u);
    HLE(33, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070532  push 2 */
    PUSH(0x2u);
    /* 00070534  mov edx,0Eh */
    edx = 0xEu;
    /* 00070539  mov ecx,2 */
    ecx = 0x2u;
    /* 0007053E  call 00182150h */
    PUSH(0x70543u);
    HLE(34, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070543  push 2 */
    PUSH(0x2u);
    /* 00070545  mov edx,0Fh */
    edx = 0xFu;
    /* 0007054A  mov ecx,2 */
    ecx = 0x2u;
    /* 0007054F  call 00182150h */
    PUSH(0x70554u);
    HLE(35, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
S_30:;
    /* 00070554  mov ecx,[ebp+170h] */
    ecx = MB32(0x170u);
    /* 0007055A  push esi */
    PUSH(esi);
    /* 0007055B  push 0 */
    PUSH(0x0u);
    /* 0007055D  push 2 */
    PUSH(0x2u);
    /* 0007055F  push 3 */
    PUSH(0x3u);
    /* 00070561  call 00080360h */
    PUSH(0x70566u);
    CALL(36, f_00080360, 0xb0, 0, 0, (ST_ecx ST_esp));
    SAMPLER(37, 3u, S_37, 0xb0, 0, 0, (ST_NONE));
    /* 00070566  push 3 */
    PUSH(0x3u);
    /* 00070568  mov edx,0Ah */
    edx = 0xAu;
    /* 0007056D  mov ecx,3 */
    ecx = 0x3u;
    /* 00070572  call 00182150h */
    PUSH(0x70577u);
    HLE(38, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070577  push 3 */
    PUSH(0x3u);
    /* 00070579  mov edx,0Bh */
    edx = 0xBu;
    /* 0007057E  mov ecx,3 */
    ecx = 0x3u;
    /* 00070583  call 00182150h */
    PUSH(0x70588u);
    HLE(39, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070588  push 3 */
    PUSH(0x3u);
    /* 0007058A  mov edx,0Ch */
    edx = 0xCu;
    /* 0007058F  mov ecx,3 */
    ecx = 0x3u;
    /* 00070594  call 00182150h */
    PUSH(0x70599u);
    HLE(40, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070599  push 2 */
    PUSH(0x2u);
    /* 0007059B  mov edx,0Dh */
    edx = 0xDu;
    /* 000705A0  mov ecx,3 */
    ecx = 0x3u;
    /* 000705A5  call 00182150h */
    PUSH(0x705AAu);
    HLE(41, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000705AA  push 2 */
    PUSH(0x2u);
    /* 000705AC  mov edx,0Eh */
    edx = 0xEu;
    /* 000705B1  mov ecx,3 */
    ecx = 0x3u;
    /* 000705B6  call 00182150h */
    PUSH(0x705BBu);
    HLE(42, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 000705BB  push 2 */
    PUSH(0x2u);
    /* 000705BD  mov edx,0Fh */
    edx = 0xFu;
    /* 000705C2  mov ecx,3 */
    ecx = 0x3u;
    /* 000705C7  call 00182150h */
    PUSH(0x705CCu);
    HLE(43, 0x182150u, D3DDevice_SetTextureState_Deferred, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
S_37:;
    /* 000705CC  mov edi,ds:[2E3520h] */
    edi = K32(0x2E3520u);
    /* 000705D2  mov eax,[edi+4] */
    eax = M32((edi+0x4u));
    /* 000705D5  mov [esp+9Ch],eax */
    SW32((esp+0x9Cu), eax);
    /* 000705DC  test byte ptr [ebp+6Ch],1 */
    { uint8_t r_ = MB8(0x6Cu) & 0x1u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000705E0  je short 000705EAh */
    if (FZ) goto L_000705EA;
L_000705E2:
    /* 000705E2  fld dword ptr ds:[1F0A68h] */
    XS(7) = KLDF(0x1F0A68u); XD(7);
    /* 000705E8  jmp short 000705F6h */
    goto L_000705F6;
L_000705EA:
    /* 000705EA  lea ecx,[esp+9Ch] */
    ecx = (esp+0x9Cu);
    /* 000705F1  call 000B5130h */
    PUSH(0x705F6u);
    CALL(44, f_000B5130, 0xb0, 0, 1, (ST_eax ST_ecx ST_esp ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo));
L_000705F6:
    /* 000705F6  fld dword ptr [ebp+84h] */
    XS(6) = MBF(0x84u); XD(6);
    /* 000705FC  lea esi,[ebp+78h] */
    esi = (ebp+0x78u);
    /* 000705FF  fsub dword ptr [esi] */
    XS(6) = XS(6) - LDF((esi)); XD(6);
    /* 00070601  push ecx */
    PUSH(ecx);
    /* 00070602  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070604  mov cx,[ebp+72h] */
    SET16(ecx, MB16(0x72u));
    /* 00070608  fstp dword ptr [esp+28h] */
    SSTF((esp+0x28u), XS(6));
    /* pop */
    /* 0007060C  fld dword ptr [ebp+88h] */
    XS(6) = MBF(0x88u); XD(6);
    /* 00070612  fsub dword ptr [esi+4] */
    XS(6) = XS(6) - LDF((esi+0x4u)); XD(6);
    /* 00070615  fstp dword ptr [esp+2Ch] */
    SSTF((esp+0x2Cu), XS(6));
    /* pop */
    /* 00070619  fld dword ptr [ebp+8Ch] */
    XS(6) = MBF(0x8Cu); XD(6);
    /* 0007061F  fsub dword ptr [esi+8] */
    XS(6) = XS(6) - LDF((esi+0x8u)); XD(6);
    /* 00070622  fstp dword ptr [esp+30h] */
    SSTF((esp+0x30u), XS(6));
    /* pop */
    /* 00070626  fld dword ptr ds:[2FC918h] */
    XS(6) = KLDF(0x2FC918u); XD(6);
    /* 0007062C  fdiv dword ptr [ebp+74h] */
    XS(6) = XS(6) / MBF(0x74u); XD(6);
    /* 0007062F  fadd st,st(1) */
    XS(6) = XS(6) + XS(7); XD(6);
    /* 00070631  fstp dword ptr [esp] */
    SSTF((esp), XS(6));
    /* pop */
    /* 00070634  push ecx */
    PUSH(ecx);
    /* 00070635  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070637  call 00173F20h */
    PUSH(0x7063Cu);
    CALL(45, f_00173F20, 0xb0, 0, 1, (ST_eax ST_ecx ST_esp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_x6 ST_x7));
    /* 0007063C  push ecx */
    PUSH(ecx);
    /* 0007063D  fstp dword ptr [esp] */
    SSTF((esp), XS(7));
    /* pop */
    /* 00070640  lea eax,[esp+34h] */
    eax = (esp+0x34u);
    /* 00070644  lea ecx,[esp+28h] */
    ecx = (esp+0x28u);
    /* 00070648  mov edx,esi */
    edx = esi;
    /* 0007064A  call 000111A0h */
    PUSH(0x7064Fu);
    CALL(46, f_000111A0, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp));
    /* 0007064F  mov ax,[ebp+70h] */
    SET16(eax, MB16(0x70u));
    /* 00070653  test ax,ax */
    { uint16_t r_ = R16(eax) & R16(eax); FLAGS(XK_LOGIC, 0, 0, r_, 16); }
    /* 00070656  jle short 0007068Eh */
    if ((FZ||(FS!=FO))) goto L_0007068E;
L_00070658:
    /* 00070658  cmp ax,5 */
    { uint16_t a_ = R16(eax), b_ = 0x5u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 0007065C  jge short 0007068Eh */
    if ((FS==FO)) goto L_0007068E;
L_0007065E:
    /* 0007065E  fld dword ptr [esp+30h] */
    XS(7) = SLDF((esp+0x30u)); XD(7);
    /* 00070662  movsx eax,ax */
    eax = (uint32_t)((int16_t)R16(eax));
    /* 00070665  lea edx,[eax+eax*2] */
    edx = (eax+(eax*2));
    /* 00070668  mov eax,[edi+84h] */
    eax = M32((edi+0x84u));
    /* 0007066E  lea eax,[eax+edx*4-0Ch] */
    eax = (eax+(edx*4)+0xFFFFFFF4u);
    /* 00070672  fmul dword ptr [eax] */
    XS(7) = XS(7) * LDF((eax)); XD(7);
    /* 00070674  fstp dword ptr [esp+30h] */
    SSTF((esp+0x30u), XS(7));
    /* pop */
    /* 00070678  fld dword ptr [esp+34h] */
    XS(7) = SLDF((esp+0x34u)); XD(7);
    /* 0007067C  fmul dword ptr [eax+4] */
    XS(7) = XS(7) * LDF((eax+0x4u)); XD(7);
    /* 0007067F  fstp dword ptr [esp+34h] */
    SSTF((esp+0x34u), XS(7));
    /* pop */
    /* 00070683  fld dword ptr [esp+38h] */
    XS(7) = SLDF((esp+0x38u)); XD(7);
    /* 00070687  fmul dword ptr [eax+8] */
    XS(7) = XS(7) * LDF((eax+0x8u)); XD(7);
    /* 0007068A  fstp dword ptr [esp+38h] */
    SSTF((esp+0x38u), XS(7));
    /* pop */
    /* 0007068E  mov ax,[ebp+4Ch] */
    SET16(eax, MB16(0x4Cu));
    /* 00070692  test ax,ax */
    { uint16_t r_ = R16(eax) & R16(eax); FLAGS(XK_LOGIC, 0, 0, r_, 16); }
    /* 00070695  jle short 000706C3h */
    if ((FZ||(FS!=FO))) goto L_000706C3;
    goto L_00070697;
L_0007068E:
    /* 0007068E  mov ax,[ebp+4Ch] */
    SET16(eax, MB16(0x4Cu));
    /* 00070692  test ax,ax */
    { uint16_t r_ = R16(eax) & R16(eax); FLAGS(XK_LOGIC, 0, 0, r_, 16); }
    /* 00070695  jle short 000706C3h */
    if ((FZ||(FS!=FO))) goto L_000706C3;
L_00070697:
    /* 00070697  cmp ax,5 */
    { uint16_t a_ = R16(eax), b_ = 0x5u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 0007069B  jge short 000706C3h */
    if ((FS==FO)) goto L_000706C3;
L_0007069D:
    /* 0007069D  mov edx,[edi+84h] */
    edx = M32((edi+0x84u));
    /* 000706A3  movsx eax,ax */
    eax = (uint32_t)((int16_t)R16(eax));
    /* 000706A6  lea ecx,[eax+eax*2] */
    ecx = (eax+(eax*2));
    /* 000706A9  lea eax,[edx+ecx*4-0Ch] */
    eax = (edx+(ecx*4)+0xFFFFFFF4u);
    /* 000706AD  mov ecx,[eax] */
    ecx = M32((eax));
    /* 000706AF  mov [esp+3Ch],ecx */
    SW32((esp+0x3Cu), ecx);
    /* 000706B3  mov edx,[eax+4] */
    edx = M32((eax+0x4u));
    /* 000706B6  mov [esp+40h],edx */
    SW32((esp+0x40u), edx);
    /* 000706BA  mov eax,[eax+8] */
    eax = M32((eax+0x8u));
    /* 000706BD  mov [esp+44h],eax */
    SW32((esp+0x44u), eax);
    /* 000706C1  jmp short 000706DDh */
    goto L_000706DD;
L_000706C3:
    /* 000706C3  mov ecx,ds:[232F6Ch] */
    ecx = K32(0x232F6Cu);
    /* 000706C9  mov edx,[ecx] */
    edx = M32((ecx));
    /* 000706CB  mov [esp+3Ch],edx */
    SW32((esp+0x3Cu), edx);
    /* 000706CF  mov eax,[ecx+4] */
    eax = M32((ecx+0x4u));
    /* 000706D2  mov [esp+40h],eax */
    SW32((esp+0x40u), eax);
    /* 000706D6  mov ecx,[ecx+8] */
    ecx = M32((ecx+0x8u));
    /* 000706D9  mov [esp+44h],ecx */
    SW32((esp+0x44u), ecx);
L_000706DD:
    /* 000706DD  test byte ptr [ebp+28h],10h */
    { uint8_t r_ = MB8(0x28u) & 0x10u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000706E1  jne near ptr 000707B4h */
    if (!FZ) goto L_000707B4;
L_000706E7:
    /* 000706E7  mov al,ds:[2E352Bh] */
    SET8L(eax, K8(0x2E352Bu));
    /* 000706EC  test al,al */
    { uint8_t r_ = R8L(eax) & R8L(eax); FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000706EE  je short 000706F7h */
    if (FZ) goto L_000706F7;
L_000706F0:
    /* 000706F0  xor edx,edx */
    { uint32_t a_ = edx, b_ = edx; uint32_t r_ = (uint32_t)(a_ ^ b_); FLAGS(XK_LOGIC, a_, b_, r_, 32); edx = r_; }
    /* 000706F2  jmp near ptr 000707B9h */
    goto L_000707B9;
L_000706F7:
    /* 000706F7  cmp word ptr [edi+50h],0 */
    { uint16_t a_ = M16((edi+0x50u)), b_ = 0x0u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 000706FC  jle short 00070708h */
    if ((FZ||(FS!=FO))) goto L_00070708;
L_000706FE:
    /* 000706FE  mov edx,1 */
    edx = 0x1u;
    /* 00070703  jmp near ptr 000707B9h */
    goto L_000707B9;
L_00070708:
    /* 00070708  cmp word ptr [edi+0Ch],1 */
    { uint16_t a_ = M16((edi+0xCu)), b_ = 0x1u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 0007070D  jg near ptr 000707B4h */
    if ((!FZ&&(FS==FO))) goto L_000707B4;
L_00070713:
    /* 00070713  mov eax,[ebp+0C8h] */
    eax = MB32(0xC8u);
    /* 00070719  or ecx,0FFFFFFFFh */
    { uint32_t a_ = ecx, b_ = 0xFFFFFFFFu; uint32_t r_ = (uint32_t)(a_ | b_); ecx = r_; }
    /* 0007071C  cmp eax,ecx */
    { uint32_t a_ = eax, b_ = ecx; FLAGS(XK_SUB, a_, b_, (uint32_t)(a_-b_), 32); }
    /* 0007071E  je short 00070794h */
    if (FZ) goto L_00070794;
L_00070720:
    /* 00070720  cmp word ptr [ebp+0D6h],0 */
    { uint16_t a_ = MB16(0xD6u), b_ = 0x0u; FLAGS(XK_SUB, a_, b_, (uint16_t)(a_-b_), 16); }
    /* 00070728  jne near ptr 000707B4h */
    if (!FZ) goto L_000707B4;
L_0007072E:
    /* 0007072E  fld dword ptr [esp+30h] */
    XS(7) = SLDF((esp+0x30u)); XD(7);
    /* 00070732  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 00070738  fnstsw ax */
    SET16(eax, fsw);
    /* 0007073A  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 0007073D  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_0007073F:
    /* 0007073F  fld dword ptr [esp+34h] */
    XS(7) = SLDF((esp+0x34u)); XD(7);
    /* 00070743  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 00070749  fnstsw ax */
    SET16(eax, fsw);
    /* 0007074B  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 0007074E  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_00070750:
    /* 00070750  fld dword ptr [esp+38h] */
    XS(7) = SLDF((esp+0x38u)); XD(7);
    /* 00070754  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 0007075A  fnstsw ax */
    SET16(eax, fsw);
    /* 0007075C  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 0007075F  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_00070761:
    /* 00070761  fld dword ptr [esp+3Ch] */
    XS(7) = SLDF((esp+0x3Cu)); XD(7);
    /* 00070765  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 0007076B  fnstsw ax */
    SET16(eax, fsw);
    /* 0007076D  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070770  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_00070772:
    /* 00070772  fld dword ptr [esp+40h] */
    XS(7) = SLDF((esp+0x40u)); XD(7);
    /* 00070776  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 0007077C  fnstsw ax */
    SET16(eax, fsw);
    /* 0007077E  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070781  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_00070783:
    /* 00070783  fld dword ptr [esp+44h] */
    XS(7) = SLDF((esp+0x44u)); XD(7);
    /* 00070787  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 0007078D  fnstsw ax */
    SET16(eax, fsw);
    /* 0007078F  test ah,44h */
    { uint8_t r_ = R8H(eax) & 0x44u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070792  jp short 000707B4h */
    if (n70_ccj(cc, 0x44u, 'P')) goto L_000707B4;
L_00070794:
    /* 00070794  cmp [ebp+170h],ecx */
    { uint32_t a_ = MB32(0x170u), b_ = ecx; FLAGS(XK_SUB, a_, b_, (uint32_t)(a_-b_), 32); }
    /* 0007079A  je short 000707ADh */
    if (FZ) goto L_000707AD;
L_0007079C:
    /* 0007079C  fld dword ptr [esp+10h] */
    XS(7) = SLDF((esp+0x10u)); XD(7);
    /* 000707A0  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 000707A6  fnstsw ax */
    SET16(eax, fsw);
    /* 000707A8  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000707AB  je short 000707B4h */
    if (n70_ccj(cc, 0x41u, 'Z')) goto L_000707B4;
L_000707AD:
    /* 000707AD  mov edx,3 */
    edx = 0x3u;
    /* 000707B2  jmp short 000707B9h */
    goto L_000707B9;
L_000707B4:
    /* 000707B4  mov edx,2 */
    edx = 0x2u;
L_000707B9:
    /* 000707B9  mov eax,[esp+0C8h] */
    eax = SM32((esp+0xC8u));
    /* 000707C0  test eax,eax */
    { uint32_t r_ = eax & eax; FLAGS(XK_LOGIC, 0, 0, r_, 32); }
    /* 000707C2  je short 000707C9h */
    if (FZ) goto L_000707C9;
L_000707C4:
    /* 000707C4  movsx eax,word ptr [eax] */
    eax = (uint32_t)((int16_t)M16((eax)));
    /* 000707C7  jmp short 000707D8h */
    goto L_000707D8;
L_000707C9:
    /* 000707C9  mov ecx,[esp+0CCh] */
    ecx = SM32((esp+0xCCu));
    /* 000707D0  call 000621E0h */
    PUSH(0x707D5u);
    CALL(47, f_000621E0, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x7));
    /* 000707D5  movsx eax,ax */
    eax = (uint32_t)((int16_t)R16(eax));
L_000707D8:
    /* 000707D8  push edx */
    PUSH(edx);
    /* 000707D9  push eax */
    PUSH(eax);
    /* 000707DA  push 0Ah */
    PUSH(0xAu);
    /* 000707DC  call 000658D0h */
    PUSH(0x707E1u);
    CALL(48, f_000658D0, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x7));
    /* 000707E1  fld dword ptr [ebp+144h] */
    XS(7) = MBF(0x144u); XD(7);
    /* 000707E7  mov eax,ds:[2E3520h] */
    eax = K32(0x2E3520u);
    /* 000707EC  fmul dword ptr [eax+5Ch] */
    XS(7) = XS(7) * LDF((eax+0x5Cu)); XD(7);
    /* 000707EF  mov edx,[ebp+0D8h] */
    edx = MB32(0xD8u);
    /* 000707F5  lea esi,[ebp+0FCh] */
    esi = (ebp+0xFCu);
    /* 000707FB  lea edi,[esp+6Ch] */
    edi = (esp+0x6Cu);
    /* 000707FF  fmul dword ptr [esp+10h] */
    XS(7) = XS(7) * SLDF((esp+0x10u)); XD(7);
    /* 00070803  lea ebx,[esp+5Ch] */
    ebx = (esp+0x5Cu);
    /* 00070807  fstp dword ptr [esp+14h] */
    SSTF((esp+0x14u), XS(7));
    /* pop */
    /* 0007080B  fld dword ptr [ebp+148h] */
    XS(7) = MBF(0x148u); XD(7);
    /* 00070811  fmul dword ptr [eax+60h] */
    XS(7) = XS(7) * LDF((eax+0x60u)); XD(7);
    /* 00070814  fstp dword ptr [esp+18h] */
    SSTF((esp+0x18u), XS(7));
    /* pop */
    /* 00070818  fld dword ptr [ebp+14Ch] */
    XS(7) = MBF(0x14Cu); XD(7);
    /* 0007081E  fmul dword ptr [eax+64h] */
    XS(7) = XS(7) * LDF((eax+0x64u)); XD(7);
    /* 00070821  fstp dword ptr [esp+1Ch] */
    SSTF((esp+0x1Cu), XS(7));
    /* pop */
    /* 00070825  fld dword ptr [ebp+150h] */
    XS(7) = MBF(0x150u); XD(7);
    /* 0007082B  fmul dword ptr [eax+68h] */
    XS(7) = XS(7) * LDF((eax+0x68u)); XD(7);
    /* 0007082E  fstp dword ptr [esp+20h] */
    SSTF((esp+0x20u), XS(7));
    /* pop */
    /* 00070832  fld dword ptr [ebp+154h] */
    XS(7) = MBF(0x154u); XD(7);
    /* 00070838  fmul dword ptr [eax+5Ch] */
    XS(7) = XS(7) * LDF((eax+0x5Cu)); XD(7);
    /* 0007083B  fmul dword ptr [esp+10h] */
    XS(7) = XS(7) * SLDF((esp+0x10u)); XD(7);
    /* 0007083F  fld dword ptr [ebp+158h] */
    XS(6) = MBF(0x158u); XD(6);
    /* 00070845  fmul dword ptr [eax+60h] */
    XS(6) = XS(6) * LDF((eax+0x60u)); XD(6);
    /* 00070848  fld dword ptr [ebp+15Ch] */
    XS(5) = MBF(0x15Cu); XD(5);
    /* 0007084E  fmul dword ptr [eax+64h] */
    XS(5) = XS(5) * LDF((eax+0x64u)); XD(5);
    /* 00070851  fld dword ptr [ebp+160h] */
    XS(4) = MBF(0x160u); XD(4);
    /* 00070857  fmul dword ptr [eax+68h] */
    XS(4) = XS(4) * LDF((eax+0x68u)); XD(4);
    /* 0007085A  mov [esp+4Ch],edx */
    SW32((esp+0x4Cu), edx);
    /* 0007085E  mov edx,ds:[2FC918h] */
    edx = K32(0x2FC918u);
    /* 00070864  push edx */
    PUSH(edx);
    /* 00070865  fstp dword ptr [esp+0B0h] */
    SSTF((esp+0xB0u), XS(4));
    /* pop */
    /* 0007086C  mov ecx,[esp+0B0h] */
    ecx = SM32((esp+0xB0u));
    /* 00070873  fld dword ptr [ebp+0D8h] */
    XS(4) = MBF(0xD8u); XD(4);
    /* 00070879  push 0 */
    PUSH(0x0u);
    /* 0007087B  fmul dword ptr [ebp+0ECh] */
    XS(4) = XS(4) * MBF(0xECu); XD(4);
    /* 00070881  mov [esp+9Ch],ecx */
    SW32((esp+0x9Cu), ecx);
    /* 00070888  mov dword ptr [esp+5Ch],3F800000h */
    SW32((esp+0x5Cu), 0x3F800000u);
    /* 00070890  mov dword ptr [esp+60h],3F800000h */
    SW32((esp+0x60u), 0x3F800000u);
    /* 00070898  fstp dword ptr [esp+58h] */
    SSTF((esp+0x58u), XS(4));
    /* pop */
    /* 0007089C  mov dword ptr [esp+64h],3F800000h */
    SW32((esp+0x64u), 0x3F800000u);
    /* 000708A4  fld dword ptr [esp+20h] */
    XS(4) = SLDF((esp+0x20u)); XD(4);
    /* 000708A8  mov dword ptr [esp+68h],0 */
    SW32((esp+0x68u), 0x0u);
    /* 000708B0  fsub st,st(2) */
    XS(4) = XS(4) - XS(6); XD(4);
    /* 000708B2  mov dword ptr [esp+6Ch],0 */
    SW32((esp+0x6Cu), 0x0u);
    /* 000708BA  mov dword ptr [esp+70h],0 */
    SW32((esp+0x70u), 0x0u);
    /* 000708C2  mov dword ptr [esp+74h],0 */
    SW32((esp+0x74u), 0x0u);
    /* 000708CA  fstp dword ptr [esp+84h] */
    SSTF((esp+0x84u), XS(4));
    /* pop */
    /* 000708D1  mov dword ptr [esp+78h],3F800000h */
    SW32((esp+0x78u), 0x3F800000u);
    /* 000708D9  fld dword ptr [esp+24h] */
    XS(4) = SLDF((esp+0x24u)); XD(4);
    /* 000708DD  mov dword ptr [esp+7Ch],0 */
    SW32((esp+0x7Cu), 0x0u);
    /* 000708E5  fsub st,st(1) */
    XS(4) = XS(4) - XS(5); XD(4);
    /* 000708E7  mov dword ptr [esp+80h],0 */
    SW32((esp+0x80u), 0x0u);
    /* 000708F2  push 0 */
    PUSH(0x0u);
    /* 000708F4  push 0 */
    PUSH(0x0u);
    /* 000708F6  fstp dword ptr [esp+90h] */
    SSTF((esp+0x90u), XS(4));
    /* pop */
    /* 000708FD  sub esp,8 */
    { uint32_t a_ = esp, b_ = 0x8u; uint32_t r_ = (uint32_t)(a_ - b_); esp = r_; }
    /* 00070900  fld dword ptr [esp+38h] */
    XS(4) = SLDF((esp+0x38u)); XD(4);
    /* 00070904  lea ecx,[eax+84h] */
    ecx = (eax+0x84u);
    /* 0007090A  fsub dword ptr [esp+0C4h] */
    XS(4) = XS(4) - SLDF((esp+0xC4u)); XD(4);
    /* 00070911  fstp dword ptr [esp+9Ch] */
    SSTF((esp+0x9Cu), XS(4));
    /* pop */
    /* 00070918  fld dword ptr [esp+2Ch] */
    XS(4) = SLDF((esp+0x2Cu)); XD(4);
    /* 0007091C  fsub st,st(3) */
    XS(4) = XS(4) - XS(7); XD(4);
    /* 0007091E  fstp dword ptr [esp+0A0h] */
    SSTF((esp+0xA0u), XS(4));
    /* pop */
    /* 00070925  fxch */
    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);
    /* 00070927  fstp dword ptr [esp+0A4h] */
    SSTF((esp+0xA4u), XS(5));
    /* pop */
    /* 0007092E  fstp dword ptr [esp+0A8h] */
    SSTF((esp+0xA8u), XS(6));
    /* pop */
    /* 00070935  fstp dword ptr [esp+0B0h] */
    SSTF((esp+0xB0u), XS(7));
    /* pop */
    /* 0007093C  fld dword ptr [eax+0C8h] */
    XS(7) = LDF((eax+0xC8u)); XD(7);
    /* 00070942  fmul dword ptr [ebp+0A0h] */
    XS(7) = XS(7) * MBF(0xA0u); XD(7);
    /* 00070948  fstp dword ptr [esp+4] */
    SSTF((esp+0x4u), XS(7));
    /* pop */
    /* 0007094C  fld dword ptr [eax+0C4h] */
    XS(7) = LDF((eax+0xC4u)); XD(7);
    /* 00070952  fmul dword ptr [ebp+9Ch] */
    XS(7) = XS(7) * MBF(0x9Cu); XD(7);
    /* 00070958  fstp dword ptr [esp] */
    SSTF((esp), XS(7));
    /* pop */
    /* 0007095B  call 00056F20h */
    PUSH(0x70960u);
    UVCALL(49, 0u, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_esi ST_edi ST_x4 ST_x5 ST_x6 ST_x7));
    /* 00070960  mov eax,[ebp+38h] */
    eax = MB32(0x38u);
    /* 00070963  mov [esp+74h],eax */
    SW32((esp+0x74u), eax);
    /* 00070967  push 3 */
    PUSH(0x3u);
    /* 00070969  lea ecx,[esp+50h] */
    ecx = (esp+0x50u);
    /* 0007096D  push ecx */
    PUSH(ecx);
    /* 0007096E  push 0FFFFFFACh */
    PUSH(0xFFFFFFACu);
    /* 00070970  call 00183F90h */
    PUSH(0x70975u);
    HLE(50, 0x183F90u, D3DDevice_SetVertexShaderConstant, 0xb0, 0, (ST_eax ST_ecx ST_esp), (ST_ecx ST_esp));
    /* 00070975  push 2 */
    PUSH(0x2u);
    /* 00070977  lea edx,[esp+80h] */
    edx = (esp+0x80u);
    /* 0007097E  push edx */
    PUSH(edx);
    /* 0007097F  push 0FFFFFFAFh */
    PUSH(0xFFFFFFAFu);
    /* 00070981  call 00183F90h */
    PUSH(0x70986u);
    HLE(51, 0x183F90u, D3DDevice_SetVertexShaderConstant, 0xb0, 0, (ST_eax ST_edx ST_esp), (ST_edx ST_esp));
    /* 00070986  mov ecx,ds:[2E3508h] */
    ecx = K32(0x2E3508u);
    /* 0007098C  test ecx,ecx */
    { uint32_t r_ = ecx & ecx; FLAGS(XK_LOGIC, 0, 0, r_, 32); }
    /* 0007098E  je near ptr 00070A34h */
    if (FZ) goto L_00070A34;
L_00070994:
    /* 00070994  fld dword ptr [ecx] */
    XS(7) = LDF((ecx)); XD(7);
    /* 00070996  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 0007099C  fnstsw ax */
    SET16(eax, fsw);
    /* 0007099E  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000709A1  je short 000709D3h */
    if (n70_ccj(cc, 0x41u, 'Z')) goto L_000709D3;
L_000709A3:
    /* 000709A3  fld dword ptr [ecx+4] */
    XS(7) = LDF((ecx+0x4u)); XD(7);
    /* 000709A6  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 000709AC  fnstsw ax */
    SET16(eax, fsw);
    /* 000709AE  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000709B1  je short 000709D3h */
    if (n70_ccj(cc, 0x41u, 'Z')) goto L_000709D3;
L_000709B3:
    /* 000709B3  fld dword ptr [ecx+8] */
    XS(7) = LDF((ecx+0x8u)); XD(7);
    /* 000709B6  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 000709BC  fnstsw ax */
    SET16(eax, fsw);
    /* 000709BE  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000709C1  je short 000709D3h */
    if (n70_ccj(cc, 0x41u, 'Z')) goto L_000709D3;
L_000709C3:
    /* 000709C3  fld dword ptr [ecx+0Ch] */
    XS(7) = LDF((ecx+0xCu)); XD(7);
    /* 000709C6  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 000709CC  fnstsw ax */
    SET16(eax, fsw);
    /* 000709CE  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 000709D1  jne short 00070A34h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070A34;
L_000709D3:
    /* 000709D3  mov dword ptr [esp+7Ch],0 */
    SW32((esp+0x7Cu), 0x0u);
    /* 000709DB  mov dword ptr [esp+80h],0 */
    SW32((esp+0x80u), 0x0u);
    /* 000709E6  mov dword ptr [esp+84h],0 */
    SW32((esp+0x84u), 0x0u);
    /* 000709F1  mov dword ptr [esp+88h],0 */
    SW32((esp+0x88u), 0x0u);
    /* 000709FC  mov eax,[ecx] */
    eax = M32((ecx));
    /* 000709FE  mov [esp+8Ch],eax */
    SW32((esp+0x8Cu), eax);
    /* 00070A05  mov edx,[ecx+4] */
    edx = M32((ecx+0x4u));
    /* 00070A08  mov [esp+90h],edx */
    SW32((esp+0x90u), edx);
    /* 00070A0F  mov eax,[ecx+8] */
    eax = M32((ecx+0x8u));
    /* 00070A12  push 2 */
    PUSH(0x2u);
    /* 00070A14  lea edx,[esp+80h] */
    edx = (esp+0x80u);
    /* 00070A1B  mov [esp+98h],eax */
    SW32((esp+0x98u), eax);
    /* 00070A22  mov ecx,[ecx+0Ch] */
    ecx = M32((ecx+0xCu));
    /* 00070A25  push edx */
    PUSH(edx);
    /* 00070A26  push 0FFFFFFAFh */
    PUSH(0xFFFFFFAFu);
    /* 00070A28  mov [esp+0A4h],ecx */
    SW32((esp+0xA4u), ecx);
    /* 00070A2F  call 00183F90h */
    PUSH(0x70A34u);
    HLE(52, 0x183F90u, D3DDevice_SetVertexShaderConstant, 0xb0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x7), (ST_ecx ST_edx ST_esp));
    /* 00070A34  mov eax,ds:[2E3520h] */
    eax = K32(0x2E3520u);
    /* 00070A39  test byte ptr [eax],4 */
    { uint8_t r_ = M8((eax)) & 0x4u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070A3C  jne near ptr 00070D09h */
    if (!FZ) goto L_00070D09;
    goto L_00070A42;
L_00070A34:
    /* 00070A34  mov eax,ds:[2E3520h] */
    eax = K32(0x2E3520u);
    /* 00070A39  test byte ptr [eax],4 */
    { uint8_t r_ = M8((eax)) & 0x4u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070A3C  jne near ptr 00070D09h */
    if (!FZ) goto L_00070D09;
L_00070A42:
    FOG_BEGIN(53, L_00070D12, 0xb0, 0, 0, (ST_eax ST_ecx ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x7));
    /* 00070A42  mov ecx,2FC6C8h */
    ecx = 0x2FC6C8u;
    /* 00070A47  mov eax,2FC8C8h */
    eax = 0x2FC8C8u;
    /* 00070A4C  call 000118D0h */
    PUSH(0x70A51u);
    CALL(54, f_000118D0, 0xb0, 0, 1, (ST_eax ST_ecx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x7));
    /* 00070A51  fdiv dword ptr ds:[2FC8C0h] */
    XS(7) = XS(7) / KLDF(0x2FC8C0u); XD(7);
    /* 00070A57  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 00070A5D  fnstsw ax */
    SET16(eax, fsw);
    /* 00070A5F  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070A62  jp short 00070A6Eh */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070A6E;
L_00070A64:
    /* 00070A64  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070A66  fld dword ptr ds:[1F0A68h] */
    XS(7) = KLDF(0x1F0A68u); XD(7);
    /* 00070A6C  jmp short 00070A83h */
    goto L_00070A83;
L_00070A6E:
    /* 00070A6E  fcom dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* 00070A74  fnstsw ax */
    SET16(eax, fsw);
    /* 00070A76  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070A79  jne short 00070A83h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070A83;
L_00070A7B:
    /* 00070A7B  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070A7D  fld dword ptr ds:[1F0A78h] */
    XS(7) = KLDF(0x1F0A78u); XD(7);
L_00070A83:
    /* 00070A83  fld dword ptr [esp+48h] */
    XS(6) = SLDF((esp+0x48u)); XD(6);
    /* 00070A87  fsub dword ptr ds:[2FC8BCh] */
    XS(6) = XS(6) - KLDF(0x2FC8BCu); XD(6);
    /* 00070A8D  fld dword ptr ds:[2FC8C0h] */
    XS(5) = KLDF(0x2FC8C0u); XD(5);
    /* 00070A93  fsub dword ptr ds:[2FC8BCh] */
    XS(5) = XS(5) - KLDF(0x2FC8BCu); XD(5);
    /* 00070A99  fdivp */
    XS(6) = XS(6) / XS(5); XD(6);
    /* pop */
    /* 00070A9B  fcom dword ptr ds:[1F0A68h] */
    FCOMT(6, XS(6), KLDF(0x1F0A68u));
    /* 00070AA1  fnstsw ax */
    SET16(eax, fsw);
    /* 00070AA3  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070AA6  jp short 00070AB2h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070AB2;
L_00070AA8:
    /* 00070AA8  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070AAA  fld dword ptr ds:[1F0A68h] */
    XS(6) = KLDF(0x1F0A68u); XD(6);
    /* 00070AB0  jmp short 00070AC7h */
    goto L_00070AC7;
L_00070AB2:
    /* 00070AB2  fcom dword ptr ds:[1F0A78h] */
    FCOMT(6, XS(6), KLDF(0x1F0A78u));
    /* 00070AB8  fnstsw ax */
    SET16(eax, fsw);
    /* 00070ABA  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070ABD  jne short 00070AC7h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070AC7;
L_00070ABF:
    /* 00070ABF  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070AC1  fld dword ptr ds:[1F0A78h] */
    XS(6) = KLDF(0x1F0A78u); XD(6);
L_00070AC7:
    /* 00070AC7  fmul dword ptr ds:[2FC8B8h] */
    XS(6) = XS(6) * KLDF(0x2FC8B8u); XD(6);
    /* 00070ACD  mov al,ds:[2FC8A8h] */
    SET8L(eax, K8(0x2FC8A8u));
    /* 00070AD2  test al,2 */
    { uint8_t r_ = R8L(eax) & 0x2u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070AD4  fstp dword ptr [esp+10h] */
    SSTF((esp+0x10u), XS(6));
    /* pop */
    /* 00070AD8  je short 00070AE2h */
    if (FZ) goto L_00070AE2;
L_00070ADA:
    /* 00070ADA  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070ADC  fld dword ptr ds:[1F0A78h] */
    XS(7) = KLDF(0x1F0A78u); XD(7);
    /* 00070AE2  fld dword ptr ds:[1F0A78h] */
    XS(6) = KLDF(0x1F0A78u); XD(6);
    /* 00070AE8  fsub dword ptr [esp+10h] */
    XS(6) = XS(6) - SLDF((esp+0x10u)); XD(6);
    /* 00070AEC  fstp dword ptr [esp+14h] */
    SSTF((esp+0x14u), XS(6));
    /* pop */
    /* 00070AF0  fld dword ptr ds:[1F0A78h] */
    XS(6) = KLDF(0x1F0A78u); XD(6);
    /* 00070AF6  fsub st,st(1) */
    XS(6) = XS(6) - XS(7); XD(6);
    /* 00070AF8  fld dword ptr ds:[2FC8ACh] */
    XS(5) = KLDF(0x2FC8ACu); XD(5);
    /* 00070AFE  fmul st,st(1) */
    XS(5) = XS(5) * XS(6); XD(5);
    /* 00070B00  fld st(2) */
    XS(4) = XS(7); XD(4);
    /* 00070B02  fmul dword ptr ds:[2FC8D8h] */
    XS(4) = XS(4) * KLDF(0x2FC8D8u); XD(4);
    /* 00070B08  faddp */
    XS(5) = XS(5) + XS(4); XD(5);
    /* pop */
    /* 00070B0A  fmul dword ptr [esp+10h] */
    XS(5) = XS(5) * SLDF((esp+0x10u)); XD(5);
    /* 00070B0E  fsubr dword ptr ds:[2FC8D8h] */
    XS(5) = KLDF(0x2FC8D8u) - XS(5); XD(5);
    /* 00070B14  fstp dword ptr [esp+18h] */
    SSTF((esp+0x18u), XS(5));
    /* pop */
    /* 00070B18  fld dword ptr ds:[2FC8DCh] */
    XS(5) = KLDF(0x2FC8DCu); XD(5);
    /* 00070B1E  fmul st,st(2) */
    XS(5) = XS(5) * XS(7); XD(5);
    /* 00070B20  fld st(1) */
    XS(4) = XS(6); XD(4);
    /* 00070B22  fmul dword ptr ds:[2FC8B0h] */
    XS(4) = XS(4) * KLDF(0x2FC8B0u); XD(4);
    /* 00070B28  faddp */
    XS(5) = XS(5) + XS(4); XD(5);
    /* pop */
    /* 00070B2A  fmul dword ptr [esp+10h] */
    XS(5) = XS(5) * SLDF((esp+0x10u)); XD(5);
    /* 00070B2E  fsubr dword ptr ds:[2FC8DCh] */
    XS(5) = KLDF(0x2FC8DCu) - XS(5); XD(5);
    /* 00070B34  fstp dword ptr [esp+1Ch] */
    SSTF((esp+0x1Cu), XS(5));
    /* pop */
    /* 00070B38  fld dword ptr ds:[2FC8E0h] */
    XS(5) = KLDF(0x2FC8E0u); XD(5);
    /* 00070B3E  fmul st,st(2) */
    XS(5) = XS(5) * XS(7); XD(5);
    /* 00070B40  fxch */
    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);
    /* 00070B42  fmul dword ptr ds:[2FC8B4h] */
    XS(5) = XS(5) * KLDF(0x2FC8B4u); XD(5);
    /* 00070B48  faddp */
    XS(6) = XS(6) + XS(5); XD(6);
    /* pop */
    /* 00070B4A  fmul dword ptr [esp+10h] */
    XS(6) = XS(6) * SLDF((esp+0x10u)); XD(6);
    /* 00070B4E  fsubr dword ptr ds:[2FC8E0h] */
    XS(6) = KLDF(0x2FC8E0u) - XS(6); XD(6);
    /* 00070B54  fstp dword ptr [esp+20h] */
    SSTF((esp+0x20u), XS(6));
    /* pop */
    /* 00070B58  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070B5A  fld dword ptr [esp+18h] */
    XS(7) = SLDF((esp+0x18u)); XD(7);
    /* 00070B5E  fchs */
    XS(7) = -XS(7); XD(7);
    /* 00070B60  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 00070B66  fnstsw ax */
    SET16(eax, fsw);
    /* 00070B68  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070B6B  jp short 00070B79h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070B79;
    goto L_00070B6D;
L_00070AE2:
    /* 00070AE2  fld dword ptr ds:[1F0A78h] */
    XS(6) = KLDF(0x1F0A78u); XD(6);
    /* 00070AE8  fsub dword ptr [esp+10h] */
    XS(6) = XS(6) - SLDF((esp+0x10u)); XD(6);
    /* 00070AEC  fstp dword ptr [esp+14h] */
    SSTF((esp+0x14u), XS(6));
    /* pop */
    /* 00070AF0  fld dword ptr ds:[1F0A78h] */
    XS(6) = KLDF(0x1F0A78u); XD(6);
    /* 00070AF6  fsub st,st(1) */
    XS(6) = XS(6) - XS(7); XD(6);
    /* 00070AF8  fld dword ptr ds:[2FC8ACh] */
    XS(5) = KLDF(0x2FC8ACu); XD(5);
    /* 00070AFE  fmul st,st(1) */
    XS(5) = XS(5) * XS(6); XD(5);
    /* 00070B00  fld st(2) */
    XS(4) = XS(7); XD(4);
    /* 00070B02  fmul dword ptr ds:[2FC8D8h] */
    XS(4) = XS(4) * KLDF(0x2FC8D8u); XD(4);
    /* 00070B08  faddp */
    XS(5) = XS(5) + XS(4); XD(5);
    /* pop */
    /* 00070B0A  fmul dword ptr [esp+10h] */
    XS(5) = XS(5) * SLDF((esp+0x10u)); XD(5);
    /* 00070B0E  fsubr dword ptr ds:[2FC8D8h] */
    XS(5) = KLDF(0x2FC8D8u) - XS(5); XD(5);
    /* 00070B14  fstp dword ptr [esp+18h] */
    SSTF((esp+0x18u), XS(5));
    /* pop */
    /* 00070B18  fld dword ptr ds:[2FC8DCh] */
    XS(5) = KLDF(0x2FC8DCu); XD(5);
    /* 00070B1E  fmul st,st(2) */
    XS(5) = XS(5) * XS(7); XD(5);
    /* 00070B20  fld st(1) */
    XS(4) = XS(6); XD(4);
    /* 00070B22  fmul dword ptr ds:[2FC8B0h] */
    XS(4) = XS(4) * KLDF(0x2FC8B0u); XD(4);
    /* 00070B28  faddp */
    XS(5) = XS(5) + XS(4); XD(5);
    /* pop */
    /* 00070B2A  fmul dword ptr [esp+10h] */
    XS(5) = XS(5) * SLDF((esp+0x10u)); XD(5);
    /* 00070B2E  fsubr dword ptr ds:[2FC8DCh] */
    XS(5) = KLDF(0x2FC8DCu) - XS(5); XD(5);
    /* 00070B34  fstp dword ptr [esp+1Ch] */
    SSTF((esp+0x1Cu), XS(5));
    /* pop */
    /* 00070B38  fld dword ptr ds:[2FC8E0h] */
    XS(5) = KLDF(0x2FC8E0u); XD(5);
    /* 00070B3E  fmul st,st(2) */
    XS(5) = XS(5) * XS(7); XD(5);
    /* 00070B40  fxch */
    { double t_ = XS(5); XS(5) = XS(6); XS(6) = t_; } XD(5); XD(6);
    /* 00070B42  fmul dword ptr ds:[2FC8B4h] */
    XS(5) = XS(5) * KLDF(0x2FC8B4u); XD(5);
    /* 00070B48  faddp */
    XS(6) = XS(6) + XS(5); XD(6);
    /* pop */
    /* 00070B4A  fmul dword ptr [esp+10h] */
    XS(6) = XS(6) * SLDF((esp+0x10u)); XD(6);
    /* 00070B4E  fsubr dword ptr ds:[2FC8E0h] */
    XS(6) = KLDF(0x2FC8E0u) - XS(6); XD(6);
    /* 00070B54  fstp dword ptr [esp+20h] */
    SSTF((esp+0x20u), XS(6));
    /* pop */
    /* 00070B58  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070B5A  fld dword ptr [esp+18h] */
    XS(7) = SLDF((esp+0x18u)); XD(7);
    /* 00070B5E  fchs */
    XS(7) = -XS(7); XD(7);
    /* 00070B60  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 00070B66  fnstsw ax */
    SET16(eax, fsw);
    /* 00070B68  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070B6B  jp short 00070B79h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070B79;
L_00070B6D:
    /* 00070B6D  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070B6F  mov dword ptr [esp+24h],0 */
    SW32((esp+0x24u), 0x0u);
    /* 00070B77  jmp short 00070B96h */
    goto L_00070B96;
L_00070B79:
    /* 00070B79  fcom dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* 00070B7F  fnstsw ax */
    SET16(eax, fsw);
    /* 00070B81  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070B84  jne short 00070B92h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070B92;
L_00070B86:
    /* 00070B86  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070B88  mov dword ptr [esp+24h],3F800000h */
    SW32((esp+0x24u), 0x3F800000u);
    /* 00070B90  jmp short 00070B96h */
    goto L_00070B96;
L_00070B92:
    /* 00070B92  fstp dword ptr [esp+24h] */
    SSTF((esp+0x24u), XS(7));
    /* pop */
L_00070B96:
    /* 00070B96  fld dword ptr [esp+1Ch] */
    XS(7) = SLDF((esp+0x1Cu)); XD(7);
    /* 00070B9A  fchs */
    XS(7) = -XS(7); XD(7);
    /* 00070B9C  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 00070BA2  fnstsw ax */
    SET16(eax, fsw);
    /* 00070BA4  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070BA7  jp short 00070BB5h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070BB5;
L_00070BA9:
    /* 00070BA9  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070BAB  mov dword ptr [esp+28h],0 */
    SW32((esp+0x28u), 0x0u);
    /* 00070BB3  jmp short 00070BD2h */
    goto L_00070BD2;
L_00070BB5:
    /* 00070BB5  fcom dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* 00070BBB  fnstsw ax */
    SET16(eax, fsw);
    /* 00070BBD  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070BC0  jne short 00070BCEh */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070BCE;
L_00070BC2:
    /* 00070BC2  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070BC4  mov dword ptr [esp+28h],3F800000h */
    SW32((esp+0x28u), 0x3F800000u);
    /* 00070BCC  jmp short 00070BD2h */
    goto L_00070BD2;
L_00070BCE:
    /* 00070BCE  fstp dword ptr [esp+28h] */
    SSTF((esp+0x28u), XS(7));
    /* pop */
L_00070BD2:
    /* 00070BD2  fld dword ptr [esp+20h] */
    XS(7) = SLDF((esp+0x20u)); XD(7);
    /* 00070BD6  fchs */
    XS(7) = -XS(7); XD(7);
    /* 00070BD8  fcom dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* 00070BDE  fnstsw ax */
    SET16(eax, fsw);
    /* 00070BE0  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070BE3  jp short 00070BF1h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070BF1;
L_00070BE5:
    /* 00070BE5  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070BE7  mov dword ptr [esp+2Ch],0 */
    SW32((esp+0x2Cu), 0x0u);
    /* 00070BEF  jmp short 00070C0Eh */
    goto L_00070C0E;
L_00070BF1:
    /* 00070BF1  fcom dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* 00070BF7  fnstsw ax */
    SET16(eax, fsw);
    /* 00070BF9  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070BFC  jne short 00070C0Ah */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070C0A;
L_00070BFE:
    /* 00070BFE  fstp st(0) */
    /* st(0) = st(0) */
    /* pop */
    /* 00070C00  mov dword ptr [esp+2Ch],3F800000h */
    SW32((esp+0x2Cu), 0x3F800000u);
    /* 00070C08  jmp short 00070C0Eh */
    goto L_00070C0E;
L_00070C0A:
    /* 00070C0A  fstp dword ptr [esp+2Ch] */
    SSTF((esp+0x2Cu), XS(7));
    /* pop */
L_00070C0E:
    /* 00070C0E  fld dword ptr [esp+18h] */
    XS(7) = SLDF((esp+0x18u)); XD(7);
    /* 00070C12  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 00070C18  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C1A  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070C1D  jp short 00070C29h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070C29;
L_00070C1F:
    /* 00070C1F  mov dword ptr [esp+18h],0 */
    SW32((esp+0x18u), 0x0u);
    /* 00070C27  jmp short 00070C42h */
    goto L_00070C42;
L_00070C29:
    /* 00070C29  fld dword ptr [esp+18h] */
    XS(7) = SLDF((esp+0x18u)); XD(7);
    /* 00070C2D  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 00070C33  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C35  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070C38  jne short 00070C42h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070C42;
L_00070C3A:
    /* 00070C3A  mov dword ptr [esp+18h],3F800000h */
    SW32((esp+0x18u), 0x3F800000u);
L_00070C42:
    /* 00070C42  fld dword ptr [esp+1Ch] */
    XS(7) = SLDF((esp+0x1Cu)); XD(7);
    /* 00070C46  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 00070C4C  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C4E  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070C51  jp short 00070C5Dh */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070C5D;
L_00070C53:
    /* 00070C53  mov dword ptr [esp+1Ch],0 */
    SW32((esp+0x1Cu), 0x0u);
    /* 00070C5B  jmp short 00070C76h */
    goto L_00070C76;
L_00070C5D:
    /* 00070C5D  fld dword ptr [esp+1Ch] */
    XS(7) = SLDF((esp+0x1Cu)); XD(7);
    /* 00070C61  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 00070C67  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C69  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070C6C  jne short 00070C76h */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070C76;
L_00070C6E:
    /* 00070C6E  mov dword ptr [esp+1Ch],3F800000h */
    SW32((esp+0x1Cu), 0x3F800000u);
L_00070C76:
    /* 00070C76  fld dword ptr [esp+20h] */
    XS(7) = SLDF((esp+0x20u)); XD(7);
    /* 00070C7A  fcomp dword ptr ds:[1F0A68h] */
    FCOMT(7, XS(7), KLDF(0x1F0A68u));
    /* pop */
    /* 00070C80  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C82  test ah,5 */
    { uint8_t r_ = R8H(eax) & 0x5u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070C85  jp short 00070C91h */
    if (n70_ccj(cc, 0x5u, 'P')) goto L_00070C91;
L_00070C87:
    /* 00070C87  mov dword ptr [esp+20h],0 */
    SW32((esp+0x20u), 0x0u);
    /* 00070C8F  jmp short 00070CAAh */
    goto L_00070CAA;
L_00070C91:
    /* 00070C91  fld dword ptr [esp+20h] */
    XS(7) = SLDF((esp+0x20u)); XD(7);
    /* 00070C95  fcomp dword ptr ds:[1F0A78h] */
    FCOMT(7, XS(7), KLDF(0x1F0A78u));
    /* pop */
    /* 00070C9B  fnstsw ax */
    SET16(eax, fsw);
    /* 00070C9D  test ah,41h */
    { uint8_t r_ = R8H(eax) & 0x41u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070CA0  jne short 00070CAAh */
    if (!n70_ccj(cc, 0x41u, 'Z')) goto L_00070CAA;
L_00070CA2:
    /* 00070CA2  mov dword ptr [esp+20h],3F800000h */
    SW32((esp+0x20u), 0x3F800000u);
L_00070CAA:
    /* 00070CAA  fld dword ptr [esp+10h] */
    XS(7) = SLDF((esp+0x10u)); XD(7);
    /* 00070CAE  lea ecx,[esp+14h] */
    ecx = (esp+0x14u);
    /* 00070CB2  fmul dword ptr ds:[2FC8ACh] */
    XS(7) = XS(7) * KLDF(0x2FC8ACu); XD(7);
    /* 00070CB8  push ecx */
    PUSH(ecx);
    /* 00070CB9  fstp dword ptr [esp+0A4h] */
    SSTF((esp+0xA4u), XS(7));
    /* pop */
    /* 00070CC0  fld dword ptr ds:[2FC8B0h] */
    XS(7) = KLDF(0x2FC8B0u); XD(7);
    /* 00070CC6  fmul dword ptr [esp+14h] */
    XS(7) = XS(7) * SLDF((esp+0x14u)); XD(7);
    /* 00070CCA  fstp dword ptr [esp+0A8h] */
    SSTF((esp+0xA8u), XS(7));
    /* pop */
    /* 00070CD1  fld dword ptr ds:[2FC8B4h] */
    XS(7) = KLDF(0x2FC8B4u); XD(7);
    /* 00070CD7  fmul dword ptr [esp+14h] */
    XS(7) = XS(7) * SLDF((esp+0x14u)); XD(7);
    /* 00070CDB  fstp dword ptr [esp+0ACh] */
    SSTF((esp+0xACu), XS(7));
    /* pop */
    /* 00070CE2  call 00011B60h */
    PUSH(0x70CE7u);
    CALL(55, f_00011B60, 0xb4, 0, 0, (ST_eax ST_ecx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fsw ST_x4 ST_x5 ST_x6 ST_x7));
    /* 00070CE7  lea edx,[esp+28h] */
    edx = (esp+0x28u);
    /* 00070CEB  push edx */
    PUSH(edx);
    /* 00070CEC  mov ebx,eax */
    ebx = eax;
    /* 00070CEE  call 00011BD0h */
    PUSH(0x70CF3u);
    CALL(56, f_00011BD0, 0xb8, 0, 0, (ST_edx ST_ebx ST_esp));
    /* 00070CF3  mov esi,eax */
    esi = eax;
    /* 00070CF5  lea eax,[esp+0A8h] */
    eax = (esp+0xA8u);
    /* 00070CFC  push eax */
    PUSH(eax);
    /* 00070CFD  call 00011BD0h */
    PUSH(0x70D02u);
    CALL(57, f_00011BD0, 0xbc, 0, 0, (ST_eax ST_esp ST_esi));
    /* 00070D02  add esp,0Ch */
    { uint32_t a_ = esp, b_ = 0xCu; uint32_t r_ = (uint32_t)(a_ + b_); FLAGS(XK_ADD, a_, b_, r_, 32); esp = r_; }
    /* 00070D05  mov edi,eax */
    edi = eax;
    FOG_END(58, 0xb0, 0, 0, (ST_esp ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo));
    /* 00070D07  jmp short 00070D12h */
    goto L_00070D12;
L_00070D09:
    /* 00070D09  mov edi,0FF000000h */
    edi = 0xFF000000u;
    /* 00070D0E  mov esi,edi */
    esi = edi;
    /* 00070D10  mov ebx,edi */
    ebx = edi;
L_00070D12:
    /* 00070D12  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070D14  mov cl,[ebp+28h] */
    SET8L(ecx, MB8(0x28u));
    /* 00070D17  shr cl,4 */
    SET8L(ecx, SHR8(R8L(ecx), 0x4u));
    /* 00070D1A  xor edx,edx */
    { uint32_t a_ = edx, b_ = edx; uint32_t r_ = (uint32_t)(a_ ^ b_); edx = r_; }
    /* 00070D1C  mov dl,[ebp+28h] */
    SET8L(edx, MB8(0x28u));
    /* 00070D1F  xor eax,eax */
    { uint32_t a_ = eax, b_ = eax; uint32_t r_ = (uint32_t)(a_ ^ b_); eax = r_; }
    /* 00070D21  mov ax,[ebp+0D4h] */
    SET16(eax, MB16(0xD4u));
    /* 00070D28  and ecx,0FFFFFF01h */
    { uint32_t a_ = ecx, b_ = 0xFFFFFF01u; uint32_t r_ = (uint32_t)(a_ & b_); ecx = r_; }
    /* 00070D2E  push ecx */
    PUSH(ecx);
    /* 00070D2F  and edx,0FFFFFF01h */
    { uint32_t a_ = edx, b_ = 0xFFFFFF01u; uint32_t r_ = (uint32_t)(a_ & b_); edx = r_; }
    /* 00070D35  push edx */
    PUSH(edx);
    /* 00070D36  push eax */
    PUSH(eax);
    /* 00070D37  push esi */
    PUSH(esi);
    /* 00070D38  lea ecx,[esp+4Ch] */
    ecx = (esp+0x4Cu);
    /* 00070D3C  push ebx */
    PUSH(ebx);
    /* 00070D3D  push ecx */
    PUSH(ecx);
    /* 00070D3E  call 00011BD0h */
    PUSH(0x70D43u);
    CALL(59, f_00011BD0, 0xc8, 0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_fcf ST_fof ST_fsw ST_x7));
    /* 00070D43  add esp,4 */
    { uint32_t a_ = esp, b_ = 0x4u; uint32_t r_ = (uint32_t)(a_ + b_); esp = r_; }
    /* 00070D46  push eax */
    PUSH(eax);
    /* 00070D47  lea edx,[esp+48h] */
    edx = (esp+0x48u);
    /* 00070D4B  push edx */
    PUSH(edx);
    /* 00070D4C  call 00011BD0h */
    PUSH(0x70D51u);
    CALL(60, f_00011BD0, 0xcc, 0, 0, (ST_edx ST_esp));
    /* 00070D51  mov cx,[ebp+0D6h] */
    SET16(ecx, MB16(0xD6u));
    /* 00070D58  add esp,4 */
    { uint32_t a_ = esp, b_ = 0x4u; uint32_t r_ = (uint32_t)(a_ + b_); esp = r_; }
    /* 00070D5B  call 0006F340h */
    PUSH(0x70D60u);
    CALL(61, f_0006F340, 0xb0, 0, 0, (ST_ecx ST_esp));
    /* 00070D60  push 901h */
    PUSH(0x901u);
    /* 00070D65  call 00182230h */
    PUSH(0x70D6Au);
    HLE(62, 0x182230u, D3DDevice_SetRenderState_CullMode, 0xb0, 0, (ST_esp), (ST_esp));
    /* 00070D6A  mov eax,[esp+0CCh] */
    eax = SM32((esp+0xCCu));
    /* 00070D71  mov ecx,[esp+0BCh] */
    ecx = SM32((esp+0xBCu));
    /* 00070D78  mov edx,[esp+0C8h] */
    edx = SM32((esp+0xC8u));
    /* 00070D7F  mov esi,[esp+0C0h] */
    esi = SM32((esp+0xC0u));
    /* 00070D86  push eax */
    PUSH(eax);
    /* 00070D87  mov eax,[esp+0C8h] */
    eax = SM32((esp+0xC8u));
    /* 00070D8E  push ecx */
    PUSH(ecx);
    /* 00070D8F  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070D91  call 0007A960h */
    PUSH(0x70D96u);
    CALL(63, f_0007A960, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_esi));
    /* 00070D96  test byte ptr [ebp+28h],2 */
    { uint8_t r_ = MB8(0x28u) & 0x2u; FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070D9A  je near ptr 00070EAAh */
    if (FZ) goto L_00070EAA;
L_00070DA0:
    /* 00070DA0  fld dword ptr [ebp+0D8h] */
    XS(7) = MBF(0xD8u); XD(7);
    /* 00070DA6  mov eax,ds:[2FC918h] */
    eax = K32(0x2FC918u);
    /* 00070DAB  fmul dword ptr [ebp+0ECh] */
    XS(7) = XS(7) * MBF(0xECu); XD(7);
    /* 00070DB1  mov edx,[ebp+0D8h] */
    edx = MB32(0xD8u);
    /* 00070DB7  push eax */
    PUSH(eax);
    /* 00070DB8  mov eax,ds:[2E3520h] */
    eax = K32(0x2E3520u);
    /* 00070DBD  fstp dword ptr [esp+54h] */
    SSTF((esp+0x54u), XS(7));
    /* pop */
    /* 00070DC1  push 0 */
    PUSH(0x0u);
    /* 00070DC3  mov [esp+54h],edx */
    SW32((esp+0x54u), edx);
    /* 00070DC7  mov dword ptr [esp+5Ch],3F800000h */
    SW32((esp+0x5Cu), 0x3F800000u);
    /* 00070DCF  mov dword ptr [esp+60h],0BF800000h */
    SW32((esp+0x60u), 0xBF800000u);
    /* 00070DD7  mov dword ptr [esp+64h],3F800000h */
    SW32((esp+0x64u), 0x3F800000u);
    /* 00070DDF  mov dword ptr [esp+68h],0 */
    SW32((esp+0x68u), 0x0u);
    /* 00070DE7  mov dword ptr [esp+6Ch],0 */
    SW32((esp+0x6Cu), 0x0u);
    /* 00070DEF  mov dword ptr [esp+70h],0 */
    SW32((esp+0x70u), 0x0u);
    /* 00070DF7  mov dword ptr [esp+74h],0 */
    SW32((esp+0x74u), 0x0u);
    /* 00070DFF  mov dword ptr [esp+78h],3F800000h */
    SW32((esp+0x78u), 0x3F800000u);
    /* 00070E07  mov dword ptr [esp+7Ch],0 */
    SW32((esp+0x7Cu), 0x0u);
    /* 00070E0F  mov dword ptr [esp+80h],0 */
    SW32((esp+0x80u), 0x0u);
    /* 00070E1A  fld dword ptr [eax+0C8h] */
    XS(7) = LDF((eax+0xC8u)); XD(7);
    /* 00070E20  fmul dword ptr [ebp+0A0h] */
    XS(7) = XS(7) * MBF(0xA0u); XD(7);
    /* 00070E26  push 0 */
    PUSH(0x0u);
    /* 00070E28  push 0 */
    PUSH(0x0u);
    /* 00070E2A  sub esp,8 */
    { uint32_t a_ = esp, b_ = 0x8u; uint32_t r_ = (uint32_t)(a_ - b_); esp = r_; }
    /* 00070E2D  fstp dword ptr [esp+4] */
    SSTF((esp+0x4u), XS(7));
    /* pop */
    /* 00070E31  lea ecx,[eax+84h] */
    ecx = (eax+0x84u);
    /* 00070E37  fld dword ptr [eax+0C4h] */
    XS(7) = LDF((eax+0xC4u)); XD(7);
    /* 00070E3D  lea edi,[esp+84h] */
    edi = (esp+0x84u);
    /* 00070E44  fmul dword ptr [ebp+9Ch] */
    XS(7) = XS(7) * MBF(0x9Cu); XD(7);
    /* 00070E4A  lea ebx,[esp+74h] */
    ebx = (esp+0x74u);
    /* 00070E4E  lea esi,[ebp+0FCh] */
    esi = (ebp+0xFCu);
    /* 00070E54  fstp dword ptr [esp] */
    SSTF((esp), XS(7));
    /* pop */
    /* 00070E57  call 00056F20h */
    PUSH(0x70E5Cu);
    UVCALL(64, 1u, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_ebx ST_esp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo ST_x7));
    /* 00070E5C  mov ecx,[ebp+38h] */
    ecx = MB32(0x38u);
    /* 00070E5F  push 3 */
    PUSH(0x3u);
    /* 00070E61  lea edx,[esp+50h] */
    edx = (esp+0x50u);
    /* 00070E65  push edx */
    PUSH(edx);
    /* 00070E66  push 0FFFFFFACh */
    PUSH(0xFFFFFFACu);
    /* 00070E68  mov [esp+80h],ecx */
    SW32((esp+0x80u), ecx);
    /* 00070E6F  call 00183F90h */
    PUSH(0x70E74u);
    HLE(65, 0x183F90u, D3DDevice_SetVertexShaderConstant, 0xb0, 0, (ST_ecx ST_edx ST_esp), (ST_ecx ST_edx ST_esp));
    /* 00070E74  push 900h */
    PUSH(0x900u);
    /* 00070E79  call 00182230h */
    PUSH(0x70E7Eu);
    HLE(66, 0x182230u, D3DDevice_SetRenderState_CullMode, 0xb0, 0, (ST_esp), (ST_esp));
    /* 00070E7E  mov eax,[esp+0CCh] */
    eax = SM32((esp+0xCCu));
    /* 00070E85  mov ecx,[esp+0BCh] */
    ecx = SM32((esp+0xBCu));
    /* 00070E8C  mov edx,[esp+0C8h] */
    edx = SM32((esp+0xC8u));
    /* 00070E93  mov esi,[esp+0C0h] */
    esi = SM32((esp+0xC0u));
    /* 00070E9A  push eax */
    PUSH(eax);
    /* 00070E9B  mov eax,[esp+0C8h] */
    eax = SM32((esp+0xC8u));
    /* 00070EA2  push ecx */
    PUSH(ecx);
    /* 00070EA3  xor ecx,ecx */
    { uint32_t a_ = ecx, b_ = ecx; uint32_t r_ = (uint32_t)(a_ ^ b_); ecx = r_; }
    /* 00070EA5  call 0007A960h */
    PUSH(0x70EAAu);
    CALL(67, f_0007A960, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_esi));
L_00070EAA:
    /* 00070EAA  mov al,ds:[2E352Ch] */
    SET8L(eax, K8(0x2E352Cu));
    /* 00070EAF  test al,al */
    { uint8_t r_ = R8L(eax) & R8L(eax); FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    /* 00070EB1  je short 00070EE8h */
    if (FZ) goto L_00070EE8;
L_00070EB3:
    /* 00070EB3  mov edx,[esp+0CCh] */
    edx = SM32((esp+0xCCu));
    /* 00070EBA  mov eax,[esp+0C8h] */
    eax = SM32((esp+0xC8u));
    /* 00070EC1  mov ecx,[esp+0C4h] */
    ecx = SM32((esp+0xC4u));
    /* 00070EC8  push edx */
    PUSH(edx);
    /* 00070EC9  mov edx,[esp+0C4h] */
    edx = SM32((esp+0xC4u));
    /* 00070ED0  push eax */
    PUSH(eax);
    /* 00070ED1  mov eax,[esp+0C4h] */
    eax = SM32((esp+0xC4u));
    /* 00070ED8  push ecx */
    PUSH(ecx);
    /* 00070ED9  mov ecx,[esp+0C4h] */
    ecx = SM32((esp+0xC4u));
    /* 00070EE0  push edx */
    PUSH(edx);
    /* 00070EE1  push eax */
    PUSH(eax);
    /* 00070EE2  push ebp */
    PUSH(ebp);
    /* 00070EE3  call 000736F0h */
    PUSH(0x70EE8u);
    CALL(68, f_000736F0, 0xb0, 0, 0, (ST_eax ST_ecx ST_edx ST_esp ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo));
    /* 00070EE8  pop edi */
    edi = POP();
    /* 00070EE9  pop esi */
    esi = POP();
    /* 00070EEA  pop ebp */
    ebp = POP();
    /* 00070EEB  pop ebx */
    ebx = POP();
    /* 00070EEC  add esp,0A0h */
    { uint32_t a_ = esp, b_ = 0xA0u; uint32_t r_ = (uint32_t)(a_ + b_); esp = r_; }
    /* 00070EF2  ret 1Ch */
    esp += 32u; EXIT(0, (ST_ebx ST_esp ST_ebp ST_esi ST_edi));
L_00070EE8:
    /* 00070EE8  pop edi */
    edi = POP();
    /* 00070EE9  pop esi */
    esi = POP();
    /* 00070EEA  pop ebp */
    ebp = POP();
    /* 00070EEB  pop ebx */
    ebx = POP();
    /* 00070EEC  add esp,0A0h */
    { uint32_t a_ = esp, b_ = 0xA0u; uint32_t r_ = (uint32_t)(a_ + b_); esp = r_; }
    /* 00070EF2  ret 1Ch */
    esp += 32u; EXIT(0, (ST_eax ST_ebx ST_esp ST_ebp ST_esi ST_edi ST_fk ST_fa ST_fb ST_fr ST_fw ST_fco ST_foo));
    return N70_EXIT;
}

/* ---- the entry, verify driver, report -------------------------------------------------------------------------- */
static __attribute__((noinline)) int n70_fast(xctx *restrict c, n70_env *restrict e, unsigned cfg) { return n70_core(c, e, cfg, 0, 0, N70_M_FAST); }
static __attribute__((noinline)) int n70_gen(xctx *restrict c, n70_env *restrict e, unsigned cfg, int resume) { return n70_core(c, e, cfg, resume, 0, N70_M_GEN); }
static __attribute__((noinline)) int n70_dry(xctx *restrict c, n70_env *restrict e, unsigned cfg, int resume, int outcome) { return n70_core(c, e, cfg, resume, outcome, N70_M_DRY); }

#ifdef __vita__
static inline uint64_t n70_ns(void) { return xk_os_monotonic_us() * 1000u; }
#else
#include <time.h>
static inline uint64_t n70_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
#endif

enum { NC_NATIVE, NC_GUEST, NC_DECLINED, NC_DECL_PROLOGUE, NC_DECL_LAYOUT, NC_DECL_JOB, NC_VERIFIED, NC_SEGMENTS, NC_MISMATCHED,
       NC_SKIPPED, NC_ON_HELPER, NC_BAILED, NC_GENERIC, NC_TIMED_NATIVE, NC_TIMED_GUEST, NC_NAN_WORDS, NC_COUNTERS };
static unsigned n70_counter[NC_COUNTERS], n70_mismatch_total;
static uint64_t n70_native_ns, n70_guest_ns;
#define NC_ADD(i, v) __atomic_fetch_add(&n70_counter[i], (unsigned)(v), __ATOMIC_RELAXED)

static int n70_mode_value = -1, n70_timing = -1;
static xv_rec_opt n70_ab_opt = XV_REC_OPT_INIT("XV_NATIVE_70110");   /* XV_REC_AB membership only (its mode is not used) */
static int n70_mode(void)
{
    int mode = __atomic_load_n(&n70_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_70110"); mode = e ? atoi(e) : XV_NATIVE_70110_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        { const char *t = getenv("XV_NATIVE_70110_TIME"); __atomic_store_n(&n70_timing, t && atoi(t) != 0, __ATOMIC_RELAXED); }
        int expected = -1;
        if (__atomic_compare_exchange_n(&n70_mode_value, &expected, mode, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            XK_LOG("[native-70110] f_00070110 material setup (callees and D3D HLE as the guest calls them): %s\n",
                   mode == 2 ? "native" : mode == 1 ? "verify (tapped guest copy, native predictions per site, guest result kept)" : "off");
        else mode = expected;
    }
    return mode;
}
unsigned xv_native_70110_mismatch_total(void) { return __atomic_load_n(&n70_mismatch_total, __ATOMIC_RELAXED); }
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_70110_force(int mode) { n70_mode(); __atomic_store_n(&n70_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); }

/* The site table: kind and key of every site n70_core numbers (the tapped guest copy reports kind and key). */
static const struct { uint8_t kind; uint32_t key; uint8_t hle_args, hle_eax; } N70_SITES[] = {
    { 0, 0, 0, 0 },
    { N70_K_HLE, 0x70319u, 1, 0 },   /* 1  SetRenderState_ZEnable */
    { N70_K_PREEMPT, 0x70321u, 0, 0 },   /* 2  jmp 7030D */
    { N70_K_HLE, 0x7032au, 1, 0 },   /* 3  SetRenderState_ZEnable */
    { N70_K_HLE, 0x7033fu, 0, 0 },   /* 4  SetRenderState_Simple */
    { N70_K_HLE, 0x70354u, 0, 0 },   /* 5  SetRenderState_Simple */
    { N70_K_HLE, 0x7036du, 1, 0 },   /* 6  SetRenderState_ZBias */
    { N70_K_HLE, 0x70377u, 1, 0 },   /* 7  SetRenderState_CullMode */
    { N70_K_HLE, 0x70386u, 0, 0 },   /* 8  SetRenderState_Simple */
    { N70_K_HLE, 0x7039fu, 0, 0 },   /* 9  SetRenderState_Simple */
    { N70_K_HLE, 0x703b4u, 0, 0 },   /* 10 SetRenderState_Simple */
    { N70_K_HLE, 0x703cdu, 0, 0 },   /* 11 SetRenderState_Simple */
    { N70_K_HLE, 0x703e6u, 0, 0 },   /* 12 SetRenderState_Simple */
    { N70_K_HLE, 0x7040fu, 0, 0 },   /* 13 SetRenderState_Simple */
    { N70_K_HLE, 0x70424u, 0, 0 },   /* 14 SetRenderState_Simple */
    { N70_K_CALL, 0x70440u, 0, 0 },   /* 15 f_00080360 */
    { N70_K_SAMPLER, 0x00000u, 0, 0 },   /* 16 sampler group 0 */
    { N70_K_HLE, 0x7044eu, 1, 0 },   /* 17 SetTextureState_Deferred */
    { N70_K_HLE, 0x7045cu, 1, 0 },   /* 18 SetTextureState_Deferred */
    { N70_K_HLE, 0x7046au, 1, 0 },   /* 19 SetTextureState_Deferred */
    { N70_K_HLE, 0x70478u, 1, 0 },   /* 20 SetTextureState_Deferred */
    { N70_K_HLE, 0x70486u, 1, 0 },   /* 21 SetTextureState_Deferred */
    { N70_K_CALL, 0x70498u, 0, 0 },   /* 22 f_00080360 */
    { N70_K_SAMPLER, 0x00001u, 0, 0 },   /* 23 sampler group 1 */
    { N70_K_HLE, 0x704a9u, 1, 0 },   /* 24 SetTextureState_Deferred */
    { N70_K_HLE, 0x704bau, 1, 0 },   /* 25 SetTextureState_Deferred */
    { N70_K_HLE, 0x704cbu, 1, 0 },   /* 26 SetTextureState_Deferred */
    { N70_K_HLE, 0x704dcu, 1, 0 },   /* 27 SetTextureState_Deferred */
    { N70_K_HLE, 0x704edu, 1, 0 },   /* 28 SetTextureState_Deferred */
    { N70_K_CALL, 0x704ffu, 0, 0 },   /* 29 f_00080360 */
    { N70_K_SAMPLER, 0x00002u, 0, 0 },   /* 30 sampler group 2 */
    { N70_K_HLE, 0x70510u, 1, 0 },   /* 31 SetTextureState_Deferred */
    { N70_K_HLE, 0x70521u, 1, 0 },   /* 32 SetTextureState_Deferred */
    { N70_K_HLE, 0x70532u, 1, 0 },   /* 33 SetTextureState_Deferred */
    { N70_K_HLE, 0x70543u, 1, 0 },   /* 34 SetTextureState_Deferred */
    { N70_K_HLE, 0x70554u, 1, 0 },   /* 35 SetTextureState_Deferred */
    { N70_K_CALL, 0x70566u, 0, 0 },   /* 36 f_00080360 */
    { N70_K_SAMPLER, 0x00003u, 0, 0 },   /* 37 sampler group 3 */
    { N70_K_HLE, 0x70577u, 1, 0 },   /* 38 SetTextureState_Deferred */
    { N70_K_HLE, 0x70588u, 1, 0 },   /* 39 SetTextureState_Deferred */
    { N70_K_HLE, 0x70599u, 1, 0 },   /* 40 SetTextureState_Deferred */
    { N70_K_HLE, 0x705aau, 1, 0 },   /* 41 SetTextureState_Deferred */
    { N70_K_HLE, 0x705bbu, 1, 0 },   /* 42 SetTextureState_Deferred */
    { N70_K_HLE, 0x705ccu, 1, 0 },   /* 43 SetTextureState_Deferred */
    { N70_K_CALL, 0x705f6u, 0, 0 },   /* 44 f_000B5130 */
    { N70_K_CALL, 0x7063cu, 0, 0 },   /* 45 f_00173F20 */
    { N70_K_CALL, 0x7064fu, 0, 0 },   /* 46 f_000111A0 */
    { N70_K_CALL, 0x707d5u, 0, 0 },   /* 47 f_000621E0 */
    { N70_K_CALL, 0x707e1u, 0, 0 },   /* 48 f_000658D0 */
    { N70_K_UV, 0x70960u, 0, 0 },   /* 49 56F20 variant 0 */
    { N70_K_HLE, 0x70975u, 3, 1 },   /* 50 SetVertexShaderConstant */
    { N70_K_HLE, 0x70986u, 3, 1 },   /* 51 SetVertexShaderConstant */
    { N70_K_HLE, 0x70a34u, 3, 1 },   /* 52 SetVertexShaderConstant */
    { N70_K_FOG_BEGIN, 0x70a42u, 0, 0 },   /* 53 fog begin */
    { N70_K_CALL, 0x70a51u, 0, 0 },   /* 54 f_000118D0 */
    { N70_K_CALL, 0x70ce7u, 0, 0 },   /* 55 f_00011B60 */
    { N70_K_CALL, 0x70cf3u, 0, 0 },   /* 56 f_00011BD0 */
    { N70_K_CALL, 0x70d02u, 0, 0 },   /* 57 f_00011BD0 */
    { N70_K_FOG_END, 0x70d07u, 0, 0 },   /* 58 fog end */
    { N70_K_CALL, 0x70d43u, 0, 0 },   /* 59 f_00011BD0 */
    { N70_K_CALL, 0x70d51u, 0, 0 },   /* 60 f_00011BD0 */
    { N70_K_CALL, 0x70d60u, 0, 0 },   /* 61 f_0006F340 */
    { N70_K_HLE, 0x70d6au, 1, 0 },   /* 62 SetRenderState_CullMode */
    { N70_K_CALL, 0x70d96u, 0, 0 },   /* 63 f_0007A960 */
    { N70_K_UV, 0x70e5cu, 0, 0 },   /* 64 56F20 variant 1 */
    { N70_K_HLE, 0x70e74u, 3, 1 },   /* 65 SetVertexShaderConstant */
    { N70_K_HLE, 0x70e7eu, 1, 0 },   /* 66 SetRenderState_CullMode */
    { N70_K_CALL, 0x70eaau, 0, 0 },   /* 67 f_0007A960 */
    { N70_K_CALL, 0x70ee8u, 0, 0 },   /* 68 f_000736F0 */
};
enum { N70_NSITES = (int)(sizeof N70_SITES / sizeof N70_SITES[0]) };
static int n70_site_of(unsigned kind, uint32_t key)
{
    for (int k = 1; k < N70_NSITES; ++k) if (N70_SITES[k].kind == kind && N70_SITES[k].key == key) return k;
    return -1;
}

/* ---- verify ------------------------------------------------------------------------------------------------------
 * The tapped guest copy runs with the real context; at every site it calls xv_native_70110_tap before (post 0) and
 * after (post 1) the call. After a site (and at the entry) the native runs dry from the actual state to its next
 * site; at that site's pre-tap the prediction is compared with the actual state. */
enum { N70_IMG_LO = 0x18F460u, N70_IMG_BYTES = 0x50u };   /* the render-state shadow words the body writes */
typedef struct n70_vs {
    xctx *c; unsigned cfg; uint32_t E;
    int site, broken, odd, ovf;
    unsigned nw, segments, bad, skipped, nan_words;
    xctx pc, pre;
    uint8_t shadow[N70_WSIZE], img[N70_IMG_BYTES];
    n70_ow ow[N70_OW_MAX];
} n70_vs;

static void n70_mismatch(n70_vs *vs, const char *what, uint32_t native, uint32_t guest)
{
    vs->bad++;
    if (__atomic_add_fetch(&n70_mismatch_total, 1, __ATOMIC_RELAXED) <= 16)
        XK_LOG("[native-70110] MISMATCH %s native %08X guest %08X (site %d, E %08X, material %08X)\n", what, native, guest,
               vs->site, vs->E, vs->c->r[4] >= 0x1000u ? X_M32(vs->E + 4u) : 0u);
}
static void n70_read_window(const n70_env *e, uint8_t *dst)
{
    for (uint32_t o = 0; o < N70_WSIZE; ++o) dst[o] = *(o < e->wcut ? e->w0 + o : e->w1 + (o - e->wcut));
}
static void n70_predict(n70_vs *vs, int resume, int outcome)
{
    vs->pc = *vs->c;
    n70_env e; memset(&e, 0, sizeof e);
    e.ram = g_xram; e.pt = N70_XPT; e.imgb = N70_IMGB; e.E = vs->E; e.wlo = vs->E - N70_WLO; n70_map(&e);
    n70_read_window(&e, vs->shadow);
    memcpy(vs->img, e.ki_18f + (N70_IMG_LO & 0xFFFu), N70_IMG_BYTES);
    e.shadow = vs->shadow; e.ow = vs->ow;
    vs->site = n70_dry(&vs->pc, &e, vs->cfg, resume, outcome);
    vs->nw = e.nw; vs->odd = e.odd; vs->ovf = e.ovf;
}
static int n70_nan_pair(uint64_t a, uint64_t b)
{
    const uint64_t m = 0x7FF0000000000000ull;
    return (a & m) == m && (a << 12) && (b & m) == m && (b << 12);
}
/* float words that are both NaN: which payload an operation propagates is the host compiler's operand order (not
 * reproduced); such a word can reach a register only through a float stored and reloaded as an integer (7086C) */
static int n70_nan32_pair(uint32_t a, uint32_t b)
{ return (a & 0x7F800000u) == 0x7F800000u && (a & 0x7FFFFFu) && (b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu); }
static void n70_compare(n70_vs *vs)
{
    const xctx *a = vs->c, *p = &vs->pc;
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (int i = 0; i < 8; ++i) if (a->r[i] != p->r[i]) { if (n70_nan32_pair(a->r[i], p->r[i])) vs->nan_words++; else n70_mismatch(vs, rn[i], p->r[i], a->r[i]); }
#define N70_CF(f) do { if (a->f != p->f) n70_mismatch(vs, #f, (uint32_t)p->f, (uint32_t)a->f); } while (0)
    N70_CF(f_kind); N70_CF(f_op1); N70_CF(f_op2); N70_CF(f_res); N70_CF(f_bits); N70_CF(f_cf_override); N70_CF(f_cf);
    N70_CF(f_of_override); N70_CF(f_of); N70_CF(fsp); N70_CF(fsw); N70_CF(fcw); N70_CF(preempt); N70_CF(fs_base); N70_CF(df);
    N70_CF(scratch); N70_CF(eip_hint);
#undef N70_CF
    if (a->fiber != p->fiber) n70_mismatch(vs, "fiber", 0, 1);
    for (int i = 0; i < 8; ++i) {
        uint64_t x, y; memcpy(&x, &p->st[i], 8); memcpy(&y, &a->st[i], 8);
        if (x == y) continue;
        if (n70_nan_pair(x, y)) { vs->nan_words++; continue; }
        char w[24]; snprintf(w, sizeof w, "st[%d] (lo word)", i); n70_mismatch(vs, w, (uint32_t)x, (uint32_t)y);
    }
    if (memcmp(a->mm, p->mm, sizeof a->mm)) n70_mismatch(vs, "mm", 0, 1);
    if (memcmp(a->xmm, p->xmm, sizeof a->xmm)) n70_mismatch(vs, "xmm", 0, 1);
    /* memory: the window, the render-state shadow words, every other native write */
    n70_env e; memset(&e, 0, sizeof e); e.ram = g_xram; e.pt = N70_XPT; e.imgb = N70_IMGB; e.wlo = vs->E - N70_WLO; n70_map(&e);
    uint8_t win[N70_WSIZE]; n70_read_window(&e, win);
    for (uint32_t o = 0; o < N70_WSIZE; o += 4) {
        uint32_t x, y; memcpy(&x, vs->shadow + o, 4); memcpy(&y, win + o, 4);
        if (x != y) { if (n70_nan32_pair(x, y)) { vs->nan_words++; continue; } char w[32]; snprintf(w, sizeof w, "stack E%+d", (int)o - (int)N70_WLO); n70_mismatch(vs, w, x, y); }
    }
    uint8_t img[N70_IMG_BYTES]; memcpy(img, vs->img, sizeof img);
    for (unsigned j = 0; j < vs->nw; ++j) {
        const n70_ow *w = &vs->ow[j];
        for (unsigned i = 0; i < w->size; ++i) {
            const uint32_t b = w->addr + i;
            if (b - N70_IMG_LO < N70_IMG_BYTES) img[b - N70_IMG_LO] = w->bytes[i];
            else if (*N70_H(&e, b) != w->bytes[i]) { char t[32]; snprintf(t, sizeof t, "write@%08X", b); n70_mismatch(vs, t, w->bytes[i], *N70_H(&e, b)); }
        }
    }
    const uint8_t *cur = e.ki_18f + (N70_IMG_LO & 0xFFFu);
    for (uint32_t o = 0; o < N70_IMG_BYTES; o += 4) {
        uint32_t x, y; memcpy(&x, img + o, 4); memcpy(&y, cur + o, 4);
        if (x != y) { char t[32]; snprintf(t, sizeof t, "shadow@%08X", N70_IMG_LO + o); n70_mismatch(vs, t, x, y); }
    }
}
void xv_native_70110_tap(xctx *c, void *vsp, unsigned kind, uint32_t key, int post, int outcome)
{
    n70_vs *vs = vsp;
    const int k = n70_site_of(kind, key);
    if (!post) {
        vs->pre = *c;
        if (vs->broken) return;
        vs->segments++;
        if (vs->site != k) { n70_mismatch(vs, "site", (uint32_t)vs->site, (uint32_t)k); vs->broken = 1; return; }
        if (vs->odd || vs->ovf) vs->skipped++; else n70_compare(vs);
        return;
    }
    if (k > 0 && N70_SITES[k].kind == N70_K_HLE) {         /* the HLE call changed only esp (and eax: 0 for VSC) */
        xctx x = vs->pre; x.r[4] += 4u + 4u * N70_SITES[k].hle_args; if (N70_SITES[k].hle_eax) x.r[0] = 0;
        if (memcmp(&x, c, sizeof x)) n70_mismatch(vs, "HLE effect", 0, (uint32_t)k);
    } else if (k > 0 && N70_SITES[k].kind == N70_K_PREEMPT) {
        /* X_PREEMPT: the budget, or a refill by xv_preempt, whose yield (on the owner) also sets eip_hint to [esp] */
        xctx x = vs->pre; x.preempt = c->preempt; x.eip_hint = c->eip_hint;
        const int fired = vs->pre.preempt - 1 <= 0;
        if (memcmp(&x, c, sizeof x) || !(c->preempt == vs->pre.preempt - 1 || (fired && c->preempt > 0)) ||
            (c->eip_hint != vs->pre.eip_hint && !(fired && c->eip_hint == X_M32(c->r[4]))))
            n70_mismatch(vs, "back-edge budget", (uint32_t)vs->pre.preempt, (uint32_t)c->preempt);
    }
    if (k < 0) { n70_mismatch(vs, "unknown site", kind, key); vs->broken = 1; return; }
    vs->broken = 0;
    n70_predict(vs, k, outcome);
}
/* the tapped copy's XV_NATIVE_MATERIAL_SAMPLER group: xv_material_sampler_try between the taps */
int xv_native_70110_sampler(xctx *c, void *vs, unsigned group)
{
    xv_native_70110_tap(c, vs, N70_K_SAMPLER, group, 0, 0);
    const int h = xv_material_sampler_try(c, group);
    xv_native_70110_tap(c, vs, N70_K_SAMPLER, group, 1, h);
    return h;
}
static int n70_verify(xctx *c, unsigned cfg)
{
    n70_vs *vs = malloc(sizeof *vs);        /* ~3 KB: off the (fiber) stack */
    if (!vs) return 0;
    memset(vs, 0, sizeof *vs); vs->c = c; vs->cfg = cfg; vs->E = c->r[4];
    n70_predict(vs, 0, 0);
    if (vs->site == N70_DECLINE) { free(vs); NC_ADD(NC_DECLINED, 1); NC_ADD(NC_DECL_PROLOGUE, 1); return 0; }
    f_00070110_vbody(c, vs);
    if (!vs->broken) {
        vs->segments++;
        if (vs->site != N70_EXIT) n70_mismatch(vs, "exit", (uint32_t)vs->site, N70_EXIT);
        else if (vs->odd || vs->ovf) vs->skipped++;
        else n70_compare(vs);
    }
    NC_ADD(NC_VERIFIED, 1); NC_ADD(NC_SEGMENTS, vs->segments); NC_ADD(NC_SKIPPED, vs->skipped); NC_ADD(NC_NAN_WORDS, vs->nan_words);
    if (vs->bad) NC_ADD(NC_MISMATCHED, 1);
    free(vs);
    return 1;
}

#ifndef __vita__
/* Host harness only: XV_NATIVE_70110_CAPTURE=<file>[:n[:skip]] writes the page table once, then n entry states (after
 * skipping `skip` calls): the context and every guest block the body reads - the frame window, the material ([E+4],
 * 0x180), the render context ([2E3520], 0xD0), its light table ([S+84], 60), its [S+A8] record and [S+B0] array, the
 * vectors at [2E3508] and [232F6C], the word at [E+18h], the globals page 0x2E3000, the camera page 0x2FC000 and the
 * constants 0x1F0A00.. - for tools/tests/native_70110.c --replay (with its callee and HLE stand-ins). Captures are game
 * memory: private, not for the repository. One call at a time (a host diagnostic; not built for the Vita). */
static void n70_cap_block(FILE *f, const n70_env *e, uint32_t a, uint32_t len)
{
    if (!a || len > 0x2000u) { const uint32_t z[2] = { 0, 0 }; fwrite(z, 4, 2, f); return; }   /* an absent block */
    uint8_t buf[0x2000]; for (uint32_t i = 0; i < len; ++i) buf[i] = *N70_H(e, a + i);
    fwrite(&a, 4, 1, f); fwrite(&len, 4, 1, f); fwrite(buf, 1, len, f);
}
static void n70_capture(xctx *c)
{
    static int state = -1; static FILE *f; static unsigned left, skip, busy;
    if (state == 0) return;
    if (__atomic_exchange_n(&busy, 1u, __ATOMIC_ACQUIRE)) return;
    if (state < 0) {
        const char *e = getenv("XV_NATIVE_70110_CAPTURE"); state = 0;
        if (e) {
            char path[512]; snprintf(path, sizeof path, "%s", e); char *p = strchr(path, ':'); left = 2000;
            if (p) { *p++ = 0; left = (unsigned)atoi(p); char *q = strchr(p, ':'); if (q) skip = (unsigned)atoi(q + 1); }
            f = fopen(path, "wb");
            if (f) { const uint32_t hdr[3] = { 0x4337304Eu, 1u << 20, (uint32_t)sizeof(xctx) }; fwrite(hdr, sizeof hdr, 1, f); fwrite(N70_XPT, 4, 1u << 20, f); state = 1; }
        }
    }
    if (state == 1 && skip) skip--;
    else if (state == 1 && left) {
        n70_env e; memset(&e, 0, sizeof e); e.ram = g_xram; e.pt = N70_XPT;
        const uint32_t E = c->r[4], M = *(uint32_t *)N70_H(&e, E + 4u), S = *(uint32_t *)N70_H(&e, 0x2E3520u);
        uint32_t nb = 13; fwrite(c, sizeof *c, 1, f); fwrite(&nb, 4, 1, f);
        n70_cap_block(f, &e, E - N70_WLO, N70_WSIZE); n70_cap_block(f, &e, M, 0x180); n70_cap_block(f, &e, S, 0xD0);
        n70_cap_block(f, &e, S ? *(uint32_t *)N70_H(&e, S + 0x84u) : 0, 60);
        n70_cap_block(f, &e, S ? *(uint32_t *)N70_H(&e, S + 0xA8u) : 0, 0x30);
        n70_cap_block(f, &e, S ? *(uint32_t *)N70_H(&e, S + 0xB0u) : 0, 16);
        n70_cap_block(f, &e, *(uint32_t *)N70_H(&e, 0x2E3508u), 16); n70_cap_block(f, &e, *(uint32_t *)N70_H(&e, 0x232F6Cu), 12);
        n70_cap_block(f, &e, *(uint32_t *)N70_H(&e, E + 0x18u), 2);
        n70_cap_block(f, &e, 0x2E3000u, 0x1000); n70_cap_block(f, &e, 0x2FC000u, 0x1000); n70_cap_block(f, &e, 0x1F0A00u, 0x100);
        n70_cap_block(f, &e, 0x18F000u, 0x1000);
        if (!--left) { fclose(f); f = NULL; state = 0; XK_LOG("[native-70110] capture written\n"); }
    }
    __atomic_store_n(&busy, 0u, __ATOMIC_RELEASE);
}
#endif

/* The hook (tools/patch_native_70110_hooks.py): f_00070110 is `if (!xv_native_70110(c, cfg)) f_00070110_body(c);`.
 * Returns 0 - the translation runs - when off (without timing) and when declined; else the call is done. */
int xv_native_70110(xctx *c, unsigned cfg)
{
    int mode = n70_mode();
    if (xv_rec_ab_active() && xv_rec_opt_ab(&n70_ab_opt)) mode = xv_rec_ab_phase ? 2 : 0;
#ifndef __vita__
    { static int cap = -1; if (cap < 0) cap = getenv("XV_NATIVE_70110_CAPTURE") != NULL; if (cap) n70_capture(c); }
#endif
    const int timed = __atomic_load_n(&n70_timing, __ATOMIC_RELAXED);
    if (mode == 0) {
        if (!timed) return 0;
        const uint64_t t0 = n70_ns(); f_00070110_body(c);
        __atomic_fetch_add(&n70_guest_ns, n70_ns() - t0, __ATOMIC_RELAXED); NC_ADD(NC_TIMED_GUEST, 1); NC_ADD(NC_GUEST, 1);
        return 1;
    }
    if (mode == 1 || timed) { extern int xv_scene_thread_on_helper(void) __attribute__((weak)); if (xv_scene_thread_on_helper && xv_scene_thread_on_helper()) NC_ADD(NC_ON_HELPER, 1); }
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if (xv_is_object_job(c)) { NC_ADD(NC_DECLINED, 1); NC_ADD(NC_DECL_JOB, 1); return 0; }
#endif
    const uint32_t E = c->r[4];
    /* esp 4-aligned, the window inside the address space and off the image pages the body addresses by constant */
    if ((E & 3u) || E < N70_WLO || E > 0xFFFFFFFFu - N70_WHI || (E - N70_WLO < 0x333000u && E + N70_WHI > 0x18F000u)) {
        NC_ADD(NC_DECLINED, 1); NC_ADD(NC_DECL_LAYOUT, 1); return 0;
    }
    if (mode == 1) return n70_verify(c, cfg);
    const uint64_t t0 = timed ? n70_ns() : 0;
    n70_env e; e.ram = g_xram; e.pt = N70_XPT; e.imgb = N70_IMGB; e.E = E; e.wlo = E - N70_WLO; e.utok = e.ftok = 0; e.scope.owner = NULL; n70_map(&e);
    int r = e.wcut == N70_WSIZE ? n70_fast(c, &e, cfg) : n70_gen(c, &e, cfg, 0);
    if (r >= N70_BAIL) { NC_ADD(NC_BAILED, 1); r = n70_gen(c, &e, cfg, r - N70_BAIL); }
    xv_phase_cleanup(&e.scope);
    if (r == N70_DECLINE) { NC_ADD(NC_DECLINED, 1); NC_ADD(NC_DECL_PROLOGUE, 1); return 0; }
    NC_ADD(NC_NATIVE, 1); if (e.wcut != N70_WSIZE) NC_ADD(NC_GENERIC, 1);
    if (timed) { __atomic_fetch_add(&n70_native_ns, n70_ns() - t0, __ATOMIC_RELAXED); NC_ADD(NC_TIMED_NATIVE, 1); }
    return 1;
}

/* ---- the 60-frame report (recomp/kernel/xd3d.c's weak report chain) ----------------------------------------- */
void xv_native_70110_report(unsigned frames)
{
    if (__atomic_load_n(&n70_mode_value, __ATOMIC_RELAXED) < 0) return;   /* never called yet */
    unsigned v[NC_COUNTERS];
    for (unsigned i = 0; i < NC_COUNTERS; ++i) v[i] = __atomic_exchange_n(&n70_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t nn = __atomic_exchange_n(&n70_native_ns, 0u, __ATOMIC_RELAXED), gn = __atomic_exchange_n(&n70_guest_ns, 0u, __ATOMIC_RELAXED);
    if (!v[NC_NATIVE] && !v[NC_GUEST] && !v[NC_VERIFIED] && !v[NC_DECLINED]) return;
    char t[120] = "";
    if (v[NC_TIMED_NATIVE] || v[NC_TIMED_GUEST])
        snprintf(t, sizeof t, "; us/call native %.3f (%u) guest %.3f (%u)", v[NC_TIMED_NATIVE] ? nn / 1000.0 / v[NC_TIMED_NATIVE] : 0.0,
                 v[NC_TIMED_NATIVE], v[NC_TIMED_GUEST] ? gn / 1000.0 / v[NC_TIMED_GUEST] : 0.0, v[NC_TIMED_GUEST]);
    XK_LOG("[native-70110] %u frames: native %u (frame across a page end %u, GEN restarts %u) guest %u declined %u (prologue %u layout %u "
           "object-job %u); verified %u segments %u mismatched %u skipped %u nan-words %u (total mismatches %u); on the scene helper %u (counted only in verifier/timed mode)%s\n",
           frames, v[NC_NATIVE], v[NC_GENERIC], v[NC_BAILED], v[NC_GUEST], v[NC_DECLINED], v[NC_DECL_PROLOGUE], v[NC_DECL_LAYOUT], v[NC_DECL_JOB], v[NC_VERIFIED],
           v[NC_SEGMENTS], v[NC_MISMATCHED], v[NC_SKIPPED], v[NC_NAN_WORDS], __atomic_load_n(&n70_mismatch_total, __ATOMIC_RELAXED),
           v[NC_ON_HELPER], t);
}
