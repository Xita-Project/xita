/* xk_native_4b9d0.c - native Halo CE (Xbox 3925) collision work under f_0004B9D0 (biped physics update).
 *
 * f_0004B9D0 costs ~9 ms/frame on the Vita's a10 (perf187, 23 calls/frame). Phase timers inside it (host x86 and
 * Pi 4, a10 checkpoint with movement) put 85-90 % of it in f_00049600 -> f_00172BF0, the character's collision
 * move: the feature collection f_00171F10 (BSP sphere query 88110, BSP feature build 868F0, object walk) and the
 * solver f_00170C10 (its feature test 864C0). The biggest single subtree is the BSP sphere query:
 *
 *   f_00088110  query set-up on its own frame (the query record q), result lists cleared, then 87EA0
 *   f_00087EA0  BSP3D traversal (recursive): sphere vs node planes; leaves recorded (q->results +C0C, <= 256); per
 *               leaf its BSP2D references, the planes already on the ancestor stack (q+0x18/+0x1C) projected
 *   f_00087E10  BSP2D traversal (recursive) of a reference, circle vs 2D node lines
 *   f_00086F50  surface test: already-tested bit, vertex ring (SSE distance, vertex list +808, <= 256), edges
 *               (segment/sphere B0CB0, edge list +404), point in polygon on the projected axes (surface list +0)
 *   f_000B0CB0  segment vs sphere
 *
 * No other calls, no HLE. The hook replaces the fused copy of this subtree (recomp/query_fusion.c
 * query_fused_172c95_171f94, the 171F94 call of the collection under 172C95) where recomp/kernel/xk_query_reuse.c
 * runs it; the query reuse / world-run admission around it is unchanged.
 *
 * Exact by construction (a transliteration of the generated code, not a re-derivation):
 *  - every guest memory read and write happens in the guest's order at the guest's address through the same
 *    translation (integer, SSE and push/pop accesses single-translation like X_M32/X_MF32/X_PUSH32, x87 float loads
 *    and stores page-split like x87_load_f32/x87_store_f32); nothing the guest reads from memory is taken from a
 *    local, so any aliasing (a deep ancestor stack running over the frame, results inside the stack) reads the bytes
 *    the guest reads;
 *  - registers: every guest register is a local assigned where the generated code assigns it (partial writes
 *    included: fnstsw ax, setcc, byte xors); callee-saved registers come back from the guest's pops (memory);
 *  - lazy flags: the whole record (kind, operands, result, width, both override cells and both stale cf/of cells),
 *    written by exactly the instructions that write it in the generated code (the emitter drops dead flags: `neg`,
 *    most `xor r,r`, `and r,imm`, `add` here) - so `sbb ecx,ecx` after `neg` reads the carry of the previous record
 *    like the translation does, and inc/dec keep their "carry preserved" override;
 *  - x87: the slots below the entry TOP are depth-indexed locals updated like st[] (pushes, fxch, fstp st(1), dead
 *    values kept); doubles with the guest's operand order, float loads widened, float stores rounded; the status
 *    word exactly as x87_compare leaves it (condition codes of the last compare, TOP of every compare OR-ed in);
 *  - SSE: xmm0/xmm1/xmm2[0] of the vertex pass in float arithmetic in the translation's order;
 *  - the back-edge budget: c->preempt drops by exactly the guest's back-edge count and xv_preempt() is called the
 *    same number of times, after the query instead of mid-loop (a scheduling point only; xk_native_visibility.c).
 * Declines (runs the fused guest code): the scene helper, a thread whose page table is not the live table (the fused
 * code mixes both), and while a watch/trace is on.
 *
 * XV_NATIVE_4B9D0 build flag (hook: tools/patch_native_4b9d0_hooks.py). Env XV_NATIVE_4B9D0: 0 off (default
 * XV_NATIVE_4B9D0_DEFAULT), 1 verify (native with a write journal, undo, run the fused guest query on the same
 * state, compare registers/flags/x87/SSE/budget, every byte the native wrote and the regions the guest may write,
 * keep the guest result), 2 native. XV_NATIVE_4B9D0_TIME=1: ns/call (Vita: us clock) of the guest (mode 0), the
 * native (2), both (1). [native-4b9d0] line every 60 frames. */
#include "xk.h"
/* xv_x86rt.h comes through xk.h (one path: the unit is also compiled from a copy by tools/test_native_4b9d0.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_NATIVE_4B9D0_DEFAULT
#define XV_NATIVE_4B9D0_DEFAULT 0
#endif

enum { N4_ZERO = 0x1F0A68u, N4_AXES = 0x1EAF30u };

/* ---- guest memory, as the generated code addresses it ----------------------------------------------------- */
typedef struct { uint8_t *ram; const uint32_t *pt; int jon; } n4_mem;   /* jon: journal the writes (verify) */
#define N4_P(a) (m->ram + m->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))

/* Verify mode journals every native write (address, size, old bytes) so the pre-state can be restored. */
typedef struct { uint32_t addr; uint8_t size, pad[3]; uint8_t old[4], now[4]; uint32_t word; } n4_jent;
typedef struct { n4_jent *e; unsigned n, cap; int overflow; } n4_journal;
static __thread n4_journal n4_j;
static void n4_jlog_slow(uint32_t a, const void *host, unsigned size)
{
    if (n4_j.n == n4_j.cap) {
        unsigned cap = n4_j.cap ? n4_j.cap * 2u : 4096u;
        n4_jent *e = realloc(n4_j.e, cap * sizeof *e);
        if (!e) { n4_j.overflow = 1; return; }
        n4_j.e = e; n4_j.cap = cap;
    }
    n4_jent *j = &n4_j.e[n4_j.n++]; j->addr = a; j->size = (uint8_t)size; memcpy(j->old, host, size);
}
#define n4_jlog(a, host, size) do { if (__builtin_expect(m->jon, 0)) n4_jlog_slow((a), (host), (size)); } while (0)

static inline __attribute__((always_inline)) uint32_t n4_r32(const n4_mem *m, uint32_t a) { uint32_t v; memcpy(&v, N4_P(a), 4); return v; }
static inline __attribute__((always_inline)) uint16_t n4_r16(const n4_mem *m, uint32_t a) { uint16_t v; memcpy(&v, N4_P(a), 2); return v; }
static inline __attribute__((always_inline)) uint8_t n4_r8(const n4_mem *m, uint32_t a) { return *N4_P(a); }
static inline __attribute__((always_inline)) void n4_w32(const n4_mem *m, uint32_t a, uint32_t v) { uint8_t *p = N4_P(a); n4_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void n4_w16(const n4_mem *m, uint32_t a, uint16_t v) { uint8_t *p = N4_P(a); n4_jlog(a, p, 2); memcpy(p, &v, 2); }
static inline __attribute__((always_inline)) void n4_w8(const n4_mem *m, uint32_t a, uint8_t v) { uint8_t *p = N4_P(a); n4_jlog(a, p, 1); *p = v; }
/* x87_load_f32 / x87_store_f32: page-split when the float crosses a page end */
static inline __attribute__((always_inline)) double n4_rf(const n4_mem *m, uint32_t a)
{
    float v;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(&v, N4_P(a), 4); else x_guest_read_pages(&v, a, 4);
    return (double)v;
}
static void n4_wf_split(const n4_mem *m, uint32_t a, float v)
{
    if (__builtin_expect(m->jon, 0))
        for (unsigned i = 0; i < 4; ++i) { uint32_t b = a + i; n4_jlog(b, N4_P(b), 1); }
    x_guest_write_pages(a, &v, 4);
}
static inline __attribute__((always_inline)) void n4_wf(const n4_mem *m, uint32_t a, double d)
{
    float v = (float)d;
    if ((a & 0xFFFu) <= 0xFFCu) { uint8_t *p = N4_P(a); n4_jlog(a, p, 4); memcpy(p, &v, 4); }
    else n4_wf_split(m, a, v);
}
/* X_MF32 (SSE movss/movhps): one translation */
static inline __attribute__((always_inline)) float n4_rmf(const n4_mem *m, uint32_t a) { float v; memcpy(&v, N4_P(a), 4); return v; }
static inline __attribute__((always_inline)) void n4_wmf(const n4_mem *m, uint32_t a, float v) { uint8_t *p = N4_P(a); n4_jlog(a, p, 4); memcpy(p, &v, 4); }

/* ---- per-query state ------------------------------------------------------------------------------------- */
/* A function's frame: the host pointers of its (at most two) pages, translated once. esp is 4-aligned (checked at
 * entry), so a slot never crosses a page end and one translation is exact for integer, x87 and SSE stores alike. */
typedef struct { uint32_t p0; uint8_t *h0, *h1; } n4_stk;
static inline __attribute__((always_inline)) void n4_stk_at(const n4_mem *m, n4_stk *k, uint32_t lo)
{
    k->p0 = lo >> 12; k->h0 = m->ram + m->pt[k->p0]; k->h1 = m->ram + m->pt[(k->p0 + 1u) & 0xFFFFFu];
}
#define N4_SP(k, a) ((((uint32_t)(a) >> 12) == (k)->p0 ? (k)->h0 : (k)->h1) + ((uint32_t)(a) & 0xFFFu))
static inline __attribute__((always_inline)) void n4_sw32(const n4_mem *m, const n4_stk *k, uint32_t a, uint32_t v) { uint8_t *p = N4_SP(k, a); n4_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void n4_sw16(const n4_mem *m, const n4_stk *k, uint32_t a, uint16_t v) { uint8_t *p = N4_SP(k, a); n4_jlog(a, p, 2); memcpy(p, &v, 2); }
static inline __attribute__((always_inline)) void n4_sw8(const n4_mem *m, const n4_stk *k, uint32_t a, uint8_t v) { uint8_t *p = N4_SP(k, a); n4_jlog(a, p, 1); *p = v; }
static inline __attribute__((always_inline)) void n4_swmf(const n4_mem *m, const n4_stk *k, uint32_t a, float v) { uint8_t *p = N4_SP(k, a); n4_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) float n4_swf(const n4_mem *m, const n4_stk *k, uint32_t a, double d) { float v = (float)d; n4_swmf(m, k, a, v); return v; }
static inline __attribute__((always_inline)) uint32_t n4_ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) uint16_t n4_ld16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline __attribute__((always_inline)) float n4_ldf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) float n4_u2f(uint32_t u) { float v; memcpy(&v, &u, 4); return v; }
static inline __attribute__((always_inline)) uint32_t n4_f2u(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
/* A guest record read through one translation when [a, a+len) lies in one page (host(a) + off is then exactly the
 * translation of a + off, for integer, x87 and SSE loads alike); otherwise NULL and every field is translated. */
static inline __attribute__((always_inline)) const uint8_t *n4_span(const n4_mem *m, uint32_t a, uint32_t len)
{ return (a & 0xFFFu) + len <= 0x1000u ? N4_P(a) : NULL; }
/* field reads: h = n4_span(base, len); off may be dynamic (outside [0, len-size] it is translated on its own) */
#define H32(h, base, off, len) ((h) && (uint32_t)(off) <= (len) - 4u ? n4_ld32((h) + (uint32_t)(off)) : n4_r32(m, (base) + (uint32_t)(off)))
#define H16(h, base, off, len) ((h) && (uint32_t)(off) <= (len) - 2u ? n4_ld16((h) + (uint32_t)(off)) : n4_r16(m, (base) + (uint32_t)(off)))
#define H8(h, base, off, len) ((h) && (uint32_t)(off) < (len) ? (h)[(uint32_t)(off)] : n4_r8(m, (base) + (uint32_t)(off)))
#define HF(h, base, off, len) ((h) && (uint32_t)(off) <= (len) - 4u ? (double)n4_ldf((h) + (uint32_t)(off)) : n4_rf(m, (base) + (uint32_t)(off)))
#define HMF(h, base, off, len) ((h) && (uint32_t)(off) <= (len) - 4u ? n4_ldf((h) + (uint32_t)(off)) : n4_rmf(m, (base) + (uint32_t)(off)))

typedef struct {
    n4_mem m;
    uint32_t fk, fa, fb, fr, fbits, fcfo, fcf, fofo, fof;   /* the lazy-flag record, as xctx holds it */
    uint32_t fsp0;
    uint16_t fsw;
    double sl[8];              /* sl[d]: x87 slot st[(fsp0 - d) & 7] (d = 1..5 written by the subtree) */
    float xmm0[4], xmm1[4], xmm2_0;
    int xmm;                   /* the vertex pass ran: xmm0, xmm1 and xmm2[0] are the subtree's */
    uint32_t be;               /* back-edges taken (X_PREEMPT sites) */
    int dirty;                 /* a write may have hit a cached value: every later such read comes from guest memory */
    /* values the query itself stored (88110's frame q = E88 - 0x228) and the BSP header, valid while !dirty */
    uint32_t E88, q, bsp, bits, ctr, res, low;
    uint16_t cap;
    float radius;
    uint32_t anc;              /* q+0x18: the ancestor-plane count */
    uint16_t axis; uint8_t side; uint32_t pu, pv;   /* q+0x21C/21E/220/224 as 87EA0 last stored them */
    uint32_t nodes3, planes, leaves, refs, nodes2, surfs, edges, verts;   /* BSP +4/+10/+1C/+28/+34/+40/+4C/+58 */
    double zero;               /* the float at 0x1F0A68 */
    int16_t axes[12];          /* 0x1EAF30: (u, v) per (axis, side) */
    n4_stk kq;                 /* 88110's frame [E88-0x230, E88+12) */
    uint8_t *hq;               /* the host address of E88 when that frame lies in one page, else NULL */
    const uint8_t *hctr;       /* the center's host pointer when its 12 bytes lie in one page */
    uint8_t *rb[3]; uint32_t rp0;   /* the result lists' pages: [res, res+0x1010) */
    unsigned nodes3n, nodes2n, leavesn, surfacesn, edgesn, vertsn, scans, scanned;
} n4q;
typedef struct { uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; } n4r;

/* the result lists (4-aligned, [res, res + 0x1010), not overlapping any frame: checked at entry) */
static inline __attribute__((always_inline)) uint8_t *n4_rp(const n4q *s, uint32_t a) { return s->rb[(a >> 12) - s->rp0] + (a & 0xFFFu); }
static inline __attribute__((always_inline)) int n4_inres(const n4q *s, uint32_t a) { return a - s->res <= 0x1010u - 4u; }

/* The lazy-flag record in locals (one set per function; saved around calls, which read and write it). */
#define N4_FL_LOCALS uint32_t fk = s->fk, fa = s->fa, fb = s->fb, fr = s->fr, fbits = s->fbits, fcfo = s->fcfo, fcf = s->fcf, fofo = s->fofo, fof = s->fof
#define N4_FL_SAVE() (s->fk = fk, s->fa = fa, s->fb = fb, s->fr = fr, s->fbits = fbits, s->fcfo = fcfo, s->fcf = fcf, s->fofo = fofo, s->fof = fof)
#define N4_FL_LOAD() (fk = s->fk, fa = s->fa, fb = s->fb, fr = s->fr, fbits = s->fbits, fcfo = s->fcfo, fcf = s->fcf, fofo = s->fofo, fof = s->fof)
#define N4_FL_LOAD() (fk = s->fk, fa = s->fa, fb = s->fb, fr = s->fr, fbits = s->fbits, fcfo = s->fcfo, fcf = s->fcf, fofo = s->fofo, fof = s->fof)
/* X_FLAGS / X_FLAGS_C */
#define SETF(k_, a_, b_, r_, bits_) (fk = (k_), fa = (uint32_t)(a_), fb = (uint32_t)(b_), fr = (uint32_t)(r_), fbits = (bits_), fcfo = 0, fofo = 0)
#define SETFC(k_, a_, b_, r_, bits_, cf_) (SETF(k_, a_, b_, r_, bits_), fcf = (cf_))
static inline __attribute__((always_inline)) uint32_t n4_fmask(uint32_t bits) { return bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u); }
static inline __attribute__((always_inline)) uint32_t n4_zf(uint32_t k, uint32_t r, uint32_t bits)
{ return k == XK_EXPLICIT ? (r >> 6) & 1u : (r & n4_fmask(bits)) == 0; }
static inline __attribute__((always_inline)) uint32_t n4_sf(uint32_t k, uint32_t r, uint32_t bits)
{ return k == XK_EXPLICIT ? (r >> 7) & 1u : (r >> (bits - 1)) & 1u; }
static inline __attribute__((always_inline)) uint32_t n4_pf(uint32_t k, uint32_t r)
{
    if (k == XK_EXPLICIT) return (r >> 2) & 1u;
    uint32_t v = r & 0xFFu; v ^= v >> 4;
    return ((0x6996u >> (v & 0xFu)) & 1u) ^ 1u;
}
static inline __attribute__((always_inline)) uint32_t n4_cf(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t bits, uint32_t cfo, uint32_t cf)
{
    if (k == XK_EXPLICIT) return r & 1u;
    if (cfo) return cf;
    uint32_t mk = n4_fmask(bits); a &= mk; b &= mk; r &= mk;
    switch (k) {
    case XK_ADD: return r < a;
    case XK_ADC: return cf ? (r <= a) : (r < a);
    case XK_SUB: return a < b;
    case XK_SBB: return cf ? (a <= b) : (a < b);
    default: return 0;
    }
}
static inline __attribute__((always_inline)) uint32_t n4_of(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t bits, uint32_t ofo, uint32_t of)
{
    if (k == XK_EXPLICIT) return (r >> 11) & 1u;
    if (ofo) return of;
    switch (k) {
    case XK_ADD: case XK_ADC: return (((a ^ r) & (b ^ r)) >> (bits - 1)) & 1u;
    case XK_SUB: case XK_SBB: return (((a ^ b) & (a ^ r)) >> (bits - 1)) & 1u;
    default: return 0;
    }
}
#define ZF() n4_zf(fk, fr, fbits)
#define SF() n4_sf(fk, fr, fbits)
#define PF() n4_pf(fk, fr)
#define CF() n4_cf(fk, fa, fb, fr, fbits, fcfo, fcf)
#define OF() n4_of(fk, fa, fb, fr, fbits, fofo, fof)
/* inc/dec as translated: only the carry is kept (override), no other field */
#define INCDEC_CF() do { uint32_t cf__ = CF(); fcfo = 1; fcf = cf__; } while (0)
/* x_shl32 / x_shr32 */
#define SHL32(v_, n_) ({ uint32_t v__ = (v_), n__ = (n_) & 31u, r__ = v__; if (n__) { r__ = v__ << n__; SETF(XK_LOGIC, 0, 0, r__, 32); \
                         fcfo = 1; fcf = (v__ >> (32u - n__)) & 1u; fofo = 1; fof = (r__ >> 31) ^ fcf; } r__; })
#define SHR32(v_, n_) ({ uint32_t v__ = (v_), n__ = (n_) & 31u, r__ = v__; if (n__) { r__ = v__ >> n__; SETF(XK_LOGIC, 0, 0, r__, 32); \
                         fcfo = 1; fcf = (v__ >> (n__ - 1u)) & 1u; fofo = 1; fof = (v__ >> 31) & 1u; } r__; })
/* fnstsw ax; test ah,imm */
#define FNSTSW() (eax = (eax & 0xFFFF0000u) | fsw)
#define TEST_AH(imm) do { uint8_t r__ = (uint8_t)((eax >> 8) & (imm)); SETF(XK_LOGIC, 0, 0, r__, 8); } while (0)

/* x87_compare: condition codes of this compare, the TOP field OR-ed in (never cleared) */
static inline __attribute__((always_inline)) uint16_t n4_cc(double a, double b)
{
    uint16_t cc;
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    uint32_t result;
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"
                     "mov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\t"
                     "it vs\n\tmovvs %0, #17664"
                     : "=r"(result) : "w"(a), "w"(b) : "cc");
    cc = (uint16_t)result;
#else
    if (isnan(a) || isnan(b)) cc = 0x4500;
    else if (a < b)           cc = 0x0100;
    else if (a == b)          cc = 0x4000;
    else                      cc = 0;
#endif
    return cc;
}
#define FCMP(a_, b_, depth_) (fsw = (uint16_t)((fsw & ~0x4700u) | n4_cc((a_), (b_)) | (((fsp0 - (depth_)) & 7u) << 11)))

/* x87 depth slots of one function: loaded at entry, saved around calls (the callee's writes win) and at exit */
#define N4_X87_LOCALS const uint32_t fsp0 = s->fsp0; uint16_t fsw = s->fsw; \
    double x1 = s->sl[1], x2 = s->sl[2], x3 = s->sl[3], x4 = s->sl[4], x5 = s->sl[5]
#define N4_X87_SAVE() (s->fsw = fsw, s->sl[1] = x1, s->sl[2] = x2, s->sl[3] = x3, s->sl[4] = x4, s->sl[5] = x5)
#define N4_X87_LOAD() (fsw = s->fsw, x1 = s->sl[1], x2 = s->sl[2], x3 = s->sl[3], x4 = s->sl[4], x5 = s->sl[5])
#define N4_REGS_IN uint32_t eax = R->eax, ecx = R->ecx, edx = R->edx, ebx = R->ebx, esp = R->esp, ebp = R->ebp, esi = R->esi, edi = R->edi
#define N4_REGS_OUT() (R->eax = eax, R->ecx = ecx, R->edx = edx, R->ebx = ebx, R->esp = esp, R->ebp = ebp, R->esi = esi, R->edi = edi)
#define N4_SAVE_ALL() (N4_REGS_OUT(), N4_FL_SAVE(), N4_X87_SAVE(), s->be = be)
#define N4_LOAD_ALL() (eax = R->eax, ecx = R->ecx, edx = R->edx, ebx = R->ebx, esp = R->esp, ebp = R->ebp, esi = R->esi, edi = R->edi, D = s->dirty, \
                       N4_FL_LOAD(), N4_X87_LOAD(), be = s->be)
#define N4_LOCALS n4_mem mm_ = s->m; const n4_mem *const m = &mm_; N4_REGS_IN; N4_FL_LOCALS; N4_X87_LOCALS; uint32_t be = s->be; int D = s->dirty
#define LO8(r, v) ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))
#define LO16(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))


/* Frame slots the guest reads back are served from locals while !dirty (only the slot's owner writes it: the result
 * lists lie above every frame, the ancestor stack grows up inside 88110's frame, both checked); after a wild write
 * (a result-list index outside the lists, an ancestor write past 88110's frame, a frame deeper than E88 - 64 KB) the
 * same reads come from guest memory, where every native write has been made in the guest's order. */
#define CR(cached, memread) (__builtin_expect(D, 0) ? (memread) : (cached))
/* frame stores: hf = the host address of E when the frame [E - lo, E + hi) lies in one page (then E + off translates to
 * hf + off), else NULL and the frame's two-page cache k picks the page per slot */
#define N4_FRAME(lo, hi) n4_stk k; n4_stk_at(m, &k, E - (lo)); \
    uint8_t *const hf = ((E - (lo)) >> 12) == ((E + (hi) - 1u) >> 12) ? k.h0 + ((E - (lo)) & 0xFFFu) + (lo) : NULL
#define FSW32(a, v) do { const uint32_t a__ = (a), v__ = (v); \
    if (__builtin_expect(hf != NULL, 1)) { uint8_t *p__ = hf + (int32_t)(a__ - E); n4_jlog(a__, p__, 4); memcpy(p__, &v__, 4); } \
    else n4_sw32(m, &k, a__, v__); } while (0)
#define FSW8(a, v) do { const uint32_t a__ = (a); const uint8_t v__ = (uint8_t)(v); \
    if (__builtin_expect(hf != NULL, 1)) { uint8_t *p__ = hf + (int32_t)(a__ - E); n4_jlog(a__, p__, 1); *p__ = v__; } \
    else n4_sw8(m, &k, a__, v__); } while (0)
#define FSWMF(a, v) do { const uint32_t a__ = (a); const float v__ = (v); \
    if (__builtin_expect(hf != NULL, 1)) { uint8_t *p__ = hf + (int32_t)(a__ - E); n4_jlog(a__, p__, 4); memcpy(p__, &v__, 4); } \
    else n4_swmf(m, &k, a__, v__); } while (0)
#define FSWF(a, d) ({ const float f__ = (float)(d); FSWMF((a), f__); f__; })
#define FSW16(a, v) do { const uint32_t a__ = (a); const uint16_t v__ = (uint16_t)(v); \
    if (__builtin_expect(hf != NULL, 1)) { uint8_t *p__ = hf + (int32_t)(a__ - E); n4_jlog(a__, p__, 2); memcpy(p__, &v__, 2); } \
    else n4_sw16(m, &k, a__, v__); } while (0)
#define DIRTY() (s->dirty = D = 1)
#define SPUSH(k, v) do { const uint32_t w__ = (v); esp -= 4u; FSW32(esp, w__); } while (0)
/* q fields while !dirty (the register holding q is then q itself) */
#define QBSP(r) CR(s->bsp, n4_r32(m, (r)))
#define QBITS(r) CR(s->bits, n4_r32(m, (r) + 8u))
#define QCTR(r) CR(s->ctr, n4_r32(m, (r) + 0xCu))
#define QRAD(r) CR((double)s->radius, n4_rf(m, (r) + 0x10u))
#define QRES(r) CR(s->res, n4_r32(m, (r) + 0x14u))
#define QANC(r) CR(s->anc, n4_r32(m, (r) + 0x18u))
#define QPU(r) CR((double)n4_u2f(s->pu), n4_rf(m, (r) + 0x220u))
#define QPV(r) CR((double)n4_u2f(s->pv), n4_rf(m, (r) + 0x224u))
#define BSPF(field, off, b) CR(s->field, n4_r32(m, (b) + (off)))
#define ZERO() CR(s->zero, n4_rf(m, N4_ZERO))
/* the axes table 0x1EAF30 + i (i = 4 * (2 * axis + side) + 0/2) */
#define AXIS(i) ((!D && (uint32_t)(i) <= 0x16u && !((i) & 1u)) ? (uint32_t)(int32_t)s->axes[(uint32_t)(i) >> 1] \
                                                               : (uint32_t)(int32_t)(int16_t)n4_r16(m, (uint32_t)(i) + N4_AXES))
/* q writes: 88110's frame while !dirty, the guest translation otherwise */
static inline __attribute__((always_inline)) void n4_qsw32(n4q *s, const n4_mem *m, uint32_t a, uint32_t v)
{
    if (__builtin_expect(s->hq != NULL, 1)) { uint8_t *p = s->hq + (int32_t)(a - s->E88); n4_jlog(a, p, 4); memcpy(p, &v, 4); }
    else n4_sw32(m, &s->kq, a, v);
}
static inline __attribute__((always_inline)) void n4_qw32(n4q *s, const n4_mem *m, int D, uint32_t a, uint32_t v)
{ if (!D) n4_qsw32(s, m, a, v); else n4_w32(m, a, v); }
/* a result-list store: inside the lists through their pages; anywhere else it is a wild write */
static inline __attribute__((always_inline)) void n4_lw32(n4q *s, const n4_mem *m, int *D, uint32_t a, uint32_t v)
{
    if (n4_inres(s, a)) { uint8_t *p = n4_rp(s, a); n4_jlog(a, p, 4); memcpy(p, &v, 4); return; }
    n4_w32(m, a, v); s->dirty = *D = 1;
}
static inline __attribute__((always_inline)) uint32_t n4_lr32(const n4q *s, const n4_mem *m, uint32_t a)
{ if (n4_inres(s, a)) return n4_ld32(n4_rp(s, a)); return n4_r32(m, a); }

/* The list scans (`cmp [base+i*4],v; je found; inc idx; movsx t,idx; cmp t,[count]; jl`): the index of the first
 * element equal to v among n (1..0x100) 4-byte elements at guest address a, n if none; per page, one translation. */
static inline __attribute__((always_inline)) uint32_t n4_find(const n4_mem *m, uint32_t a, uint32_t n, uint32_t v)
{
    uint32_t i = 0;
    while (i < n) {
        const uint32_t e = a + 4u * i, room = (0x1003u - (e & 0xFFFu)) >> 2;
        const uint32_t lim = n - i < room ? n - i : room;
        const uint8_t *p = N4_P(e);
        for (uint32_t j = 0; j < lim; ++j) if (n4_ld32(p + 4u * j) == v) return i + j;
        i += lim;
    }
    return n;
}

/* ---- f_000B0CB0: segment vs sphere. esp -> [ret][radius]; eax = center, ecx = start, edx = direction ----------
 * Inlined into 86F50's edge loop: rad/d0-d2 are the floats 86F50 just stored at [esp+4] and [edx..edx+8] (no write
 * can come between), k is 86F50's frame (B0CB0's frame lies inside it). */
static inline __attribute__((always_inline)) void n4_b0cb0(n4q *restrict s, n4r *restrict R, const n4_stk *kk, uint8_t *const hf,
                                                             const uint32_t E, float rad, float d0, float d1, float d2)
{
    N4_LOCALS;
    const n4_stk k = *kk;                                    /* 86F50's frame (E, hf, k: B0CB0's frame lies inside it) */
    s->edgesn++;
    esp -= 0x10u;
    const uint8_t *hv = n4_span(m, ecx, 12);
    const uint8_t *hc = (!D && eax == s->ctr) ? s->hctr : n4_span(m, eax, 12);
    x1 = HF(hv, ecx, 0, 12); x1 = x1 - HF(hc, eax, 0, 12);
    x2 = HF(hv, ecx, 4, 12); x2 = x2 - HF(hc, eax, 4, 12);
    x3 = HF(hv, ecx, 8, 12); x3 = x3 - HF(hc, eax, 8, 12);
    x4 = x3; x4 = x4 * x3;                                   /* fld st(0); fmul st,st(1) */
    x5 = x2; x5 = x5 * x2; x4 = x4 + x5;                     /* fld st(2); fmul st,st(3); faddp */
    x5 = x1; x5 = x5 * x1; x4 = x4 + x5;                     /* fld st(3); fmul st,st(4); faddp */
    x5 = (double)rad; x5 = x5 * (double)rad;                 /* fld/fmul [esp+14h] */
    x4 = x4 - x5;                                            /* fsubp: |start - center|^2 - r^2 */
    float q0 = FSWF(esp, x4);                        /* fst [esp] */
    FCMP(x4, ZERO(), 4);                                     /* fcomp: depth 3 after */
    FNSTSW(); TEST_AH(5);
    if (!PF()) { LO8(eax, 1); esp += 0x18u; goto out; }      /* inside at the start: fstp x3; al = 1 */
    /* B0CFC: the direction words (the floats 86F50 stored at [edx..edx+8]) */
    eax = n4_f2u(d0); ecx = n4_f2u(d1); edx = n4_f2u(d2);
    FSW32(esp + 0xCu, edx);
    x4 = (double)d2; x4 = x4 * x3;
    FSW32(esp + 8u, ecx);
    x5 = (double)d1;
    FSW32(esp + 4u, eax);
    x5 = x5 * x2; x4 = x4 + x5;
    x5 = (double)d0; x5 = x5 * x1; x4 = x4 + x5;
    const float t = FSWF(esp + 0x14u, x4);           /* fstp [esp+14h]: over the radius argument; fstp x3 */
    x1 = (double)t;
    FCMP(x1, ZERO(), 1);
    FNSTSW(); TEST_AH(1);
    if (ZF()) { LO8(eax, 0); esp += 0x18u; goto out; }       /* B0D80: moving away (xor al,al: no flags) */
    /* B0D41 */
    x1 = (double)d2; x1 = x1 * (double)d2;
    x2 = (double)d1; x2 = x2 * (double)d1; x1 = x1 + x2;
    x2 = (double)d0; x2 = x2 * (double)d0; x1 = x1 + x2;
    x2 = (double)t; x2 = x2 * (double)t;
    x3 = x1; x3 = x3 * (double)q0;
    x2 = x2 - x3;
    q0 = FSWF(esp, x2);                              /* fst [esp]: the discriminant */
    FCMP(x2, ZERO(), 2);
    FNSTSW(); TEST_AH(0x41);
    if (!PF()) { LO8(eax, 0); esp += 0x18u; goto out; }      /* B0D7E: no root */
    /* B0D88 (depth 1) */
    x1 = -x1;
    x1 = x1 - (double)t;
    FCMP(x1, ZERO(), 1);
    FNSTSW(); TEST_AH(5);
    if (!PF()) { be++; LO8(eax, 1); esp += 0x18u; goto out; }   /* jnp B0CF2 (a back-edge): fstp; al = 1 */
    /* B0D9F */
    x2 = x1; x2 = x2 * x1;
    FCMP(x2, (double)q0, 2);
    FNSTSW();
    TEST_AH(5);
    if (PF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }  /* B0DBB: xor eax,eax */
    else eax = 1;
    esp += 0x18u;
out:
    N4_SAVE_ALL();
}

/* ---- f_00086F50: surface test. esp -> [ret][q][surface index]; ret 8 --------------------------------------- */
static __attribute__((noinline)) void n4_86f50(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    s->surfacesn++;
    if (E - 0x54u < s->low) DIRTY();
    N4_FRAME(0x54u, 12u);                                    /* [E-0x54, E+0xC): this frame, B0CB0's and the arguments */
    const uint32_t sv_ebx = ebx, sv_ebp = ebp, sv_esi = esi;
    esp -= 0x2Cu;
    const uint32_t surf = n4_r32(m, E + 8u), qa = n4_r32(m, E + 4u);   /* 87E10's pushes */
    edx = surf;
    SPUSH(&k, ebx); SPUSH(&k, ebp); SPUSH(&k, esi);
    esi = qa;
    eax = QBSP(esi);
    eax = BSPF(surfs, 0x40u, eax);
    ecx = edx + edx * 2u;
    ebp = eax + ecx * 4u;
    const uint32_t srec = ebp;
    const uint8_t *hs = n4_span(m, srec, 12);
    { uint8_t r_ = H8(hs, srec, 8u, 12) & 8u; SETF(XK_LOGIC, 0, 0, r_, 8); }
    const uint32_t sv_edi = edi;
    SPUSH(&k, edi);
    FSW32(E - 0x1Cu, ebp);                          /* [esp+20h] */
    uint32_t s1c = ebp;                                      /* locals of this frame's slots: s<offset from E> */
    if (!ZF()) {
        /* 86F74: the surface's tested bit */
        ecx = H8(hs, srec, 9u, 12);
        eax = CR((uint32_t)(int32_t)(int16_t)s->cap, (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 4u));
        SETF(XK_SUB, ecx, eax, ecx - eax, 32);
        if (!(SF() == OF())) {
            edi = QBITS(esi);
            eax = ecx;
            ecx &= 0x1Fu;
            ebx = 1u;
            ebx = SHL32(ebx, (uint8_t)ecx);
            eax = SHR32(eax, 5u);
            { uint32_t r_ = n4_r32(m, edi + eax * 4u) & ebx; SETF(XK_LOGIC, 0, 0, r_, 32); }
            if (ZF()) goto L_872BA;
        }
    }
    /* 86F9B: vertex ring */
    x1 = QRAD(esi);
    eax = H32(hs, srec, 4u, 12);
    x2 = x1; x2 = x2 * x1;
    ecx = esp + 0x14u;
    FSW8(E - 0x29u, 0); uint8_t hit = 0;
    FSW32(E - 0xCu, ecx); const uint32_t s0c = ecx;
    const float r2 = FSWF(E - 8u, x2);
    goto L_86FC0;
L_86FBA:
    edx = CR(surf, n4_r32(m, E + 8u));
L_86FC0:
    {
        s->vertsn++;
        ecx = QBSP(esi);
        edi = BSPF(edges, 0x4Cu, ecx);
        eax = eax + eax * 2u;
        edi = edi + eax * 8u;
        const uint32_t erec = edi;
        const uint8_t *he = n4_span(m, erec, 24);
        { uint32_t a_ = H32(he, erec, 0x14u, 24); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
        LO8(eax, ZF());
        ebx = (uint8_t)eax;
        edx = H32(he, erec, ebx * 4u, 24);
        eax = edx;
        eax = SHL32(eax, 4u);
        eax = eax + BSPF(verts, 0x58u, ecx);
        ecx = QCTR(esi);
        FSW32(E - 4u, ebx); uint32_t s04 = ebx;
        FSW32(E - 0x10u, eax);
        FSW32(E - 0x14u, ecx);
        const uint32_t vrec = eax, crec = ecx;
        eax = CR(s0c, n4_r32(m, E - 0xCu));
        ecx = vrec;
        const uint8_t *hv = n4_span(m, vrec, 12);
        const float a0 = HMF(hv, vrec, 0u, 12), a2 = HMF(hv, vrec, 4u, 12), a3 = HMF(hv, vrec, 8u, 12);
        ecx = crec;
        const uint8_t *hc = (!D && crec == s->ctr) ? s->hctr : n4_span(m, crec, 12);
        const float b0 = HMF(hc, crec, 0u, 12), b2 = HMF(hc, crec, 4u, 12), b3 = HMF(hc, crec, 8u, 12);
        float e0 = a0 - b0, e2 = a2 - b2, e3 = a3 - b3;      /* subps (lane 1: 0 - 0) */
        e0 = e0 * e0; e2 = e2 * e2; e3 = e3 * e3;            /* mulps */
        float sum = e0;                                      /* movss xmm2,xmm0 */
        sum = sum + e2;                                      /* shufps 0Eh: (e2,e3,e0,e0); addss */
        sum = sum + e3;                                      /* shufps 39h: (e3,e0,e0,e2); addss */
        s->xmm0[0] = e3; s->xmm0[1] = e0; s->xmm0[2] = e0; s->xmm0[3] = e2;
        s->xmm1[0] = b0; s->xmm1[1] = 0.0f; s->xmm1[2] = b2; s->xmm1[3] = b3;
        s->xmm2_0 = sum; s->xmm = 1;
        float d2v = sum;
        if (!D) FSWMF(E - 0x28u, sum); else n4_wmf(m, eax, sum);   /* movss [eax]: eax = [esp+30h] = esp+14h */
        esi = CR(qa, n4_r32(m, E + 4u));
        x1 = CR((double)d2v, n4_rf(m, E - 0x28u));
        FCMP(x1, CR((double)r2, n4_rf(m, E - 8u)), 1);
        FNSTSW(); TEST_AH(0x41);
        if (PF()) goto L_8708B;
        /* 8703B: the vertex touches the sphere: add it to the vertex list */
        eax = QRES(esi);
        ebp = n4_lr32(s, m, eax + 0x808u);
        ebx = 0;
        SETF(XK_LOGIC, 0, 0, ebp, 32);
        if (!(ZF() || SF() != OF())) {
            ecx = 0;
            if (!D && eax == s->res && ebp <= 0x100u) {      /* the fast scan: exact exit state below */
                const uint32_t n = ebp, i = n4_find(m, eax + 0x80Cu, n, edx);
                s->scans++; s->scanned += i < n ? i + 1u : n;
                if (i) { uint32_t p_ = n4_lr32(s, m, eax + 0x80Cu + 4u * (i - 1u)); fcfo = 1; fcf = p_ < edx; }
                ebx = i; ecx = i;
                if (i < n) { SETF(XK_SUB, edx, edx, 0, 32); be += i; goto L_8707E; }
                SETF(XK_SUB, n, n, 0, 32); be += n - 1u;
            } else {
                for (;;) {
                    { uint32_t a_ = n4_lr32(s, m, eax + ecx * 4u + 0x80Cu); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
                    if (ZF()) goto L_8707E;
                    INCDEC_CF(); ebx = ebx + 1u;
                    ecx = (uint32_t)(int32_t)(int16_t)ebx;
                    { uint32_t b_ = n4_lr32(s, m, eax + 0x808u); SETF(XK_SUB, ecx, b_, ecx - b_, 32); }
                    if (SF() != OF()) { be++; continue; }
                    break;
                }
            }
        }
        SETF(XK_SUB, ebp, 0x100u, ebp - 0x100u, 32);
        if (!(SF() == OF())) {
            n4_lw32(s, m, &D, eax + ebp * 4u + 0x80Cu, edx);
            { INCDEC_CF(); uint32_t v_ = n4_lr32(s, m, eax + 0x808u) + 1u; n4_lw32(s, m, &D, eax + 0x808u, v_); }
            esi = CR(qa, n4_r32(m, E + 4u));
        }
    L_8707E:
        ebx = CR(s04, n4_r32(m, E - 4u));
        ebp = CR(s1c, n4_r32(m, E - 0x1Cu));
        FSW8(E - 0x29u, 1); hit = 1;
    L_8708B:
        eax = (!D || edi == erec) ? H32(he, erec, ebx * 4u + 8u, 24) : n4_r32(m, edi + ebx * 4u + 8u);
        ecx = (!D && ebp == srec) ? H32(hs, srec, 4u, 12) : n4_r32(m, ebp + 4u);
        SETF(XK_SUB, eax, ecx, eax - ecx, 32);
        if (!ZF()) { be++; goto L_86FBA; }
    }
    /* 8709A: edges */
    edi = ecx;
L_870A0:
    {
        eax = QBSP(esi);
        ecx = BSPF(edges, 0x4Cu, eax);
        ebx = BSPF(verts, 0x58u, eax);
        edx = edi + edi * 2u;
        ebp = ecx + edx * 8u;
        const uint32_t erec = ebp;
        const uint8_t *he = n4_span(m, erec, 24);
        edx = CR(surf, n4_r32(m, E + 8u));
        { uint32_t a_ = H32(he, erec, 0x14u, 24); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
        LO8(edx, ZF());
        ecx = (uint8_t)edx;
        FSW32(E - 4u, ecx); const uint32_t s04 = ecx;
        ecx = H32(he, erec, ecx * 4u, 24);
        eax = 0;
        ecx = SHL32(ecx, 4u);
        ecx = ecx + ebx;
        { uint8_t r_ = (uint8_t)edx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        LO8(eax, ZF());
        SPUSH(&k, ecx);
        edx = esp + 0x18u;
        eax = H32(he, erec, eax * 4u, 24);
        eax = SHL32(eax, 4u);
        const uint32_t va = ecx, vb = eax + ebx;
        const uint8_t *ha = n4_span(m, va, 12), *hb = n4_span(m, vb, 12);
        x1 = HF(hb, vb, 0u, 12);
        eax = vb;
        x1 = x1 - HF(ha, va, 0u, 12);
        const float d0 = FSWF(E - 0x28u, x1);
        x1 = HF(hb, vb, 4u, 12); x1 = x1 - HF(ha, va, 4u, 12);
        const float d1 = FSWF(E - 0x24u, x1);
        x1 = HF(hb, vb, 8u, 12);
        eax = QCTR(esi);
        x1 = x1 - HF(ha, va, 8u, 12);
        const float d2 = FSWF(E - 0x20u, x1);
        x1 = QRAD(esi);
        const float rad = FSWF(esp, x1);
        SPUSH(&k, 0x87108u);
        N4_SAVE_ALL();
        n4_b0cb0(s, R, &k, hf, E, rad, d0, d1, d2);
        N4_LOAD_ALL();
        { uint8_t r_ = (uint8_t)eax; SETF(XK_LOGIC, 0, 0, r_, 8); }
        if (!ZF()) {
            /* 8710C: the edge touches the sphere: add it to the edge list */
            eax = QRES(esi);
            ebx = n4_lr32(s, m, eax + 0x404u);
            edx = 0;
            SETF(XK_LOGIC, 0, 0, ebx, 32);
            if (!(ZF() || SF() != OF())) {
                ecx = 0;
                if (!D && eax == s->res && ebx <= 0x100u) {
                    const uint32_t n = ebx, i = n4_find(m, eax + 0x408u, n, edi);
                    s->scans++; s->scanned += i < n ? i + 1u : n;
                    if (i) { uint32_t p_ = n4_lr32(s, m, eax + 0x408u + 4u * (i - 1u)); fcfo = 1; fcf = p_ < edi; }
                    edx = i; ecx = i;
                    if (i < n) { SETF(XK_SUB, edi, edi, 0, 32); be += i; goto L_8714E; }
                    SETF(XK_SUB, n, n, 0, 32); be += n - 1u;
                } else {
                    for (;;) {
                        { uint32_t a_ = n4_lr32(s, m, eax + ecx * 4u + 0x408u); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
                        if (ZF()) goto L_8714E;
                        INCDEC_CF(); edx = edx + 1u;
                        ecx = (uint32_t)(int32_t)(int16_t)edx;
                        { uint32_t b_ = n4_lr32(s, m, eax + 0x404u); SETF(XK_SUB, ecx, b_, ecx - b_, 32); }
                        if (SF() != OF()) { be++; continue; }
                        break;
                    }
                }
            }
            SETF(XK_SUB, ebx, 0x100u, ebx - 0x100u, 32);
            if (!(SF() == OF())) {
                n4_lw32(s, m, &D, eax + ebx * 4u + 0x408u, edi);
                { INCDEC_CF(); uint32_t v_ = n4_lr32(s, m, eax + 0x404u) + 1u; n4_lw32(s, m, &D, eax + 0x404u, v_); }
                esi = CR(qa, n4_r32(m, E + 4u));
            }
        L_8714E:
            FSW8(E - 0x29u, 1); hit = 1;
        }
        /* 87153 */
        ecx = CR(s04, n4_r32(m, E - 4u));
        edx = CR(s1c, n4_r32(m, E - 0x1Cu));
        edi = (!D || ebp == erec) ? H32(he, erec, ecx * 4u + 8u, 24) : n4_r32(m, ebp + ecx * 4u + 8u);
        { uint32_t b_ = (!D && edx == srec) ? H32(hs, srec, 4u, 12) : n4_r32(m, edx + 4u); SETF(XK_SUB, edi, b_, edi - b_, 32); }
        if (!ZF()) { be++; goto L_870A0; }
    }
    LO8(eax, CR(hit, n4_r8(m, E - 0x29u)));
    { uint8_t r_ = (uint8_t)eax; SETF(XK_LOGIC, 0, 0, r_, 8); }
    if (!ZF()) goto L_87287;
    {
        /* 87174: point in polygon on the projected axes (no list write and no call in this loop) */
        eax = edx;
        edi = (!D && eax == srec) ? H32(hs, srec, 4u, 12) : n4_r32(m, eax + 4u);
        eax = QBSP(esi);
        ecx = BSPF(edges, 0x4Cu, eax);
        edx = BSPF(verts, 0x58u, eax);
        LO8(eax, CR(s->side, n4_r8(m, esi + 0x21Eu)));
        FSW32(E - 8u, ecx); const uint32_t s08 = ecx;
        ecx = CR((uint32_t)(int32_t)(int16_t)s->axis, (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 0x21Cu));
        eax = (uint8_t)eax;
        eax = eax + ecx * 2u;
        eax = SHL32(eax, 2u);
        ecx = AXIS(eax);
        eax = AXIS(eax + 2u);
        ecx = SHL32(ecx, 2u);
        eax = SHL32(eax, 2u);
        FSW32(E - 0x14u, edi); const uint32_t s14 = edi;
        FSW32(E - 4u, edx); const uint32_t s04 = edx;
        FSW32(E - 0xCu, ecx); const uint32_t s0c2 = ecx;
        FSW32(E - 0x10u, eax); const uint32_t s10 = eax;
        const double pu = QPU(esi), pv = QPV(esi);
        const uint32_t sface = CR(surf, n4_r32(m, E + 8u));
        goto L_871C5;
    L_871C1:
        edx = s04;
    L_871C5:
        eax = s08;
        ecx = edi + edi * 2u;
        const uint32_t erec = eax + ecx * 8u;
        const uint8_t *he = n4_span(m, erec, 24);
        ebp = H32(he, erec, 0x14u, 24);
        edi = erec;
        ecx = sface;
        SETF(XK_SUB, ebp, ecx, ebp - ecx, 32);
        LO8(ecx, ZF());
        ebp = (uint8_t)ecx;
        eax = H32(he, erec, ebp * 4u, 24);
        eax = SHL32(eax, 4u);
        eax = eax + edx;
        ebx = 0;
        { uint8_t r_ = (uint8_t)ecx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        LO8(ebx, ZF());
        ecx = H32(he, erec, ebx * 4u, 24);
        ebx = CR(s->side, n4_r8(m, esi + 0x21Eu));
        ecx = SHL32(ecx, 4u);
        ecx = ecx + edx;
        edx = CR((uint32_t)(int32_t)(int16_t)s->axis, (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 0x21Cu));
        edx = ebx + edx * 2u;
        edx = SHL32(edx, 2u);
        ebx = AXIS(edx);
        edx = AXIS(edx + 2u);
        const uint32_t va = eax, vb = ecx;
        const uint8_t *ha = n4_span(m, va, 12), *hb = n4_span(m, vb, 12);
        x1 = HF(ha, va, ebx * 4u, 12);
        x2 = HF(ha, va, edx * 4u, 12);
        eax = s0c2;
        x3 = HF(hb, vb, eax, 12);
        edx = s10;
        const float c20 = FSWF(E - 0x1Cu, x3);
        x3 = HF(hb, vb, edx, 12);
        const float c24 = FSWF(E - 0x18u, x3);
        { double t_ = x2; x2 = x1; x1 = t_; }                /* fxch */
        x2 = x2 - pu;
        const float c14 = FSWF(E - 0x28u, x2);      /* fstp: depth 1 */
        x1 = x1 - pv;
        x2 = (double)c20; x2 = x2 - pu;
        x3 = (double)c24; x3 = x3 - pv;
        x4 = (double)c14;
        x4 = x4 * x3;                                        /* fmul st,st(1) */
        { double t_ = x4; x4 = x1; x1 = t_; }                /* fxch st(3) */
        x4 = x4 * x2;                                        /* fmul st,st(2) */
        x1 = x1 - x4;                                        /* fsubp st(3),st: depth 3 */
        { double t_ = x3; x3 = x1; x1 = t_; }                /* fxch st(2) */
        FCMP(x3, ZERO(), 3);                                 /* fcomp: depth 2 */
        FNSTSW();
        x1 = x2;                                             /* fstp st(1): depth 1 */
        TEST_AH(5);
        /* fstp st(0): depth 0 */
        if (!PF()) goto L_872BA;
        edi = H32(he, erec, ebp * 4u + 8u, 24);
        SETF(XK_SUB, edi, s14, edi - s14, 32);
        if (!ZF()) { be++; goto L_871C1; }
    }
L_87287:
    {
        /* the surface touches the sphere: add it to the surface list */
        esi = QRES(esi);
        edx = n4_lr32(s, m, esi);
        ecx = 0;
        SETF(XK_LOGIC, 0, 0, edx, 32);
        const uint32_t sface = CR(surf, n4_r32(m, E + 8u));
        if (!(ZF() || SF() != OF())) {
            eax = 0;
            if (!D && esi == s->res && edx <= 0x100u) {
                const uint32_t n = edx, i = n4_find(m, esi + 4u, n, sface);
                s->scans++; s->scanned += i < n ? i + 1u : n;
                if (i) { uint32_t p_ = n4_lr32(s, m, esi + 4u * (i - 1u) + 4u); fcfo = 1; fcf = p_ < sface; edi = n; }
                ecx = i;
                if (i < n) { eax = sface; SETF(XK_SUB, sface, sface, 0, 32); be += i; goto L_872BA; }
                eax = n; edi = n; SETF(XK_SUB, n, n, 0, 32); be += n - 1u;
            } else {
                for (;;) {
                    eax = n4_lr32(s, m, esi + eax * 4u + 4u);
                    { uint32_t b_ = CR(surf, n4_r32(m, E + 8u)); SETF(XK_SUB, eax, b_, eax - b_, 32); }
                    if (ZF()) goto L_872BA;
                    edi = n4_lr32(s, m, esi);
                    INCDEC_CF(); ecx = ecx + 1u;
                    eax = (uint32_t)(int32_t)(int16_t)ecx;
                    SETF(XK_SUB, eax, edi, eax - edi, 32);
                    if (SF() != OF()) { be++; continue; }
                    break;
                }
            }
        }
        SETF(XK_SUB, edx, 0x100u, edx - 0x100u, 32);
        if (!(SF() == OF())) {
            ecx = CR(surf, n4_r32(m, E + 8u));
            n4_lw32(s, m, &D, esi + edx * 4u + 4u, ecx);
            { INCDEC_CF(); uint32_t v_ = n4_lr32(s, m, esi) + 1u; n4_lw32(s, m, &D, esi, v_); }
        }
    }
L_872BA:
    edi = CR(sv_edi, n4_r32(m, E - 0x3Cu));
    esi = CR(sv_esi, n4_r32(m, E - 0x38u));
    ebp = CR(sv_ebp, n4_r32(m, E - 0x34u));
    ebx = CR(sv_ebx, n4_r32(m, E - 0x30u));
    esp = E + 12u;
    N4_SAVE_ALL();
}

/* ---- f_00087E10: BSP2D traversal. esp -> [ret]; ecx = q, edx = node ------------------------------------------ */
static __attribute__((noinline)) void n4_87e10(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    if (E - 0x18u < s->low) DIRTY();
    N4_FRAME(0x18u, 0u);
    const uint32_t sv_ebx = ebx, sv_esi = esi, sv_edi = edi;
    SETF(XK_LOGIC, 0, 0, edx, 32);
    SPUSH(&k, ebx); SPUSH(&k, esi); SPUSH(&k, edi);
    edi = ecx;
    if (SF()) goto L_87E81;
    for (;;) {
        s->nodes2n++;
        eax = QBSP(edi);
        ecx = edx + edx * 4u;
        edx = BSPF(nodes2, 0x34u, eax);
        esi = edx + ecx * 4u;
        const uint32_t nrec = esi;
        const uint8_t *hn = n4_span(m, nrec, 20);
        x1 = HF(hn, nrec, 4u, 20);
        x1 = x1 * QPV(edi);
        x2 = QPU(edi);
        x2 = x2 * HF(hn, nrec, 0u, 20);
        x1 = x1 + x2;                                        /* faddp */
        x1 = x1 - HF(hn, nrec, 8u, 20);
        FCMP(x1, QRAD(edi), 1);
        FNSTSW(); TEST_AH(0x41);
        if (PF()) { SETF(XK_LOGIC, (uint8_t)ecx, (uint8_t)ecx, 0, 8); LO8(ecx, 0); }   /* xor cl,cl */
        else LO8(ecx, 1);
        x2 = QRAD(edi);
        x2 = -x2;
        { double t_ = x2; x2 = x1; x1 = t_; }                /* fxch */
        FCMP(x2, x1, 2);                                     /* fcompp */
        FNSTSW(); TEST_AH(1);
        if (!ZF()) { SETF(XK_LOGIC, (uint8_t)ebx, (uint8_t)ebx, 0, 8); LO8(ebx, 0); }   /* xor bl,bl */
        else LO8(ebx, 1);
        { uint8_t r_ = (uint8_t)ecx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        if (!ZF()) {
            edx = H32(hn, nrec, 0xCu, 20);
            ecx = edi;
            SPUSH(&k, 0x87E76u);
            N4_SAVE_ALL();
            n4_87e10(s, R);
            N4_LOAD_ALL();
        }
        { uint8_t r_ = (uint8_t)ebx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        if (ZF()) goto L_87E8E;
        edx = (!D || esi == nrec) ? H32(hn, nrec, 0x10u, 20) : n4_r32(m, esi + 0x10u);
        SETF(XK_LOGIC, 0, 0, edx, 32);
        if (!SF()) { be++; continue; }
        break;
    }
L_87E81:
    edx &= 0x7FFFFFFFu;
    SPUSH(&k, edx); SPUSH(&k, edi);
    SPUSH(&k, 0x87E8Eu);
    N4_SAVE_ALL();
    n4_86f50(s, R);
    N4_LOAD_ALL();
L_87E8E:
    edi = CR(sv_edi, n4_r32(m, E - 0xCu));
    esi = CR(sv_esi, n4_r32(m, E - 8u));
    ebx = CR(sv_ebx, n4_r32(m, E - 4u));
    esp = E + 4u;
    N4_SAVE_ALL();
}

/* ---- f_00087EA0: BSP3D traversal. esp -> [ret]; ecx = q, edx = node ------------------------------------------ */
/* an ancestor-plane store at q+0x1C+4*i: past 88110's frame (a BSP deeper than 131 straddling planes) is a wild write */
static inline __attribute__((always_inline)) void n4_aw32(n4q *s, const n4_mem *m, int *D, uint32_t a, uint32_t v)
{
    if (!*D && a - (s->q + 0x1Cu) < s->E88 - (s->q + 0x1Cu)) { n4_qsw32(s, m, a, v); return; }
    n4_w32(m, a, v); s->dirty = *D = 1;
}
static __attribute__((noinline)) void n4_87ea0(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    if (E - 0x24u < s->low) DIRTY();
    N4_FRAME(0x24u, 0u);
    const uint32_t sv_ebx = ebx, sv_ebp = ebp, sv_esi = esi, sv_edi = edi;
    esp -= 0x10u;
    SETF(XK_LOGIC, 0, 0, edx, 32);
    SPUSH(&k, ebx); SPUSH(&k, ebp); SPUSH(&k, esi); SPUSH(&k, edi);
    esi = ecx;
    uint32_t s10 = 0;                                        /* [esp+10h]: -radius (float bits), later the leaf */
    if (SF()) goto L_87F1A;
    {
        x1 = QRAD(esi);
        eax = QBSP(esi);
        ebx = BSPF(nodes3, 4u, eax);
        x1 = -x1;
        ebp = BSPF(planes, 0x10u, eax);
        const float nr = FSWF(E - 0x10u, x1); s10 = n4_f2u(nr);
        ecx = QCTR(esi);
        const uint8_t *hc = (!D && ecx == s->ctr) ? s->hctr : n4_span(m, ecx, 12);
        const double rad = QRAD(esi);
        for (;;) {                                           /* 87EC1 */
            s->nodes3n++;
            eax = edx + edx * 2u;
            edi = ebx + eax * 4u;
            const uint32_t nrec = edi;
            const uint8_t *hn = n4_span(m, nrec, 12);
            eax = H32(hn, nrec, 0u, 12);
            eax = SHL32(eax, 4u);
            const uint32_t prec = eax + ebp;
            const uint8_t *hp = n4_span(m, prec, 16);
            x1 = HF(hp, prec, 8u, 16);
            eax = prec;
            x1 = x1 * HF(hc, ecx, 8u, 12);
            x2 = HF(hp, prec, 4u, 16); x2 = x2 * HF(hc, ecx, 4u, 12);
            x1 = x1 + x2;
            x2 = HF(hp, prec, 0u, 16); x2 = x2 * HF(hc, ecx, 0u, 12);
            x1 = x1 + x2;
            x1 = x1 - HF(hp, prec, 0xCu, 16);
            FCMP(x1, rad, 1);
            FNSTSW(); TEST_AH(5);
            if (PF()) { SETF(XK_LOGIC, (uint8_t)edx, (uint8_t)edx, 0, 8); LO8(edx, 0); }   /* xor dl,dl */
            else LO8(edx, 1);
            FCMP(x1, (double)nr, 1);                         /* fcomp [esp+10h]: depth 0 after */
            FNSTSW(); TEST_AH(0x41);
            if (!ZF()) {                                     /* 87F94: the back side only */
                SETF(XK_LOGIC, (uint8_t)eax, (uint8_t)eax, 0, 8); LO8(eax, 0);
                be++;
            } else {
                { uint8_t r_ = (uint8_t)edx; SETF(XK_LOGIC, 0, 0, r_, 8); }
                LO8(eax, 1);
                if (!ZF()) {                                 /* 87F9B: both sides */
                    eax = H32(hn, nrec, 0u, 12);
                    ecx = QANC(esi);
                    eax |= 0x80000000u;
                    n4_aw32(s, m, &D, esi + ecx * 4u + 0x1Cu, eax);
                    { INCDEC_CF(); uint32_t v_ = QANC(esi) + 1u; n4_qw32(s, m, D, esi + 0x18u, v_); s->anc = v_; }
                    edx = H32(hn, nrec, 4u, 12);
                    ecx = esi;
                    SPUSH(&k, 0x87FB6u);
                    N4_SAVE_ALL();
                    n4_87ea0(s, R);
                    N4_LOAD_ALL();
                    const uint8_t *hn2 = (!D || edi == nrec) ? hn : n4_span(m, edi, 12);
                    ebp = QANC(esi);
                    INCDEC_CF(); ebp = ebp - 1u;
                    n4_qw32(s, m, D, esi + 0x18u, ebp); s->anc = ebp;
                    edx = H32(hn2, edi, 0u, 12);
                    edx &= 0x7FFFFFFFu;
                    eax = ebp;
                    n4_aw32(s, m, &D, esi + eax * 4u + 0x1Cu, edx);
                    { INCDEC_CF(); uint32_t v_ = QANC(esi) + 1u; n4_qw32(s, m, D, esi + 0x18u, v_); s->anc = v_; }
                    edx = H32(hn2, edi, 8u, 12);
                    ecx = esi;
                    SPUSH(&k, 0x87FD8u);
                    N4_SAVE_ALL();
                    n4_87ea0(s, R);
                    N4_LOAD_ALL();
                    eax = QANC(esi);
                    edi = CR(sv_edi, n4_r32(m, E - 0x20u));
                    INCDEC_CF(); eax = eax - 1u;
                    n4_qw32(s, m, D, esi + 0x18u, eax); s->anc = eax;
                    esi = CR(sv_esi, n4_r32(m, E - 0x1Cu));
                    ebp = CR(sv_ebp, n4_r32(m, E - 0x18u));
                    ebx = CR(sv_ebx, n4_r32(m, E - 0x14u));
                    esp = E + 4u;
                    N4_SAVE_ALL();
                    return;
                }
            }
            /* 87F0F */
            edx = (uint8_t)eax;
            edx = H32(hn, nrec, edx * 4u + 4u, 12);
            SETF(XK_LOGIC, 0, 0, edx, 32);
            if (!SF()) { be++; continue; }
            break;
        }
    }
L_87F1A:
    SETF(XK_SUB, edx, 0xFFFFFFFFu, edx + 1u, 32);
    if (ZF()) goto L_880FC;
    {
        /* 87F23: a leaf */
        s->leavesn++;
        eax = QBSP(esi);
        ecx = BSPF(leaves, 0x1Cu, eax);
        eax = QRES(esi);
        edx &= 0x7FFFFFFFu;
        edi = ecx + edx * 8u;
        ecx = n4_lr32(s, m, eax + 0xC0Cu);
        SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);
        FSW32(E - 0x10u, edi); s10 = edi;
        if (!(SF() == OF())) {
            n4_lw32(s, m, &D, eax + ecx * 4u + 0xC10u, edx);
            eax = QRES(esi);
            { INCDEC_CF(); uint32_t v_ = n4_lr32(s, m, eax + 0xC0Cu) + 1u; n4_lw32(s, m, &D, eax + 0xC0Cu, v_); }
        }
        const uint8_t *hl = n4_span(m, edi, 8);
        edx = (uint32_t)(int32_t)(int16_t)H16(hl, edi, 2u, 8);
        ebp = H32(hl, edi, 4u, 8);
        edx = edx + ebp;
        SETF(XK_SUB, ebp, edx, ebp - edx, 32);
        if (SF() == OF()) goto L_880FC;
    }
    for (;;) {                                               /* 87F67: the leaf's BSP2D references */
        edi = QBSP(esi);
        eax = BSPF(refs, 0x28u, edi);
        ebx = eax + ebp * 8u;
        const uint32_t rrec = ebx;
        const uint8_t *hr = n4_span(m, rrec, 8);
        eax = QANC(esi);
        edx = 0;
        SETF(XK_LOGIC, 0, 0, eax, 32);
        if (ZF() || SF() != OF()) goto L_880E6;
        ecx = H32(hr, rrec, 0u, 8);
        eax = 0;
        if (!D && esi == s->q && s->anc <= 131u) {           /* the ancestor stack inside 88110's frame */
            const uint32_t n = s->anc, i = n4_find(m, esi + 0x1Cu, n, ecx);
            s->scans++; s->scanned += i < n ? i + 1u : n;
            if (i) { uint32_t p_ = n4_r32(m, esi + 0x1Cu + 4u * (i - 1u)); fcfo = 1; fcf = p_ < ecx; }
            edx = i; eax = i;
            if (i == n) { SETF(XK_SUB, n, n, 0, 32); be += n - 1u; goto L_880E6; }
            SETF(XK_SUB, ecx, ecx, 0, 32); be += i;
        } else {
            for (;;) {
                { uint32_t a_ = n4_r32(m, esi + eax * 4u + 0x1Cu); SETF(XK_SUB, a_, ecx, a_ - ecx, 32); }
                if (ZF()) break;
                INCDEC_CF(); edx = edx + 1u;
                eax = (uint32_t)(int32_t)(int16_t)edx;
                { uint32_t b_ = n4_r32(m, esi + 0x18u); SETF(XK_SUB, eax, b_, eax - b_, 32); }
                if (SF() != OF()) { be++; continue; }
                goto L_880E6;
            }
        }
        {
            /* 87FE7: the reference's plane is on the ancestor stack: project the center, pick the axes, test in 2D */
            eax = BSPF(planes, 0x10u, edi);
            ecx &= 0x7FFFFFFFu;
            ecx = SHL32(ecx, 4u);
            ecx = ecx + eax;
            const uint32_t prec = ecx;
            const uint8_t *hp = n4_span(m, prec, 16);
            eax = QCTR(esi);
            const uint8_t *hc = (!D && eax == s->ctr) ? s->hctr : n4_span(m, eax, 12);
            x1 = HF(hc, eax, 8u, 12); x1 = x1 * HF(hp, prec, 8u, 16);
            x2 = HF(hc, eax, 4u, 12); x2 = x2 * HF(hp, prec, 4u, 16);
            x1 = x1 + x2;
            x2 = HF(hp, prec, 0u, 16); x2 = x2 * HF(hc, eax, 0u, 12);
            x1 = x1 + x2;
            x1 = x1 - HF(hp, prec, 0xCu, 16);
            x1 = -x1;
            x2 = x1; x2 = x2 * HF(hp, prec, 0u, 16); x2 = x2 + HF(hc, eax, 0u, 12);
            float pj[3];
            pj[0] = FSWF(E - 0xCu, x2);
            x2 = x1; x2 = x2 * HF(hp, prec, 4u, 16); x2 = x2 + HF(hc, eax, 4u, 12);
            pj[1] = FSWF(E - 8u, x2);
            x1 = x1 * HF(hp, prec, 8u, 16); x1 = x1 + HF(hc, eax, 8u, 12);
            pj[2] = FSWF(E - 4u, x1);
            x1 = fabs(HF(hp, prec, 0u, 16));
            x2 = fabs(HF(hp, prec, 4u, 16));
            x3 = fabs(HF(hp, prec, 8u, 16));
            FCMP(x3, x2, 3);                                 /* fcom */
            FNSTSW(); TEST_AH(1);
            if (ZF()) {
                FCMP(x3, x1, 3);                             /* fcomp st(2): depth 2 */
                FNSTSW(); TEST_AH(1);
                if (ZF()) { eax = 2; goto L_88072; }         /* fstp st(0) x2 */
            }
            /* 8805C: fstp st(0) after the first compare; 8805E at depth 2 */
            FCMP(x2, x1, 2);                                 /* fcomp: depth 1 */
            FNSTSW();
            /* fstp st(0): depth 0 */
            TEST_AH(1);
            if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }
            else eax = 1;
        L_88072:
            edx = (uint32_t)(int32_t)(int16_t)eax;
            if (!D) { uint8_t *p_ = s->hq ? s->hq + (int32_t)(esi + 0x21Cu - s->E88) : N4_SP(&s->kq, esi + 0x21Cu);
                      n4_jlog(esi + 0x21Cu, p_, 2); uint16_t v_ = (uint16_t)eax; memcpy(p_, &v_, 2); s->axis = (uint16_t)eax; }
            else n4_w16(m, esi + 0x21Cu, (uint16_t)eax);
            x1 = HF(hp, prec, edx * 4u, 16);
            FCMP(x1, ZERO(), 1);
            FNSTSW(); TEST_AH(0x41);
            if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }
            else eax = 1;
            ecx = H32(hr, rrec, 0u, 8);
            ecx &= 0x80000000u;
            ecx = 0u - ecx;                                  /* neg: no flags in the translation */
            { uint32_t cf_ = CF(); uint32_t r_ = ecx - ecx - cf_; SETFC(XK_SBB, ecx, ecx, r_, 32, cf_); ecx = r_; }
            eax = (uint8_t)eax;
            ecx = 0u - ecx;
            SETF(XK_SUB, eax, ecx, eax - ecx, 32);
            LO8(eax, !ZF());
            if (!D) { uint8_t *p_ = s->hq ? s->hq + (int32_t)(esi + 0x21Eu - s->E88) : N4_SP(&s->kq, esi + 0x21Eu);
                      n4_jlog(esi + 0x21Eu, p_, 1); *p_ = (uint8_t)eax; s->side = (uint8_t)eax; }
            else n4_w8(m, esi + 0x21Eu, (uint8_t)eax);
            ecx = (uint8_t)eax;
            eax = ecx + edx * 2u;
            eax = SHL32(eax, 2u);
            edx = AXIS(eax + 2u);
            eax = AXIS(eax);
            esp = E - 0x20u;
            x1 = (!D && edx <= 2u) ? (double)pj[edx] : n4_rf(m, esp + edx * 4u + 0x14u);
            ecx = (!D && eax <= 2u) ? n4_f2u(pj[eax]) : n4_r32(m, esp + eax * 4u + 0x14u);
            if (!D) {
                const float v_ = (float)x1; n4_qsw32(s, m, esi + 0x224u, n4_f2u(v_)); s->pv = n4_f2u(v_);
                n4_qsw32(s, m, esi + 0x220u, ecx); s->pu = ecx;
            } else { n4_wf(m, esi + 0x224u, x1); n4_w32(m, esi + 0x220u, ecx); }
            edx = H32(hr, rrec, 4u, 8);
            ecx = esi;
            SPUSH(&k, 0x880E6u);
            N4_SAVE_ALL();
            n4_87e10(s, R);
            N4_LOAD_ALL();
        }
    L_880E6:
        esp = E - 0x20u;
        eax = CR(s10, n4_r32(m, E - 0x10u));
        {
            const uint8_t *hl = n4_span(m, eax, 8);
            edx = (uint32_t)(int32_t)(int16_t)H16(hl, eax, 2u, 8);
            ecx = H32(hl, eax, 4u, 8);
        }
        INCDEC_CF(); ebp = ebp + 1u;
        edx = edx + ecx;
        SETF(XK_SUB, ebp, edx, ebp - edx, 32);
        if (SF() != OF()) { be++; continue; }
        break;
    }
L_880FC:
    edi = CR(sv_edi, n4_r32(m, E - 0x20u));
    esi = CR(sv_esi, n4_r32(m, E - 0x1Cu));
    ebp = CR(sv_ebp, n4_r32(m, E - 0x18u));
    ebx = CR(sv_ebx, n4_r32(m, E - 0x14u));
    esp = E + 4u;
    N4_SAVE_ALL();
}

/* ---- f_00088110: esp -> [ret][center pointer][radius]; eax = BSP, cx = capacity, edx = the tested-surface bits,
 * esi = the result lists. ret 8. The caller checked the layout (n4_layout) and set the query constants. -------- */
static void n4_88110(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    const n4_stk k = s->kq; uint8_t *const hf = s->hq;
    esp -= 0x228u;
    FSW16(esp + 4u, (uint16_t)ecx);
    ecx = n4_r32(m, E + 8u);
    const uint32_t sv_edi = edi;
    SPUSH(k, edi);
    edi = 0;
    FSW32(esp + 4u, eax);
    eax = n4_r32(m, E + 4u);
    FSW32(esp + 0xCu, edx);
    FSW32(esp + 0x14u, ecx);
    edx = 0;
    ecx = esp + 4u;
    FSW32(esp + 0x10u, eax);
    FSW32(esp + 0x18u, esi);
    FSW32(esp + 0x1Cu, edi);
    n4_lw32(s, m, &D, esi + 0xC0Cu, edi);
    n4_lw32(s, m, &D, esi, edi);
    n4_lw32(s, m, &D, esi + 0x404u, edi);
    n4_lw32(s, m, &D, esi + 0x808u, edi);
    SPUSH(k, 0x88163u);
    N4_SAVE_ALL();
    n4_87ea0(s, R);
    N4_LOAD_ALL();
    { uint32_t a_ = n4_lr32(s, m, esi); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
    if (!ZF() && SF() == OF()) goto hit;
    { uint32_t a_ = n4_lr32(s, m, esi + 0x404u); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
    if (!ZF() && SF() == OF()) goto hit;
    eax = 0;
    goto done;
hit:
    eax = 1;
done:
    edi = CR(sv_edi, n4_r32(m, E - 0x22Cu));
    esp = E + 12u;
    N4_SAVE_ALL();
}
/* ---- modes, budget, counters ------------------------------------------------------------------------------ */
/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go at the end (xk_native_visibility.c). */
static inline void n4_budget(xctx *c, uint32_t backedges)
{
    if (c->preempt > 0 && (uint32_t)c->preempt > backedges) { c->preempt -= (int32_t)backedges; return; }   /* no refill */
    while (backedges) {
        int32_t n = c->preempt >= 1 ? c->preempt : 1;
        if ((uint32_t)n > backedges) { c->preempt -= (int32_t)backedges; return; }
        backedges -= (uint32_t)n; c->preempt -= n;
        xv_preempt(c);
    }
}
#ifdef __vita__
static inline uint64_t n4_ns(void) { return xk_os_monotonic_us() * 1000u; }
#else
#include <time.h>
static inline uint64_t n4_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
#endif

enum { N4_CALLS, N4_VERIFIED, N4_MISMATCHED, N4_DECLINED, N4_JOURNAL_FAIL, N4_NODES3, N4_NODES2, N4_LEAVES, N4_SURFACES,
       N4_EDGES, N4_VERTICES, N4_BACKEDGES, N4_SCANS, N4_SCANNED, N4_DIRTY, N4_LAYOUT, N4_TIMED_NATIVE, N4_TIMED_GUEST, N4_NAN_WORDS, N4_COUNTERS };
static unsigned n4_counter[N4_COUNTERS], n4_mismatch_total;
static uint64_t n4_native_ns, n4_guest_ns;
#define N4_ADD(i, v) __atomic_fetch_add(&n4_counter[i], (unsigned)(v), __ATOMIC_RELAXED)

static int n4_mode_value = -1, n4_detail;
static int n4_mode(void)
{
    int mode = __atomic_load_n(&n4_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_4B9D0"); mode = e ? atoi(e) : XV_NATIVE_4B9D0_DEFAULT;
        { const char *t = getenv("XV_NATIVE_4B9D0_TIME"); n4_detail = mode == 1 || (t && atoi(t) != 0); }
        if (mode < 0 || mode > 2) mode = 0;
        int expected = -1;
        if (__atomic_compare_exchange_n(&n4_mode_value, &expected, mode, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            XK_LOG("[native-4b9d0] f_00088110 BSP sphere query (+87EA0/87E10/86F50/B0CB0) under 172C95: %s\n",
                   mode == 2 ? "native" : mode == 1 ? "verify (native vs fused guest, guest result kept)" : "off");
        else mode = expected;
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_4b9d0_force(int mode) { __atomic_store_n(&n4_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); n4_detail = 1; }
static int n4_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_4B9D0_TIME"); on = e && atoi(e) != 0; }
    return on;
}

/* The layout the native relies on (else the fused guest query runs): esp 4-aligned with 64 KB of room below, the
 * result lists 4-aligned above 88110's frame and its arguments, the BSP header and the image constants it caches
 * outside the stack range and the lists. Then no store of the query can reach a frame but the frame's owner's (the
 * `dirty` exceptions aside), nor the cached header and constants. */
static int n4_layout(const xctx *c)
{
    const uint32_t E = c->r[4], res = c->r[6], bsp = c->r[0];
    if ((E & 3u) || E < 0x20000u || E > 0xFFFFF000u) return 0;
    if ((res & 3u) || res < E + 12u || res > 0xFFFFFFFFu - 0x1010u) return 0;
    const uint32_t lo = E - 0x10000u, hi = E + 12u;
#define N4_OVER(a, n, b, len) ((a) < (b) + (len) && (b) < (a) + (n))
    if (bsp > 0xFFFFFFFFu - 0x60u || N4_OVER(bsp, 0x5Cu, lo, hi - lo) || N4_OVER(bsp, 0x5Cu, res, 0x1010u)) return 0;
    if (N4_OVER(N4_ZERO, 4u, lo, hi - lo) || N4_OVER(N4_ZERO, 4u, res, 0x1010u)) return 0;
    if (N4_OVER(N4_AXES, 0x18u, lo, hi - lo) || N4_OVER(N4_AXES, 0x18u, res, 0x1010u)) return 0;
#undef N4_OVER
    return 1;
}
/* The whole query from the state of `call 00088110h` (esp at the pushed return address) to after its `ret 8`. */
static void n4_query(xctx *c, n4q *s, int journal)
{
    s->m.ram = g_xram; s->m.pt = g_xpt; s->m.jon = journal;
    const n4_mem *m = &s->m;
    s->fk = c->f_kind; s->fa = c->f_op1; s->fb = c->f_op2; s->fr = c->f_res; s->fbits = c->f_bits;
    s->fcfo = c->f_cf_override; s->fcf = c->f_cf; s->fofo = c->f_of_override; s->fof = c->f_of;
    s->fsp0 = c->fsp; s->fsw = c->fsw;
    for (unsigned d = 0; d < 8; ++d) s->sl[d] = c->st[(s->fsp0 - d) & 7u];
    s->xmm = 0; s->be = 0; s->dirty = 0;
    s->nodes3n = s->nodes2n = s->leavesn = s->surfacesn = s->edgesn = s->vertsn = s->scans = s->scanned = 0;
    /* what 88110 stores into its frame q (read here from the registers and arguments it copies) */
    const uint32_t E = c->r[4];
    s->E88 = E; s->q = E - 0x228u; s->low = E - 0x10000u;
    s->bsp = c->r[0]; s->cap = (uint16_t)c->r[1]; s->bits = c->r[2]; s->res = c->r[6];
    s->ctr = n4_r32(m, E + 4u);
    { const uint32_t r = n4_r32(m, E + 8u); s->radius = n4_u2f(r); }
    s->anc = 0;
    n4_stk_at(m, &s->kq, E - 0x230u);
    s->hq = ((E - 0x230u) >> 12) == ((E + 11u) >> 12) ? s->kq.h0 + ((E - 0x230u) & 0xFFFu) + 0x230u : NULL;
    s->axis = n4_r16(m, s->q + 0x21Cu); s->side = n4_r8(m, s->q + 0x21Eu);
    s->pu = n4_r32(m, s->q + 0x220u); s->pv = n4_r32(m, s->q + 0x224u);
    const uint32_t b = s->bsp;
    s->nodes3 = n4_r32(m, b + 4u); s->planes = n4_r32(m, b + 0x10u); s->leaves = n4_r32(m, b + 0x1Cu); s->refs = n4_r32(m, b + 0x28u);
    s->nodes2 = n4_r32(m, b + 0x34u); s->surfs = n4_r32(m, b + 0x40u); s->edges = n4_r32(m, b + 0x4Cu); s->verts = n4_r32(m, b + 0x58u);
    s->zero = n4_rf(m, N4_ZERO);
    for (unsigned i = 0; i < 12; ++i) s->axes[i] = (int16_t)n4_r16(m, N4_AXES + 2u * i);
    s->hctr = n4_span(m, s->ctr, 12);
    s->rp0 = s->res >> 12;
    for (unsigned i = 0; i < 3; ++i) s->rb[i] = m->ram + m->pt[(s->rp0 + i) & 0xFFFFFu];
    n4r R = { c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7] };
    n4_88110(s, &R);
    c->r[0] = R.eax; c->r[1] = R.ecx; c->r[2] = R.edx; c->r[3] = R.ebx; c->r[4] = R.esp; c->r[5] = R.ebp; c->r[6] = R.esi; c->r[7] = R.edi;
    c->f_kind = s->fk; c->f_op1 = s->fa; c->f_op2 = s->fb; c->f_res = s->fr; c->f_bits = s->fbits;
    c->f_cf_override = s->fcfo; c->f_cf = s->fcf; c->f_of_override = s->fofo; c->f_of = s->fof;
    c->fsw = s->fsw;
    for (unsigned d = 1; d <= 5; ++d) c->st[(s->fsp0 - d) & 7u] = s->sl[d];
    if (s->xmm) {
        memcpy(c->xmm[0], s->xmm0, sizeof s->xmm0); memcpy(c->xmm[1], s->xmm1, sizeof s->xmm1); c->xmm[2][0] = s->xmm2_0;
    }
}
static void n4_count(const n4q *s)
{
    N4_ADD(N4_CALLS, 1);
    if (!n4_detail) return;
    N4_ADD(N4_NODES3, s->nodes3n); N4_ADD(N4_NODES2, s->nodes2n); N4_ADD(N4_LEAVES, s->leavesn);
    N4_ADD(N4_SURFACES, s->surfacesn); N4_ADD(N4_EDGES, s->edgesn); N4_ADD(N4_VERTICES, s->vertsn); N4_ADD(N4_BACKEDGES, s->be);
    N4_ADD(N4_SCANS, s->scans); N4_ADD(N4_SCANNED, s->scanned); N4_ADD(N4_DIRTY, s->dirty ? 1 : 0);
}

/* ---- verify mode ------------------------------------------------------------------------------------------- */
enum { N4_REGIONS = 4, N4_REGION_CAP = 0x40000u };
static int n4_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
static int n4_same_float(float a, float b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
/* A float NaN in both results: which NaN payload an operation propagates is the host compiler's operand order
 * (the guest bodies built -O0/-O2 already differ); such floats only reach the dead stack frame and x87 slots. */
static inline int n4_nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static void n4_report_mismatch(const char *what, uint32_t a, uint32_t b, const n4q *s)
{
    if (__atomic_add_fetch(&n4_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-4b9d0] MISMATCH %s native %08X guest %08X (query: %u nodes, %u leaves, %u surfaces, %u edges)\n",
               what, a, b, s->nodes3n, s->leavesn, s->surfacesn, s->edgesn);
}

void query_fused_172c95_171f94(xctx *c);   /* recomp/query_fusion.c: the fused guest query (88110 subtree) */
int xv_scene_thread_on_helper(void) __attribute__((weak));

static void n4_verify(xctx *c, int timed)
{
    const xctx before = *c;
    n4q s;
    n4_j.n = 0; n4_j.overflow = 0;
    const uint64_t t0 = timed ? n4_ns() : 0;
    n4_query(c, &s, 1);
    if (timed) { __atomic_fetch_add(&n4_native_ns, n4_ns() - t0, __ATOMIC_RELAXED); N4_ADD(N4_TIMED_NATIVE, 1); }
    const xctx native = *c;
    /* regions the guest may write: the stack from below the deepest native write to the caller's arguments, and the
     * result lists (esi at entry, 0x1010 bytes). Everything the native wrote is journaled too. */
    uint32_t low = before.r[4] - 0x230u;
    for (unsigned i = 0; i < n4_j.n; ++i) {
        const uint32_t a = n4_j.e[i].addr;
        if (a < before.r[4] + 0x10u && before.r[4] - a < 0x100000u && a < low) low = a;
    }
    uint32_t reg_addr[N4_REGIONS], reg_len[N4_REGIONS]; unsigned nreg = 0;
    reg_addr[nreg] = low - 0x100u; reg_len[nreg] = before.r[4] + 0x10u - (low - 0x100u);
    if (reg_len[nreg] > N4_REGION_CAP) reg_len[nreg] = N4_REGION_CAP;
    nreg++;
    reg_addr[nreg] = before.r[6]; reg_len[nreg] = 0x1010u; nreg++;
    uint32_t total = 0;
    for (unsigned i = 0; i < nreg; ++i) total += reg_len[i];
    uint8_t *img = malloc(total), *cur = malloc(total);
    const unsigned nj = n4_j.n;
    n4_jent *j = malloc((nj ? nj : 1) * sizeof *j);
    if (!img || !cur || !j || n4_j.overflow) {
        /* cannot verify this call: keep the native result (it is complete), account for its budget */
        N4_ADD(N4_JOURNAL_FAIL, 1);
        free(img); free(cur); free(j);
        n4_budget(c, s.be); n4_count(&s);
        return;
    }
    for (unsigned i = 0, o = 0; i < nreg; o += reg_len[i], ++i) x_guest_read_pages(img + o, reg_addr[i], reg_len[i]);
    const n4_mem *m = &s.m;
    for (unsigned i = 0; i < nj; ++i) {
        j[i] = n4_j.e[i]; memcpy(j[i].now, N4_P(j[i].addr), j[i].size);
        x_guest_read_pages(&j[i].word, j[i].addr & ~3u, 4);
    }
    for (unsigned i = nj; i-- > 0;) memcpy(N4_P(j[i].addr), j[i].old, j[i].size);
    n4_count(&s);
    /* the guest on the same state, unbounded budget */
    *c = before; c->preempt = 1 << 30;
    const uint64_t t1 = timed ? n4_ns() : 0;
    query_fused_172c95_171f94(c);
    if (timed) { __atomic_fetch_add(&n4_guest_ns, n4_ns() - t1, __ATOMIC_RELAXED); N4_ADD(N4_TIMED_GUEST, 1); }
    const uint32_t guest_backedges = (uint32_t)((1 << 30) - c->preempt);
    unsigned bad = 0, nan_words = 0;
    const xctx *n = &native;
#define N4_CMP(what, a, b) do { if ((a) != (b)) { bad++; n4_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &s); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) N4_CMP(rn[i], n->r[i], c->r[i]);
    N4_CMP("backedges", s.be, guest_backedges);
    N4_CMP("fsp", n->fsp, c->fsp); N4_CMP("fcw", n->fcw, c->fcw); N4_CMP("fsw", n->fsw, c->fsw); N4_CMP("df", n->df, c->df);
    N4_CMP("f_kind", n->f_kind, c->f_kind); N4_CMP("f_op1", n->f_op1, c->f_op1); N4_CMP("f_op2", n->f_op2, c->f_op2);
    N4_CMP("f_res", n->f_res, c->f_res); N4_CMP("f_bits", n->f_bits, c->f_bits);
    N4_CMP("f_cf_override", n->f_cf_override, c->f_cf_override); N4_CMP("f_of_override", n->f_of_override, c->f_of_override);
    N4_CMP("f_cf", n->f_cf, c->f_cf); N4_CMP("f_of", n->f_of, c->f_of);
    N4_CMP("fs_base", n->fs_base, c->fs_base); N4_CMP("scratch", n->scratch, c->scratch); N4_CMP("eip_hint", n->eip_hint, c->eip_hint);
    for (unsigned i = 0; i < 8; ++i)
        if (!n4_same_double(n->st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &n->st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st[%u] fsp %u (lo word)", i, c->fsp); bad++; n4_report_mismatch(w, (uint32_t)a, (uint32_t)b, &s);
        }
    for (unsigned r = 0; r < 8; ++r)
        for (unsigned l = 0; l < 4; ++l)
            if (!n4_same_float(n->xmm[r][l], c->xmm[r][l])) {
                uint32_t a, b; memcpy(&a, &n->xmm[r][l], 4); memcpy(&b, &c->xmm[r][l], 4);
                char w[40]; snprintf(w, sizeof w, "xmm%u[%u]", r, l); bad++; n4_report_mismatch(w, a, b, &s);
            }
    if (memcmp(n->mm, c->mm, sizeof c->mm)) { bad++; n4_report_mismatch("mm", 0, 1, &s); }
    /* every byte the native wrote holds the same final value after the guest (NaN words: any payload) */
    for (unsigned i = 0; i < nj; ++i) {
        uint8_t now[4]; memcpy(now, N4_P(j[i].addr), j[i].size);
        if (memcmp(now, j[i].now, j[i].size)) {
            uint32_t gw; x_guest_read_pages(&gw, j[i].addr & ~3u, 4);
            if (n4_nan32(gw) && n4_nan32(j[i].word)) { nan_words++; continue; }
            uint32_t a = 0, b = 0; memcpy(&a, j[i].now, j[i].size); memcpy(&b, now, j[i].size);
            char w[40]; snprintf(w, sizeof w, "write@%08X", j[i].addr); bad++; n4_report_mismatch(w, a, b, &s);
            break;
        }
    }
    /* and the guest wrote nothing else in the regions it may write */
    for (unsigned i = 0, o = 0; i < nreg; o += reg_len[i], ++i) {
        x_guest_read_pages(cur + o, reg_addr[i], reg_len[i]);
        if (!memcmp(cur + o, img + o, reg_len[i])) continue;
        for (uint32_t k = 0; k < reg_len[i]; ++k) {
            if (cur[o + k] == img[o + k]) continue;
            const uint32_t a = reg_addr[i] + k, w0 = (a & ~3u) - reg_addr[i];
            if ((a & ~3u) >= reg_addr[i] && w0 + 4u <= reg_len[i]) {
                uint32_t gw, nw; memcpy(&gw, cur + o + w0, 4); memcpy(&nw, img + o + w0, 4);
                if (n4_nan32(gw) && n4_nan32(nw)) { nan_words++; k = w0 + 3u; continue; }
            }
            char w[48]; snprintf(w, sizeof w, "region%u@%08X (byte)", i, a);
            bad++; n4_report_mismatch(w, img[o + k], cur[o + k], &s); break;
        }
    }
#undef N4_CMP
    c->preempt = before.preempt;
    n4_budget(c, guest_backedges);
    N4_ADD(N4_VERIFIED, 1); N4_ADD(N4_NAN_WORDS, nan_words);
    if (bad) N4_ADD(N4_MISMATCHED, 1);
    free(img); free(cur); free(j);
}

#if !defined(__vita__) && !defined(XV_NATIVE_4B9D0_TEST)
/* Host harness only: XV_NATIVE_4B9D0_CAPTURE=<file>[:count] writes the guest arena and page table once, then count
 * (default 2000) query entry states the native accepts (xctx + the guest stack from esp - 64 KB to esp + 64 KB), for
 * tools/tests/native_4b9d0.c --replay (realistic speed and exactness offline). */
extern uint32_t xk_mem_arena_size(void);
static char n4_capture_path[512];
static void n4_capture(const xctx *c)
{
    static int state = -1; static FILE *f; static unsigned n, max, skip;
    if (state == 0) return;
    if (state < 0) {
        const char *e = getenv("XV_NATIVE_4B9D0_CAPTURE"); state = 0;
        if (!e) return;
        char path[512]; snprintf(path, sizeof path, "%s", e);   /* <file>[:count[:skip]] */
        max = 2000; skip = 0;
        char *c1 = strchr(path, ':');
        if (c1) { *c1 = 0; max = (unsigned)atoi(c1 + 1); char *c2 = strchr(c1 + 1, ':'); if (c2) skip = (unsigned)atoi(c2 + 1); }
        state = 2;
        snprintf(n4_capture_path, sizeof n4_capture_path, "%s", path);
    }
    if (state == 2) {
        if (skip) { skip--; return; }
        if (!(f = fopen(n4_capture_path, "wb"))) { state = 0; return; }
        state = 1;
        const uint32_t hdr[4] = { 0x3444344Eu, xk_mem_arena_size(), 1u << 20, 0x20000u };
        fwrite(hdr, sizeof hdr, 1, f); fwrite(g_xpt, 4, 1u << 20, f); fwrite(g_xram, 1, hdr[1], f);
        XK_LOG("[native-4b9d0] capture: arena %u bytes to %s, %u queries\n", hdr[1], n4_capture_path, max);
    }
    /* a portable record (the harness and the replay may differ in pointer width) */
    struct { uint32_t r[8], fl[9], fsp, fsw, fcw, df; int32_t preempt; double st[8]; float xmm[8][4]; uint64_t mm[8]; } rec;
    memcpy(rec.r, c->r, sizeof rec.r);
    rec.fl[0] = c->f_kind; rec.fl[1] = c->f_op1; rec.fl[2] = c->f_op2; rec.fl[3] = c->f_res; rec.fl[4] = c->f_bits;
    rec.fl[5] = c->f_cf_override; rec.fl[6] = c->f_cf; rec.fl[7] = c->f_of_override; rec.fl[8] = c->f_of;
    rec.fsp = c->fsp; rec.fsw = c->fsw; rec.fcw = c->fcw; rec.df = c->df; rec.preempt = c->preempt;
    memcpy(rec.st, c->st, sizeof rec.st); memcpy(rec.xmm, c->xmm, sizeof rec.xmm); memcpy(rec.mm, c->mm, sizeof rec.mm);
    static uint8_t win[0x20000];
    x_guest_read_pages(win, c->r[4] - 0x10000u, sizeof win);
    fwrite(&rec, sizeof rec, 1, f); fwrite(win, 1, sizeof win, f);
    if (++n == max) { fclose(f); f = NULL; state = 0; XK_LOG("[native-4b9d0] capture: %u queries written\n", n); }
}
#endif

/* The hook: in place of query_fused_172c95_171f94(c) (recomp/kernel/xk_query_reuse.c). */
void xv_native_4b9d0_query(xctx *c)
{
    const int mode = n4_mode();
    const int timed = n4_timing();
#if !defined(__vita__) && !defined(XV_NATIVE_4B9D0_TEST)
    if (X_PT == g_xpt && !(xv_scene_thread_on_helper && xv_scene_thread_on_helper()) && n4_layout(c)) n4_capture(c);
#endif
    if (!mode) {
        if (!timed) { query_fused_172c95_171f94(c); return; }
        const uint64_t t0 = n4_ns();
        query_fused_172c95_171f94(c);
        __atomic_fetch_add(&n4_guest_ns, n4_ns() - t0, __ATOMIC_RELAXED); N4_ADD(N4_TIMED_GUEST, 1);
        return;
    }
    /* The fused guest code translates integer accesses through the live table and float accesses through the
     * thread's: the native (one table) only where the two are the same. The query is tick work anyway. */
    if ((xv_scene_thread_on_helper && xv_scene_thread_on_helper()) || X_PT != g_xpt || !n4_layout(c)) {
        N4_ADD(N4_DECLINED, 1);
        if (X_PT == g_xpt && !(xv_scene_thread_on_helper && xv_scene_thread_on_helper())) N4_ADD(N4_LAYOUT, 1);
        query_fused_172c95_171f94(c);
        return;
    }
    if (mode == 1) { n4_verify(c, timed); return; }
    n4q s;
    const uint64_t t0 = timed ? n4_ns() : 0;
    n4_query(c, &s, 0);
    if (timed) { __atomic_fetch_add(&n4_native_ns, n4_ns() - t0, __ATOMIC_RELAXED); N4_ADD(N4_TIMED_NATIVE, 1); }
    n4_budget(c, s.be); n4_count(&s);
}

void xv_native_4b9d0_report(unsigned frames)
{
    unsigned n[N4_COUNTERS];
    for (unsigned i = 0; i < N4_COUNTERS; ++i) n[i] = __atomic_exchange_n(&n4_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t native_ns = __atomic_exchange_n(&n4_native_ns, 0, __ATOMIC_RELAXED), guest_ns = __atomic_exchange_n(&n4_guest_ns, 0, __ATOMIC_RELAXED);
    if (!n[N4_CALLS] && !n[N4_TIMED_GUEST] && !n[N4_DECLINED]) return;
    char timing[112] = "";
    if (n[N4_TIMED_NATIVE])
        snprintf(timing, sizeof timing, "; us/call native %.3f", (double)native_ns / 1000.0 / n[N4_TIMED_NATIVE]);
    if (n[N4_TIMED_GUEST])
        snprintf(timing + strlen(timing), sizeof timing - strlen(timing), "%s guest %.3f (%u timed)", n[N4_TIMED_NATIVE] ? "" : "; us/call",
                 (double)guest_ns / 1000.0 / n[N4_TIMED_GUEST], n[N4_TIMED_GUEST]);
    XK_LOG("[native-4b9d0] %u frames: calls %u verified %u mismatched %u (total mismatches %u) declined %u journal-fail %u; "
           "nodes %u 2d-nodes %u leaves %u surfaces %u edges %u vertices %u back-edges %u scans %u scanned %u dirty %u layout-declined %u nan-words %u%s\n",
           frames, n[N4_CALLS], n[N4_VERIFIED], n[N4_MISMATCHED], __atomic_load_n(&n4_mismatch_total, __ATOMIC_RELAXED),
           n[N4_DECLINED], n[N4_JOURNAL_FAIL], n[N4_NODES3], n[N4_NODES2], n[N4_LEAVES], n[N4_SURFACES], n[N4_EDGES],
           n[N4_VERTICES], n[N4_BACKEDGES], n[N4_SCANS], n[N4_SCANNED], n[N4_DIRTY], n[N4_LAYOUT], n[N4_NAN_WORDS], timing);
}
