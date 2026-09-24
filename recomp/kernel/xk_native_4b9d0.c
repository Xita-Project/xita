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
typedef struct {
    n4_mem m;
    uint32_t fk, fa, fb, fr, fbits, fcfo, fcf, fofo, fof;   /* the lazy-flag record, as xctx holds it */
    uint32_t fsp0;
    uint16_t fsw;
    double sl[8];              /* sl[d]: x87 slot st[(fsp0 - d) & 7] (d = 1..5 written by the subtree) */
    float xmm0[4], xmm1[4], xmm2_0;
    int xmm;                   /* the vertex pass ran: xmm0, xmm1 and xmm2[0] are the subtree's */
    uint32_t be;               /* back-edges taken (X_PREEMPT sites) */
    unsigned nodes3, nodes2, leaves, surfaces, edges, vertices;
} n4q;
typedef struct { uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; } n4r;

/* The lazy-flag record in locals (one set per function; saved around calls, which read and write it). */
#define N4_FL_LOCALS uint32_t fk = s->fk, fa = s->fa, fb = s->fb, fr = s->fr, fbits = s->fbits, fcfo = s->fcfo, fcf = s->fcf, fofo = s->fofo, fof = s->fof
#define N4_FL_SAVE() (s->fk = fk, s->fa = fa, s->fb = fb, s->fr = fr, s->fbits = fbits, s->fcfo = fcfo, s->fcf = fcf, s->fofo = fofo, s->fof = fof)
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
#define N4_LOAD_ALL() (eax = R->eax, ecx = R->ecx, edx = R->edx, ebx = R->ebx, esp = R->esp, ebp = R->ebp, esi = R->esi, edi = R->edi, \
                       N4_FL_LOAD(), N4_X87_LOAD(), be = s->be)
#define N4_LOCALS n4_mem mm_ = s->m; const n4_mem *const m = &mm_; N4_REGS_IN; N4_FL_LOCALS; N4_X87_LOCALS; uint32_t be = s->be
#define PUSH(v) do { uint32_t v__ = (v); esp -= 4u; n4_w32(m, esp, v__); } while (0)
#define POP() ({ uint32_t v__ = n4_r32(m, esp); esp += 4u; v__; })
#define LO8(r, v) ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))
#define LO16(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))

/* ---- f_000B0CB0: segment vs sphere. esp -> [ret][radius]; eax = center, ecx = start, edx = direction -------- */
static inline __attribute__((always_inline)) void n4_b0cb0(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    esp -= 0x10u;
    x1 = n4_rf(m, ecx); x1 = x1 - n4_rf(m, eax);
    x2 = n4_rf(m, ecx + 4u); x2 = x2 - n4_rf(m, eax + 4u);
    x3 = n4_rf(m, ecx + 8u); x3 = x3 - n4_rf(m, eax + 8u);
    x4 = x3; x4 = x4 * x3;                                   /* fld st(0); fmul st,st(1) */
    x5 = x2; x5 = x5 * x2; x4 = x4 + x5;                     /* fld st(2); fmul st,st(3); faddp */
    x5 = x1; x5 = x5 * x1; x4 = x4 + x5;                     /* fld st(3); fmul st,st(4); faddp */
    x5 = n4_rf(m, esp + 0x14u); x5 = x5 * n4_rf(m, esp + 0x14u);
    x4 = x4 - x5;                                            /* fsubp: |start - center|^2 - r^2 */
    n4_wf(m, esp, x4);                                       /* fst [esp] */
    FCMP(x4, n4_rf(m, N4_ZERO), 4);                          /* fcomp: depth 3 after */
    FNSTSW(); TEST_AH(5);
    if (!PF()) {                                             /* inside the sphere at the start: fstp x3 */
        LO8(eax, 1);
        esp += 0x10u; esp += 8u;
        goto out;
    }
    /* B0CFC */
    eax = n4_r32(m, edx); ecx = n4_r32(m, edx + 4u); edx = n4_r32(m, edx + 8u);
    n4_w32(m, esp + 0xCu, edx);
    x4 = n4_rf(m, esp + 0xCu); x4 = x4 * x3;
    n4_w32(m, esp + 8u, ecx);
    x5 = n4_rf(m, esp + 8u);
    n4_w32(m, esp + 4u, eax);
    x5 = x5 * x2; x4 = x4 + x5;
    x5 = n4_rf(m, esp + 4u); x5 = x5 * x1; x4 = x4 + x5;
    n4_wf(m, esp + 0x14u, x4);                               /* fstp [esp+14h]: the radius argument's word; depth 3, then 0 */
    x1 = n4_rf(m, esp + 0x14u);
    FCMP(x1, n4_rf(m, N4_ZERO), 1);
    FNSTSW(); TEST_AH(1);
    if (ZF()) { LO8(eax, 0); esp += 0x10u; esp += 8u; goto out; }   /* B0D80: moving away (xor al,al: no flags) */
    /* B0D41 */
    x1 = n4_rf(m, esp + 0xCu); x1 = x1 * n4_rf(m, esp + 0xCu);
    x2 = n4_rf(m, esp + 8u); x2 = x2 * n4_rf(m, esp + 8u); x1 = x1 + x2;
    x2 = n4_rf(m, esp + 4u); x2 = x2 * n4_rf(m, esp + 4u); x1 = x1 + x2;
    x2 = n4_rf(m, esp + 0x14u); x2 = x2 * n4_rf(m, esp + 0x14u);
    x3 = x1; x3 = x3 * n4_rf(m, esp);
    x2 = x2 - x3;
    n4_wf(m, esp, x2);                                       /* fst [esp]: the discriminant */
    FCMP(x2, n4_rf(m, N4_ZERO), 2);
    FNSTSW(); TEST_AH(0x41);
    if (!PF()) { LO8(eax, 0); esp += 0x10u; esp += 8u; goto out; }   /* B0D7E: no root */
    /* B0D88 (depth 1) */
    x1 = -x1;
    x1 = x1 - n4_rf(m, esp + 0x14u);
    FCMP(x1, n4_rf(m, N4_ZERO), 1);
    FNSTSW(); TEST_AH(5);
    if (!PF()) { be++; LO8(eax, 1); esp += 0x10u; esp += 8u; goto out; }   /* jnp B0CF2 (a back-edge): fstp; al = 1 */
    /* B0D9F */
    x2 = x1; x2 = x2 * x1;
    FCMP(x2, n4_rf(m, esp), 2);
    FNSTSW();
    TEST_AH(5);
    if (PF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }  /* B0DBB: xor eax,eax */
    else eax = 1;
    esp += 0x10u; esp += 8u;
    (void)E;
out:
    N4_SAVE_ALL();
}

/* ---- f_00086F50: surface test. esp -> [ret][q][surface index]; ret 8 --------------------------------------- */
static __attribute__((noinline)) void n4_86f50(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    const uint32_t E = esp;
    s->surfaces++;
    esp -= 0x2Cu;
    edx = n4_r32(m, esp + 0x34u);
    PUSH(ebx); PUSH(ebp); PUSH(esi);
    esi = n4_r32(m, esp + 0x3Cu);
    eax = n4_r32(m, esi);
    eax = n4_r32(m, eax + 0x40u);
    ecx = edx + edx * 2u;
    ebp = eax + ecx * 4u;
    { uint8_t r_ = n4_r8(m, ebp + 8u) & 8u; SETF(XK_LOGIC, 0, 0, r_, 8); }
    PUSH(edi);
    n4_w32(m, esp + 0x20u, ebp);
    if (!ZF()) {
        /* 86F74: the surface's tested bit */
        ecx = n4_r8(m, ebp + 9u);
        eax = (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 4u);
        SETF(XK_SUB, ecx, eax, ecx - eax, 32);
        if (!(SF() == OF())) {
            edi = n4_r32(m, esi + 8u);
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
    x1 = n4_rf(m, esi + 0x10u);
    eax = n4_r32(m, ebp + 4u);
    x2 = x1; x2 = x2 * x1;
    ecx = esp + 0x14u;
    n4_w8(m, esp + 0x13u, 0);
    n4_w32(m, esp + 0x30u, ecx);
    n4_wf(m, esp + 0x34u, x2);
    goto L_86FC0;
L_86FBA:
    edx = n4_r32(m, esp + 0x44u);
L_86FC0:
    s->vertices++;
    ecx = n4_r32(m, esi);
    edi = n4_r32(m, ecx + 0x4Cu);
    eax = eax + eax * 2u;
    edi = edi + eax * 8u;
    { uint32_t a_ = n4_r32(m, edi + 0x14u); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
    LO8(eax, ZF());
    ebx = (uint8_t)eax;
    edx = n4_r32(m, edi + ebx * 4u);
    eax = edx;
    eax = SHL32(eax, 4u);
    eax = eax + n4_r32(m, ecx + 0x58u);
    ecx = n4_r32(m, esi + 0xCu);
    n4_w32(m, esp + 0x38u, ebx);
    n4_w32(m, esp + 0x2Cu, eax);
    n4_w32(m, esp + 0x28u, ecx);
    eax = n4_r32(m, esp + 0x30u);
    ecx = n4_r32(m, esp + 0x2Cu);
    {
        float a0 = n4_rmf(m, ecx), a2 = n4_rmf(m, ecx + 4u), a3 = n4_rmf(m, ecx + 4u + 4u);
        ecx = n4_r32(m, esp + 0x28u);
        float b0 = n4_rmf(m, ecx), b2 = n4_rmf(m, ecx + 4u), b3 = n4_rmf(m, ecx + 4u + 4u);
        float d0 = a0 - b0, d1 = 0.0f - 0.0f, d2 = a2 - b2, d3 = a3 - b3;   /* subps */
        d0 = d0 * d0; d1 = d1 * d1; d2 = d2 * d2; d3 = d3 * d3;            /* mulps */
        float sum = d0;                                                    /* movss xmm2,xmm0 */
        sum = sum + d2;                                                    /* shufps 0Eh: (d2,d3,d0,d0) */
        sum = sum + d3;                                                    /* shufps 39h: (d3,d0,d0,d2) */
        s->xmm0[0] = d3; s->xmm0[1] = d0; s->xmm0[2] = d0; s->xmm0[3] = d2;
        s->xmm1[0] = b0; s->xmm1[1] = 0.0f; s->xmm1[2] = b2; s->xmm1[3] = b3;
        s->xmm2_0 = sum; s->xmm = 1;
        n4_wmf(m, eax, sum);
    }
    esi = n4_r32(m, esp + 0x40u);
    x1 = n4_rf(m, esp + 0x14u);
    FCMP(x1, n4_rf(m, esp + 0x34u), 1);
    FNSTSW(); TEST_AH(0x41);
    if (PF()) goto L_8708B;
    /* 8703B: the vertex touches the sphere: add it to the vertex list */
    eax = n4_r32(m, esi + 0x14u);
    ebp = n4_r32(m, eax + 0x808u);
    ebx = 0;
    SETF(XK_LOGIC, 0, 0, ebp, 32);
    if (!(ZF() || SF() != OF())) {
        ecx = 0;
        for (;;) {
            { uint32_t a_ = n4_r32(m, eax + ecx * 4u + 0x80Cu); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
            if (ZF()) goto L_8707E;
            INCDEC_CF(); ebx = ebx + 1u;
            ecx = (uint32_t)(int32_t)(int16_t)ebx;
            { uint32_t b_ = n4_r32(m, eax + 0x808u); SETF(XK_SUB, ecx, b_, ecx - b_, 32); }
            if (SF() != OF()) { be++; continue; }
            break;
        }
    }
    SETF(XK_SUB, ebp, 0x100u, ebp - 0x100u, 32);
    if (!(SF() == OF())) {
        n4_w32(m, eax + ebp * 4u + 0x80Cu, edx);
        { INCDEC_CF(); uint32_t v_ = n4_r32(m, eax + 0x808u) + 1u; n4_w32(m, eax + 0x808u, v_); }
        esi = n4_r32(m, esp + 0x40u);
    }
L_8707E:
    ebx = n4_r32(m, esp + 0x38u);
    ebp = n4_r32(m, esp + 0x20u);
    n4_w8(m, esp + 0x13u, 1);
L_8708B:
    eax = n4_r32(m, edi + ebx * 4u + 8u);
    ecx = n4_r32(m, ebp + 4u);
    SETF(XK_SUB, eax, ecx, eax - ecx, 32);
    if (!ZF()) { be++; goto L_86FBA; }
    /* 8709A: edges */
    edi = ecx;
L_870A0:
    s->edges++;
    eax = n4_r32(m, esi);
    ecx = n4_r32(m, eax + 0x4Cu);
    ebx = n4_r32(m, eax + 0x58u);
    edx = edi + edi * 2u;
    ebp = ecx + edx * 8u;
    edx = n4_r32(m, esp + 0x44u);
    { uint32_t a_ = n4_r32(m, ebp + 0x14u); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
    LO8(edx, ZF());
    ecx = (uint8_t)edx;
    n4_w32(m, esp + 0x38u, ecx);
    ecx = n4_r32(m, ebp + ecx * 4u);
    eax = 0;
    ecx = SHL32(ecx, 4u);
    ecx = ecx + ebx;
    { uint8_t r_ = (uint8_t)edx; SETF(XK_LOGIC, 0, 0, r_, 8); }
    LO8(eax, ZF());
    PUSH(ecx);
    edx = esp + 0x18u;
    eax = n4_r32(m, ebp + eax * 4u);
    eax = SHL32(eax, 4u);
    x1 = n4_rf(m, eax + ebx);
    eax = eax + ebx;
    x1 = x1 - n4_rf(m, ecx);
    n4_wf(m, esp + 0x18u, x1);
    x1 = n4_rf(m, eax + 4u); x1 = x1 - n4_rf(m, ecx + 4u);
    n4_wf(m, esp + 0x1Cu, x1);
    x1 = n4_rf(m, eax + 8u);
    eax = n4_r32(m, esi + 0xCu);
    x1 = x1 - n4_rf(m, ecx + 8u);
    n4_wf(m, esp + 0x20u, x1);
    x1 = n4_rf(m, esi + 0x10u);
    n4_wf(m, esp, x1);
    PUSH(0x87108u);
    N4_SAVE_ALL();
    n4_b0cb0(s, R);
    N4_LOAD_ALL();
    { uint8_t r_ = (uint8_t)eax; SETF(XK_LOGIC, 0, 0, r_, 8); }
    if (!ZF()) {
        /* 8710C: the edge touches the sphere: add it to the edge list */
        eax = n4_r32(m, esi + 0x14u);
        ebx = n4_r32(m, eax + 0x404u);
        edx = 0;
        SETF(XK_LOGIC, 0, 0, ebx, 32);
        if (!(ZF() || SF() != OF())) {
            ecx = 0;
            for (;;) {
                { uint32_t a_ = n4_r32(m, eax + ecx * 4u + 0x408u); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
                if (ZF()) goto L_8714E;
                INCDEC_CF(); edx = edx + 1u;
                ecx = (uint32_t)(int32_t)(int16_t)edx;
                { uint32_t b_ = n4_r32(m, eax + 0x404u); SETF(XK_SUB, ecx, b_, ecx - b_, 32); }
                if (SF() != OF()) { be++; continue; }
                break;
            }
        }
        SETF(XK_SUB, ebx, 0x100u, ebx - 0x100u, 32);
        if (!(SF() == OF())) {
            n4_w32(m, eax + ebx * 4u + 0x408u, edi);
            { INCDEC_CF(); uint32_t v_ = n4_r32(m, eax + 0x404u) + 1u; n4_w32(m, eax + 0x404u, v_); }
            esi = n4_r32(m, esp + 0x40u);
        }
    L_8714E:
        n4_w8(m, esp + 0x13u, 1);
    }
    /* 87153 */
    ecx = n4_r32(m, esp + 0x38u);
    edx = n4_r32(m, esp + 0x20u);
    edi = n4_r32(m, ebp + ecx * 4u + 8u);
    { uint32_t b_ = n4_r32(m, edx + 4u); SETF(XK_SUB, edi, b_, edi - b_, 32); }
    if (!ZF()) { be++; goto L_870A0; }
    LO8(eax, n4_r8(m, esp + 0x13u));
    { uint8_t r_ = (uint8_t)eax; SETF(XK_LOGIC, 0, 0, r_, 8); }
    if (!ZF()) goto L_87287;
    /* 87174: point in polygon on the projected axes */
    eax = edx;
    edi = n4_r32(m, eax + 4u);
    eax = n4_r32(m, esi);
    ecx = n4_r32(m, eax + 0x4Cu);
    edx = n4_r32(m, eax + 0x58u);
    LO8(eax, n4_r8(m, esi + 0x21Eu));
    n4_w32(m, esp + 0x34u, ecx);
    ecx = (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 0x21Cu);
    eax = (uint8_t)eax;
    eax = eax + ecx * 2u;
    eax = SHL32(eax, 2u);
    ecx = (uint32_t)(int32_t)(int16_t)n4_r16(m, eax + N4_AXES);
    eax = (uint32_t)(int32_t)(int16_t)n4_r16(m, eax + N4_AXES + 2u);
    ecx = SHL32(ecx, 2u);
    eax = SHL32(eax, 2u);
    n4_w32(m, esp + 0x28u, edi);
    n4_w32(m, esp + 0x38u, edx);
    n4_w32(m, esp + 0x30u, ecx);
    n4_w32(m, esp + 0x2Cu, eax);
    goto L_871C5;
L_871C1:
    edx = n4_r32(m, esp + 0x38u);
L_871C5:
    eax = n4_r32(m, esp + 0x34u);
    ecx = edi + edi * 2u;
    ebp = n4_r32(m, eax + ecx * 8u + 0x14u);
    edi = eax + ecx * 8u;
    ecx = n4_r32(m, esp + 0x44u);
    SETF(XK_SUB, ebp, ecx, ebp - ecx, 32);
    LO8(ecx, ZF());
    ebp = (uint8_t)ecx;
    eax = n4_r32(m, edi + ebp * 4u);
    eax = SHL32(eax, 4u);
    eax = eax + edx;
    ebx = 0;
    { uint8_t r_ = (uint8_t)ecx; SETF(XK_LOGIC, 0, 0, r_, 8); }
    LO8(ebx, ZF());
    ecx = n4_r32(m, edi + ebx * 4u);
    ebx = n4_r8(m, esi + 0x21Eu);
    ecx = SHL32(ecx, 4u);
    ecx = ecx + edx;
    edx = (uint32_t)(int32_t)(int16_t)n4_r16(m, esi + 0x21Cu);
    edx = ebx + edx * 2u;
    edx = SHL32(edx, 2u);
    ebx = (uint32_t)(int32_t)(int16_t)n4_r16(m, edx + N4_AXES);
    edx = (uint32_t)(int32_t)(int16_t)n4_r16(m, edx + N4_AXES + 2u);
    x1 = n4_rf(m, eax + ebx * 4u);
    x2 = n4_rf(m, eax + edx * 4u);
    eax = n4_r32(m, esp + 0x30u);
    x3 = n4_rf(m, eax + ecx);
    edx = n4_r32(m, esp + 0x2Cu);
    n4_wf(m, esp + 0x20u, x3);
    x3 = n4_rf(m, edx + ecx);
    n4_wf(m, esp + 0x24u, x3);
    { double t_ = x2; x2 = x1; x1 = t_; }                    /* fxch */
    x2 = x2 - n4_rf(m, esi + 0x220u);
    n4_wf(m, esp + 0x14u, x2);                               /* fstp: depth 1 */
    x1 = x1 - n4_rf(m, esi + 0x224u);
    x2 = n4_rf(m, esp + 0x20u); x2 = x2 - n4_rf(m, esi + 0x220u);
    x3 = n4_rf(m, esp + 0x24u); x3 = x3 - n4_rf(m, esi + 0x224u);
    x4 = n4_rf(m, esp + 0x14u);
    x4 = x4 * x3;                                            /* fmul st,st(1) */
    { double t_ = x4; x4 = x1; x1 = t_; }                    /* fxch st(3) */
    x4 = x4 * x2;                                            /* fmul st,st(2) */
    x1 = x1 - x4;                                            /* fsubp st(3),st: depth 3 */
    { double t_ = x3; x3 = x1; x1 = t_; }                    /* fxch st(2) */
    FCMP(x3, n4_rf(m, N4_ZERO), 3);                          /* fcomp: depth 2 */
    FNSTSW();
    x1 = x2;                                                 /* fstp st(1): depth 1 */
    TEST_AH(5);
    /* fstp st(0): depth 0 */
    if (!PF()) goto L_872BA;
    edi = n4_r32(m, edi + ebp * 4u + 8u);
    { uint32_t b_ = n4_r32(m, esp + 0x28u); SETF(XK_SUB, edi, b_, edi - b_, 32); }
    if (!ZF()) { be++; goto L_871C1; }
L_87287:
    /* the surface touches the sphere: add it to the surface list */
    esi = n4_r32(m, esi + 0x14u);
    edx = n4_r32(m, esi);
    ecx = 0;
    SETF(XK_LOGIC, 0, 0, edx, 32);
    if (!(ZF() || SF() != OF())) {
        eax = 0;
        for (;;) {
            eax = n4_r32(m, esi + eax * 4u + 4u);
            { uint32_t b_ = n4_r32(m, esp + 0x44u); SETF(XK_SUB, eax, b_, eax - b_, 32); }
            if (ZF()) goto L_872BA;
            edi = n4_r32(m, esi);
            INCDEC_CF(); ecx = ecx + 1u;
            eax = (uint32_t)(int32_t)(int16_t)ecx;
            SETF(XK_SUB, eax, edi, eax - edi, 32);
            if (SF() != OF()) { be++; continue; }
            break;
        }
    }
    SETF(XK_SUB, edx, 0x100u, edx - 0x100u, 32);
    if (!(SF() == OF())) {
        ecx = n4_r32(m, esp + 0x44u);
        n4_w32(m, esi + edx * 4u + 4u, ecx);
        { INCDEC_CF(); uint32_t v_ = n4_r32(m, esi) + 1u; n4_w32(m, esi, v_); }
    }
L_872BA:
    edi = POP(); esi = POP(); ebp = POP(); ebx = POP();
    esp += 0x2Cu;
    esp += 12u;
    (void)E;
    N4_SAVE_ALL();
}

/* ---- f_00087E10: BSP2D traversal. esp -> [ret]; ecx = q, edx = node ------------------------------------------ */
static __attribute__((noinline)) void n4_87e10(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    SETF(XK_LOGIC, 0, 0, edx, 32);
    PUSH(ebx); PUSH(esi); PUSH(edi);
    edi = ecx;
    if (SF()) goto L_87E81;
    for (;;) {
        s->nodes2++;
        eax = n4_r32(m, edi);
        ecx = edx + edx * 4u;
        edx = n4_r32(m, eax + 0x34u);
        x1 = n4_rf(m, edx + ecx * 4u + 4u);
        esi = edx + ecx * 4u;
        x1 = x1 * n4_rf(m, edi + 0x224u);
        x2 = n4_rf(m, edi + 0x220u);
        x2 = x2 * n4_rf(m, esi);
        x1 = x1 + x2;                                        /* faddp */
        x1 = x1 - n4_rf(m, esi + 8u);
        FCMP(x1, n4_rf(m, edi + 0x10u), 1);
        FNSTSW(); TEST_AH(0x41);
        if (PF()) { SETF(XK_LOGIC, (uint8_t)ecx, (uint8_t)ecx, 0, 8); LO8(ecx, 0); }   /* xor cl,cl */
        else LO8(ecx, 1);
        x2 = n4_rf(m, edi + 0x10u);
        x2 = -x2;
        { double t_ = x2; x2 = x1; x1 = t_; }                /* fxch */
        FCMP(x2, x1, 2);                                     /* fcompp */
        FNSTSW(); TEST_AH(1);
        if (!ZF()) { SETF(XK_LOGIC, (uint8_t)ebx, (uint8_t)ebx, 0, 8); LO8(ebx, 0); }   /* xor bl,bl */
        else LO8(ebx, 1);
        { uint8_t r_ = (uint8_t)ecx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        if (!ZF()) {
            edx = n4_r32(m, esi + 0xCu);
            ecx = edi;
            PUSH(0x87E76u);
            N4_SAVE_ALL();
            n4_87e10(s, R);
            N4_LOAD_ALL();
        }
        { uint8_t r_ = (uint8_t)ebx; SETF(XK_LOGIC, 0, 0, r_, 8); }
        if (ZF()) goto L_87E8E;
        edx = n4_r32(m, esi + 0x10u);
        SETF(XK_LOGIC, 0, 0, edx, 32);
        if (!SF()) { be++; continue; }
        break;
    }
L_87E81:
    edx &= 0x7FFFFFFFu;
    PUSH(edx); PUSH(edi);
    PUSH(0x87E8Eu);
    N4_SAVE_ALL();
    n4_86f50(s, R);
    N4_LOAD_ALL();
L_87E8E:
    edi = POP(); esi = POP(); ebx = POP();
    esp += 4u;
    N4_SAVE_ALL();
}

/* ---- f_00087EA0: BSP3D traversal. esp -> [ret]; ecx = q, edx = node ------------------------------------------ */
static __attribute__((noinline)) void n4_87ea0(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    esp -= 0x10u;
    SETF(XK_LOGIC, 0, 0, edx, 32);
    PUSH(ebx); PUSH(ebp); PUSH(esi); PUSH(edi);
    esi = ecx;
    if (SF()) goto L_87F1A;
    x1 = n4_rf(m, esi + 0x10u);
    eax = n4_r32(m, esi);
    ebx = n4_r32(m, eax + 4u);
    x1 = -x1;
    ebp = n4_r32(m, eax + 0x10u);
    n4_wf(m, esp + 0x10u, x1);
    ecx = n4_r32(m, esi + 0xCu);
    for (;;) {                                               /* 87EC1 */
        s->nodes3++;
        eax = edx + edx * 2u;
        edi = ebx + eax * 4u;
        eax = n4_r32(m, edi);
        eax = SHL32(eax, 4u);
        x1 = n4_rf(m, eax + ebp + 8u);
        eax = eax + ebp;
        x1 = x1 * n4_rf(m, ecx + 8u);
        x2 = n4_rf(m, eax + 4u); x2 = x2 * n4_rf(m, ecx + 4u);
        x1 = x1 + x2;
        x2 = n4_rf(m, eax); x2 = x2 * n4_rf(m, ecx);
        x1 = x1 + x2;
        x1 = x1 - n4_rf(m, eax + 0xCu);
        FCMP(x1, n4_rf(m, esi + 0x10u), 1);
        FNSTSW(); TEST_AH(5);
        if (PF()) { SETF(XK_LOGIC, (uint8_t)edx, (uint8_t)edx, 0, 8); LO8(edx, 0); }   /* xor dl,dl */
        else LO8(edx, 1);
        FCMP(x1, n4_rf(m, esp + 0x10u), 1);                  /* fcomp [esp+10h]: depth 0 after */
        FNSTSW(); TEST_AH(0x41);
        if (!ZF()) {                                         /* 87F94: the back side only */
            SETF(XK_LOGIC, (uint8_t)eax, (uint8_t)eax, 0, 8); LO8(eax, 0);
            be++;
        } else {
            { uint8_t r_ = (uint8_t)edx; SETF(XK_LOGIC, 0, 0, r_, 8); }
            LO8(eax, 1);
            if (!ZF()) {                                     /* 87F9B: both sides */
                eax = n4_r32(m, edi);
                ecx = n4_r32(m, esi + 0x18u);
                eax |= 0x80000000u;
                n4_w32(m, esi + ecx * 4u + 0x1Cu, eax);
                { INCDEC_CF(); uint32_t v_ = n4_r32(m, esi + 0x18u) + 1u; n4_w32(m, esi + 0x18u, v_); }
                edx = n4_r32(m, edi + 4u);
                ecx = esi;
                PUSH(0x87FB6u);
                N4_SAVE_ALL();
                n4_87ea0(s, R);
                N4_LOAD_ALL();
                ebp = n4_r32(m, esi + 0x18u);
                INCDEC_CF(); ebp = ebp - 1u;
                n4_w32(m, esi + 0x18u, ebp);
                edx = n4_r32(m, edi);
                edx &= 0x7FFFFFFFu;
                eax = ebp;
                n4_w32(m, esi + eax * 4u + 0x1Cu, edx);
                { INCDEC_CF(); uint32_t v_ = n4_r32(m, esi + 0x18u) + 1u; n4_w32(m, esi + 0x18u, v_); }
                edx = n4_r32(m, edi + 8u);
                ecx = esi;
                PUSH(0x87FD8u);
                N4_SAVE_ALL();
                n4_87ea0(s, R);
                N4_LOAD_ALL();
                eax = n4_r32(m, esi + 0x18u);
                edi = POP();
                INCDEC_CF(); eax = eax - 1u;
                n4_w32(m, esi + 0x18u, eax);
                esi = POP(); ebp = POP(); ebx = POP();
                esp += 0x10u;
                esp += 4u;
                N4_SAVE_ALL();
                return;
            }
        }
        /* 87F0F */
        edx = (uint8_t)eax;
        edx = n4_r32(m, edi + edx * 4u + 4u);
        SETF(XK_LOGIC, 0, 0, edx, 32);
        if (!SF()) { be++; continue; }
        break;
    }
L_87F1A:
    SETF(XK_SUB, edx, 0xFFFFFFFFu, edx + 1u, 32);
    if (ZF()) goto L_880FC;
    /* 87F23: a leaf */
    s->leaves++;
    eax = n4_r32(m, esi);
    ecx = n4_r32(m, eax + 0x1Cu);
    eax = n4_r32(m, esi + 0x14u);
    edx &= 0x7FFFFFFFu;
    edi = ecx + edx * 8u;
    ecx = n4_r32(m, eax + 0xC0Cu);
    SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);
    n4_w32(m, esp + 0x10u, edi);
    if (!(SF() == OF())) {
        n4_w32(m, eax + ecx * 4u + 0xC10u, edx);
        eax = n4_r32(m, esi + 0x14u);
        { INCDEC_CF(); uint32_t v_ = n4_r32(m, eax + 0xC0Cu) + 1u; n4_w32(m, eax + 0xC0Cu, v_); }
    }
    edx = (uint32_t)(int32_t)(int16_t)n4_r16(m, edi + 2u);
    ebp = n4_r32(m, edi + 4u);
    edx = edx + ebp;
    SETF(XK_SUB, ebp, edx, ebp - edx, 32);
    if (SF() == OF()) goto L_880FC;
    for (;;) {                                               /* 87F67: the leaf's BSP2D references */
        edi = n4_r32(m, esi);
        eax = n4_r32(m, edi + 0x28u);
        ebx = eax + ebp * 8u;
        eax = n4_r32(m, esi + 0x18u);
        edx = 0;
        SETF(XK_LOGIC, 0, 0, eax, 32);
        if (!(ZF() || SF() != OF())) {
            ecx = n4_r32(m, ebx);
            eax = 0;
            for (;;) {
                { uint32_t a_ = n4_r32(m, esi + eax * 4u + 0x1Cu); SETF(XK_SUB, a_, ecx, a_ - ecx, 32); }
                if (ZF()) goto L_87FE7;
                INCDEC_CF(); edx = edx + 1u;
                eax = (uint32_t)(int32_t)(int16_t)edx;
                { uint32_t b_ = n4_r32(m, esi + 0x18u); SETF(XK_SUB, eax, b_, eax - b_, 32); }
                if (SF() != OF()) { be++; continue; }
                break;
            }
        }
        goto L_880E6;
    L_87FE7:
        /* the reference's plane is on the ancestor stack: project the center onto it, pick the axes, test in 2D */
        eax = n4_r32(m, edi + 0x10u);
        ecx &= 0x7FFFFFFFu;
        ecx = SHL32(ecx, 4u);
        ecx = ecx + eax;
        eax = n4_r32(m, esi + 0xCu);
        x1 = n4_rf(m, eax + 8u); x1 = x1 * n4_rf(m, ecx + 8u);
        x2 = n4_rf(m, eax + 4u); x2 = x2 * n4_rf(m, ecx + 4u);
        x1 = x1 + x2;
        x2 = n4_rf(m, ecx); x2 = x2 * n4_rf(m, eax);
        x1 = x1 + x2;
        x1 = x1 - n4_rf(m, ecx + 0xCu);
        x1 = -x1;
        x2 = x1; x2 = x2 * n4_rf(m, ecx); x2 = x2 + n4_rf(m, eax);
        n4_wf(m, esp + 0x14u, x2);
        x2 = x1; x2 = x2 * n4_rf(m, ecx + 4u); x2 = x2 + n4_rf(m, eax + 4u);
        n4_wf(m, esp + 0x18u, x2);
        x1 = x1 * n4_rf(m, ecx + 8u); x1 = x1 + n4_rf(m, eax + 8u);
        n4_wf(m, esp + 0x1Cu, x1);
        x1 = fabs(n4_rf(m, ecx));
        x2 = fabs(n4_rf(m, ecx + 4u));
        x3 = fabs(n4_rf(m, ecx + 8u));
        FCMP(x3, x2, 3);                                     /* fcom */
        FNSTSW(); TEST_AH(1);
        if (ZF()) {
            FCMP(x3, x1, 3);                                 /* fcomp st(2): depth 2 */
            FNSTSW(); TEST_AH(1);
            if (ZF()) { eax = 2; goto L_88072; }             /* fstp st(0) x2 */
        }
        /* 8805C: fstp st(0) when coming from the first compare; 8805E: depth 2 */
        FCMP(x2, x1, 2);                                     /* fcomp: depth 1 */
        FNSTSW();
        /* fstp st(0): depth 0 */
        TEST_AH(1);
        if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }
        else eax = 1;
    L_88072:
        edx = (uint32_t)(int32_t)(int16_t)eax;
        n4_w16(m, esi + 0x21Cu, (uint16_t)eax);
        x1 = n4_rf(m, ecx + edx * 4u);
        FCMP(x1, n4_rf(m, N4_ZERO), 1);
        FNSTSW(); TEST_AH(0x41);
        if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }
        else eax = 1;
        ecx = n4_r32(m, ebx);
        ecx &= 0x80000000u;
        ecx = 0u - ecx;                                      /* neg: no flags in the translation */
        { uint32_t cf_ = CF(); uint32_t r_ = ecx - ecx - cf_; SETFC(XK_SBB, ecx, ecx, r_, 32, cf_); ecx = r_; }
        eax = (uint8_t)eax;
        ecx = 0u - ecx;
        SETF(XK_SUB, eax, ecx, eax - ecx, 32);
        LO8(eax, !ZF());
        n4_w8(m, esi + 0x21Eu, (uint8_t)eax);
        ecx = (uint8_t)eax;
        eax = ecx + edx * 2u;
        eax = SHL32(eax, 2u);
        edx = (uint32_t)(int32_t)(int16_t)n4_r16(m, eax + N4_AXES + 2u);
        eax = (uint32_t)(int32_t)(int16_t)n4_r16(m, eax + N4_AXES);
        x1 = n4_rf(m, esp + edx * 4u + 0x14u);
        ecx = n4_r32(m, esp + eax * 4u + 0x14u);
        n4_wf(m, esi + 0x224u, x1);
        n4_w32(m, esi + 0x220u, ecx);
        edx = n4_r32(m, ebx + 4u);
        ecx = esi;
        PUSH(0x880E6u);
        N4_SAVE_ALL();
        n4_87e10(s, R);
        N4_LOAD_ALL();
    L_880E6:
        eax = n4_r32(m, esp + 0x10u);
        edx = (uint32_t)(int32_t)(int16_t)n4_r16(m, eax + 2u);
        ecx = n4_r32(m, eax + 4u);
        INCDEC_CF(); ebp = ebp + 1u;
        edx = edx + ecx;
        SETF(XK_SUB, ebp, edx, ebp - edx, 32);
        if (SF() != OF()) { be++; continue; }
        break;
    }
L_880FC:
    edi = POP(); esi = POP(); ebp = POP(); ebx = POP();
    esp += 0x10u;
    esp += 4u;
    N4_SAVE_ALL();
}

/* ---- f_00088110: esp -> [ret][center pointer][radius]; eax = BSP, cx = capacity, edx = the tested-surface bits,
 * esi = the result lists. ret 8 ------------------------------------------------------------------------------- */
static void n4_88110(n4q *restrict s, n4r *restrict R)
{
    N4_LOCALS;
    esp -= 0x228u;
    n4_w16(m, esp + 4u, (uint16_t)ecx);
    ecx = n4_r32(m, esp + 0x230u);
    PUSH(edi);
    edi = 0;
    n4_w32(m, esp + 4u, eax);
    eax = n4_r32(m, esp + 0x230u);
    n4_w32(m, esp + 0xCu, edx);
    n4_w32(m, esp + 0x14u, ecx);
    edx = 0;
    ecx = esp + 4u;
    n4_w32(m, esp + 0x10u, eax);
    n4_w32(m, esp + 0x18u, esi);
    n4_w32(m, esp + 0x1Cu, edi);
    n4_w32(m, esi + 0xC0Cu, edi);
    n4_w32(m, esi, edi);
    n4_w32(m, esi + 0x404u, edi);
    n4_w32(m, esi + 0x808u, edi);
    PUSH(0x88163u);
    N4_SAVE_ALL();
    n4_87ea0(s, R);
    N4_LOAD_ALL();
    { uint32_t a_ = n4_r32(m, esi); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
    if (!ZF() && SF() == OF()) goto hit;
    { uint32_t a_ = n4_r32(m, esi + 0x404u); SETF(XK_SUB, a_, edi, a_ - edi, 32); }
    if (!ZF() && SF() == OF()) goto hit;
    eax = 0;
    goto done;
hit:
    eax = 1;
done:
    edi = POP();
    esp += 0x228u;
    esp += 12u;
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
       N4_EDGES, N4_VERTICES, N4_BACKEDGES, N4_TIMED_NATIVE, N4_TIMED_GUEST, N4_NAN_WORDS, N4_COUNTERS };
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

/* The whole query from the state of `call 00088110h` (esp at the pushed return address) to after its `ret 8`. */
static void n4_query(xctx *c, n4q *s, int journal)
{
    s->m.ram = g_xram; s->m.pt = g_xpt; s->m.jon = journal;
    s->fk = c->f_kind; s->fa = c->f_op1; s->fb = c->f_op2; s->fr = c->f_res; s->fbits = c->f_bits;
    s->fcfo = c->f_cf_override; s->fcf = c->f_cf; s->fofo = c->f_of_override; s->fof = c->f_of;
    s->fsp0 = c->fsp; s->fsw = c->fsw;
    for (unsigned d = 0; d < 8; ++d) s->sl[d] = c->st[(s->fsp0 - d) & 7u];
    s->xmm = 0; s->be = 0;
    s->nodes3 = s->nodes2 = s->leaves = s->surfaces = s->edges = s->vertices = 0;
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
    N4_ADD(N4_NODES3, s->nodes3); N4_ADD(N4_NODES2, s->nodes2); N4_ADD(N4_LEAVES, s->leaves);
    N4_ADD(N4_SURFACES, s->surfaces); N4_ADD(N4_EDGES, s->edges); N4_ADD(N4_VERTICES, s->vertices); N4_ADD(N4_BACKEDGES, s->be);
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
               what, a, b, s->nodes3, s->leaves, s->surfaces, s->edges);
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

/* The hook: in place of query_fused_172c95_171f94(c) (recomp/kernel/xk_query_reuse.c). */
void xv_native_4b9d0_query(xctx *c)
{
    const int mode = n4_mode();
    const int timed = n4_timing();
    if (!mode) {
        if (!timed) { query_fused_172c95_171f94(c); return; }
        const uint64_t t0 = n4_ns();
        query_fused_172c95_171f94(c);
        __atomic_fetch_add(&n4_guest_ns, n4_ns() - t0, __ATOMIC_RELAXED); N4_ADD(N4_TIMED_GUEST, 1);
        return;
    }
    /* The fused guest code translates integer accesses through the live table and float accesses through the
     * thread's: the native (one table) only where the two are the same. The query is tick work anyway. */
    if ((xv_scene_thread_on_helper && xv_scene_thread_on_helper()) || X_PT != g_xpt) {
        N4_ADD(N4_DECLINED, 1);
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
           "nodes %u 2d-nodes %u leaves %u surfaces %u edges %u vertices %u back-edges %u nan-words %u%s\n",
           frames, n[N4_CALLS], n[N4_VERIFIED], n[N4_MISMATCHED], __atomic_load_n(&n4_mismatch_total, __ATOMIC_RELAXED),
           n[N4_DECLINED], n[N4_JOURNAL_FAIL], n[N4_NODES3], n[N4_NODES2], n[N4_LEAVES], n[N4_SURFACES], n[N4_EDGES],
           n[N4_VERTICES], n[N4_BACKEDGES], n[N4_NAN_WORDS], timing);
}
