/* xk_native_1721b0.c - native Halo CE (Xbox 3925) BSP ray cast under f_001721B0 (the collision vector test).
 *
 * f_001721B0 casts a collision vector (a segment) through the structure BSP and the objects near it. In a30 gameplay on
 * the Vita it costs ~300 us per call (the looping-sound obstruction pass alone ~59 rays and ~28 ms per frame; particle
 * collision, projectiles and AI lines of sight on top). Host sampling (x86 and Pi 4, a30) puts ~60 % of the subtree in
 * one self-contained piece, the BSP segment cast f_00088E90, which 1721B0 calls once for the structure BSP and which
 * f_001731D0 (under the object walk f_00171AF0) calls once per object collision-model region the ray reaches
 * (docs/native-1721b0.md). That subtree is native here:
 *   f_00088E90  set-up: the traversal record S on its frame (flags, BSP, the caller's words, point/delta pointers, the
 *               result record), result t = 0 or 1 or the caller's limit, leaf count cleared; then 88B80 on [0, t1]
 *   f_00088B80  recursive segment traversal: the node's plane interval (the xv_bsp_plane_interval arithmetic of the
 *               generated code, 88BA5..88BF8), near child first, split at t = -origin/delta, far child if the hit so far
 *               lies beyond the split; leaves: solid/leaf-kind rules, the leaf list (<= 256, +0x414 overflow slot), the
 *               leaf's surface test 889E0, the hit record
 *   f_000889E0  a leaf's BSP2D references on the crossed plane: the hit point projected on the plane's dominant axes,
 *               17ADD0 (BSP2D point location), 86E20 (point in the surface's edge ring)
 *   f_0017ADD0  BSP2D point location;   f_00086E20  point in polygon (edge ring walk), the surface's "ignored" bit mask
 * No other calls, no HLE. The hook is the entry of f_00088E90 (tools/patch_native_1721b0_hooks.py), so every caller
 * of the cast (1721B0, 1731D0 and seven others) is served; when the native declines, the translation runs.
 *
 * Exact by construction (a transliteration of the generated code, docs/native-1721b0.md):
 *  - every guest memory read and write happens in the guest's order at the guest's address through the same
 *    translation (X_PT; integer accesses single-translation like X_M32, x87 float loads and stores page-split like
 *    x87_load_f32 / x87_store_f32); a value the guest reads back from memory is taken from a local only where no store
 *    but the owner's can have reached it: the frames of 88B80/889E0/17ADD0/86E20 (the result record is checked to lie
 *    outside the 64 KB below 88E90's frame, the recursion is bounded well inside it); S, the result record and all
 *    guest data are read from memory at the guest's points;
 *  - registers: every guest register is a local assigned where the generated code assigns it (partial writes
 *    included: fnstsw ax, setcc, mov al/bl/cl/dl/ax/cx/dx); callee-saved registers come back from the pushes;
 *  - lazy flags: the whole record (kind, operands, result, width, both override cells, both stale cf/of cells),
 *    written by exactly the instructions that write it in the generated code (inc keeps only the carry, shl/shr set
 *    both overrides, `neg`, `or r,-1`, most `xor r,r` and `and r,imm` write none);
 *  - x87: slots below the entry TOP are depth-indexed locals updated like st[] (d1..d4; dead values kept, fstp st(2),
 *    fstp st(0)); doubles in the translation's operand order and rounding points (float loads widened, float stores
 *    rounded, -ffp-contract=off); the status word as x87_compare leaves it (condition codes of the last compare, the
 *    TOP of every compare OR-ed in, never cleared);
 *  - the back-edge budget: c->preempt drops by exactly the guest's back-edge count (the loops of 889E0, 17ADD0,
 *    86E20) and xv_preempt() is called the same number of times, after the call instead of mid-loop (a scheduling
 *    point only, as in xk_native_visibility.c / xk_native_4b9d0.c).
 * Declines (the translation runs): esp not 4-aligned or near the address-space ends, a result record inside the 64 KB
 * below 88E90's frame. A recursion deeper than NR_MAXDEPTH levels hands that subtree to the translated f_00088B80
 * (exact: the state is the guest's at every call boundary).
 *
 * XV_NATIVE_1721B0 build flag (hook: tools/patch_native_1721b0_hooks.py). Env XV_NATIVE_1721B0: 0 off (default
 * XV_NATIVE_1721B0_DEFAULT), 1 verify (native with a write journal, undo, run the translation on the same state,
 * compare registers/flags/x87/budget, every byte the native wrote and the regions the guest may write, keep the guest
 * result), 2 native. XV_NATIVE_1721B0_TIME=1: ns/call (Vita: us clock) of the translation (mode 0), the native (2),
 * both (1). [native-1721b0] lines every 60 frames. Host harness only: XV_NATIVE_1721B0_CAPTURE=<file>[:n[:skip]]
 * writes entry states for tools/tests/native_1721b0.c --replay. */
#include "xk.h"
/* xv_x86rt.h comes through xk.h (one path: the unit is also compiled from a copy by tools/test_native_1721b0.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_NATIVE_1721B0_DEFAULT
#define XV_NATIVE_1721B0_DEFAULT 0
#endif

enum { NR_ZERO = 0x1F0A68u, NR_ONE = 0x1F0A78u, NR_AXES = 0x1EAF30u, NR_MAXDEPTH = 1024u };

/* ---- guest memory, as the generated code addresses it (X_PT, taken once per call) --------------------------- */
typedef struct { uint8_t *ram; const uint32_t *pt; int jon; } nr_mem;   /* jon: journal the writes (verify) */
#define NR_P(a) (m->ram + m->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))

/* Verify mode journals every native write (address, size, old bytes) so the pre-state can be restored. */
typedef struct { uint32_t addr; uint8_t size, pad[3]; uint8_t old[4], now[4]; uint32_t word; } nr_jent;
typedef struct { nr_jent *e; unsigned n, cap; int overflow; } nr_journal;
static __thread nr_journal nr_j;
static void nr_jlog_slow(uint32_t a, const void *host, unsigned size)
{
    if (nr_j.n == nr_j.cap) {
        unsigned cap = nr_j.cap ? nr_j.cap * 2u : 4096u;
        nr_jent *e = realloc(nr_j.e, cap * sizeof *e);
        if (!e) { nr_j.overflow = 1; return; }
        nr_j.e = e; nr_j.cap = cap;
    }
    nr_jent *j = &nr_j.e[nr_j.n++]; j->addr = a; j->size = (uint8_t)size; memcpy(j->old, host, size);
}
#define nr_jlog(a, host, size) do { if (__builtin_expect(m->jon, 0)) nr_jlog_slow((a), (host), (size)); } while (0)

static inline __attribute__((always_inline)) uint32_t nr_r32(const nr_mem *m, uint32_t a) { uint32_t v; memcpy(&v, NR_P(a), 4); return v; }
static inline __attribute__((always_inline)) uint16_t nr_r16(const nr_mem *m, uint32_t a) { uint16_t v; memcpy(&v, NR_P(a), 2); return v; }
static inline __attribute__((always_inline)) uint8_t nr_r8(const nr_mem *m, uint32_t a) { return *NR_P(a); }
static inline __attribute__((always_inline)) void nr_w32(const nr_mem *m, uint32_t a, uint32_t v) { uint8_t *p = NR_P(a); nr_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline __attribute__((always_inline)) void nr_w16(const nr_mem *m, uint32_t a, uint16_t v) { uint8_t *p = NR_P(a); nr_jlog(a, p, 2); memcpy(p, &v, 2); }
static inline __attribute__((always_inline)) void nr_w8(const nr_mem *m, uint32_t a, uint8_t v) { uint8_t *p = NR_P(a); nr_jlog(a, p, 1); *p = v; }
/* x87_load_f32 / x87_store_f32: page-split when the float crosses a page end */
static inline __attribute__((always_inline)) double nr_rf(const nr_mem *m, uint32_t a)
{
    float v;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(&v, NR_P(a), 4); else x_guest_read_pages(&v, a, 4);
    return (double)v;
}
static void nr_wf_split(const nr_mem *m, uint32_t a, float v)
{
    if (__builtin_expect(m->jon, 0))
        for (unsigned i = 0; i < 4; ++i) { uint32_t b = a + i; nr_jlog(b, NR_P(b), 1); }
    x_guest_write_pages(a, &v, 4);
}
static inline __attribute__((always_inline)) float nr_wf(const nr_mem *m, uint32_t a, double d)
{
    float v = (float)d;
    if ((a & 0xFFFu) <= 0xFFCu) { uint8_t *p = NR_P(a); nr_jlog(a, p, 4); memcpy(p, &v, 4); }
    else nr_wf_split(m, a, v);
    return v;
}
static inline __attribute__((always_inline)) uint32_t nr_ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) uint16_t nr_ld16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline __attribute__((always_inline)) float nr_ldf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }
static inline __attribute__((always_inline)) float nr_u2f(uint32_t u) { float v; memcpy(&v, &u, 4); return v; }
static inline __attribute__((always_inline)) uint32_t nr_f2u(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
/* A guest record read through one translation when [a, a+len) lies in one page (host(a) + off is then exactly the
 * translation of a + off, for integer and x87 loads alike); otherwise NULL and every field is translated. */
static inline __attribute__((always_inline)) const uint8_t *nr_span(const nr_mem *m, uint32_t a, uint32_t len)
{ return (a & 0xFFFu) + len <= 0x1000u ? NR_P(a) : NULL; }
#define H32(h, base, off) ((h) ? nr_ld32((h) + (off)) : nr_r32(m, (base) + (off)))
#define HF(h, base, off) ((h) ? (double)nr_ldf((h) + (off)) : nr_rf(m, (base) + (off)))

/* A function's frame [E - lo, E + hi) (E: its entry esp, 4-aligned): the host pointers of its (at most two) pages,
 * translated once; a 4-aligned slot never crosses a page end, so one translation is exact for integer and x87
 * accesses alike. hf = the host address of E when the frame lies in one page, else NULL. */
typedef struct { uint32_t p0; uint8_t *h0, *h1; } nr_stk;
static inline __attribute__((always_inline)) void nr_stk_at(const nr_mem *m, nr_stk *k, uint32_t lo)
{
    k->p0 = lo >> 12; k->h0 = m->ram + m->pt[k->p0]; k->h1 = m->ram + m->pt[(k->p0 + 1u) & 0xFFFFFu];
}
#define NR_SP(k, a) ((((uint32_t)(a) >> 12) == (k)->p0 ? (k)->h0 : (k)->h1) + ((uint32_t)(a) & 0xFFFu))
#define NR_FRAME(lo, hi) nr_stk k; uint8_t *hf; \
    if (__builtin_expect(((E - (lo)) >> 12) == ((E + (hi) - 1u) >> 12), 1)) { hf = NR_P(E - (lo)) + (lo); k.p0 = 0; k.h0 = k.h1 = NULL; } \
    else { hf = NULL; nr_stk_at(m, &k, E - (lo)); }
#define FP(a) (__builtin_expect(hf != NULL, 1) ? hf + (int32_t)((uint32_t)(a) - E) : NR_SP(&k, (a)))
#define FW32(a, v) do { const uint32_t a__ = (a), v__ = (v); uint8_t *p__ = FP(a__); nr_jlog(a__, p__, 4); memcpy(p__, &v__, 4); } while (0)
#define FW16(a, v) do { const uint32_t a__ = (a); const uint16_t v__ = (uint16_t)(v); uint8_t *p__ = FP(a__); nr_jlog(a__, p__, 2); memcpy(p__, &v__, 2); } while (0)
#define FW8(a, v) do { const uint32_t a__ = (a); const uint8_t v__ = (uint8_t)(v); uint8_t *p__ = FP(a__); nr_jlog(a__, p__, 1); *p__ = v__; } while (0)
#define FWF(a, d) ({ const float f__ = (float)(d); const uint32_t a__ = (a); uint8_t *p__ = FP(a__); nr_jlog(a__, p__, 4); memcpy(p__, &f__, 4); f__; })
#define FR32(a) nr_ld32(FP(a))
#define FR8(a) (*FP(a))
#define FRF(a) ((double)nr_ldf(FP(a)))
#define PUSH(v) do { const uint32_t w__ = (v); esp -= 4u; FW32(esp, w__); } while (0)

/* ---- per-call state ---------------------------------------------------------------------------------------- */
typedef struct {
    nr_mem m;
    uint32_t fk, fa, fb, fr, fbits, fcfo, fcf, fofo, fof;   /* the lazy-flag record, as xctx holds it */
    uint32_t fsp0;
    uint16_t fsw;
    double sl[8];              /* sl[d]: x87 slot st[(fsp0 - d) & 7] (d = 1..4 written by the subtree) */
    uint32_t be;               /* back-edges taken (X_PREEMPT sites) */
    uint32_t F;                /* 88E90's frame = the traversal record S (88B80's ebp) */
    uint8_t *hS;               /* the host address of F when [F, F+0x28) lies in one page, else NULL */
    /* What no store of the call can reach (nr_layout: the record, the frames, S and 88E90's arguments lie apart from
     * them): S's words 88E90 stores and nothing rewrites (the flags, the caller's word, a1, the BSP, point, delta
     * and record pointers), the BSP header, the point and delta floats, the image constants. */
    uint32_t sflags, sword, sa1, bsp, pt, dir, rec;
    uint32_t nodes3, planes, leavesa, refsa, nodes2a, surfsa, edgesa, vertsa;
    double P[3], D[3], zero, one;
    int16_t axes[12];
    /* What only the call's own stores change: S+0x1C/+0x20/+0x24 (the last leaf, its kind, the crossed plane) and the
     * record's t and leaf count, as last stored (every store also goes to guest memory in the guest's order) */
    uint32_t s1c, s24, rt, rcount; uint8_t s20;
    unsigned maxdepth;
    int jfail;                 /* a subtree ran in the translation while journaling: the call cannot be verified */
    xctx *c;                   /* the guest context (only for a delegated subtree) */
    unsigned nodes, leaves, refs, polys, edges, steps2, hits, deleg;
} nrs;
typedef struct { uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; } nrr;

/* The lazy-flag record in locals. Every function of the subtree writes the whole record before it reads a flag, and
 * every call is followed by a flag-writing instruction, so across a call only the two stale cells (cf, of: X_FLAGS
 * leaves them) travel; a function's exit record is its own (saved for the top level). */
#define NR_FL_LOCALS uint32_t fk = s->fk, fa = s->fa, fb = s->fb, fr = s->fr, fbits = s->fbits, fcfo = s->fcfo, fcf = s->fcf, fofo = s->fofo, fof = s->fof
#define NR_FL_SAVE() (s->fk = fk, s->fa = fa, s->fb = fb, s->fr = fr, s->fbits = fbits, s->fcfo = fcfo, s->fcf = fcf, s->fofo = fofo, s->fof = fof)
/* X_FLAGS / X_FLAGS_C */
#define SETF(k_, a_, b_, r_, bits_) (fk = (k_), fa = (uint32_t)(a_), fb = (uint32_t)(b_), fr = (uint32_t)(r_), fbits = (bits_), fcfo = 0, fofo = 0)
#define SETFC(k_, a_, b_, r_, bits_, cf_) (SETF(k_, a_, b_, r_, bits_), fcf = (cf_))
static inline __attribute__((always_inline)) uint32_t nr_fmask(uint32_t bits) { return bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u); }
static inline __attribute__((always_inline)) uint32_t nr_zf(uint32_t k, uint32_t r, uint32_t bits)
{ return k == XK_EXPLICIT ? (r >> 6) & 1u : (r & nr_fmask(bits)) == 0; }
static inline __attribute__((always_inline)) uint32_t nr_sf(uint32_t k, uint32_t r, uint32_t bits)
{ return k == XK_EXPLICIT ? (r >> 7) & 1u : (r >> (bits - 1)) & 1u; }
static inline __attribute__((always_inline)) uint32_t nr_pf(uint32_t k, uint32_t r)
{
    if (k == XK_EXPLICIT) return (r >> 2) & 1u;
    uint32_t v = r & 0xFFu; v ^= v >> 4;
    return ((0x6996u >> (v & 0xFu)) & 1u) ^ 1u;
}
static inline __attribute__((always_inline)) uint32_t nr_cf(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t bits, uint32_t cfo, uint32_t cf)
{
    if (k == XK_EXPLICIT) return r & 1u;
    if (cfo) return cf;
    uint32_t mk = nr_fmask(bits); a &= mk; b &= mk; r &= mk;
    switch (k) {
    case XK_ADD: return r < a;
    case XK_ADC: return cf ? (r <= a) : (r < a);
    case XK_SUB: return a < b;
    case XK_SBB: return cf ? (a <= b) : (a < b);
    default: return 0;
    }
}
static inline __attribute__((always_inline)) uint32_t nr_of(uint32_t k, uint32_t a, uint32_t b, uint32_t r, uint32_t bits, uint32_t ofo, uint32_t of)
{
    if (k == XK_EXPLICIT) return (r >> 11) & 1u;
    if (ofo) return of;
    switch (k) {
    case XK_ADD: case XK_ADC: return (((a ^ r) & (b ^ r)) >> (bits - 1)) & 1u;
    case XK_SUB: case XK_SBB: return (((a ^ b) & (a ^ r)) >> (bits - 1)) & 1u;
    default: return 0;
    }
}
#define ZF() nr_zf(fk, fr, fbits)
#define SF() nr_sf(fk, fr, fbits)
#define PF() nr_pf(fk, fr)
#define CF() nr_cf(fk, fa, fb, fr, fbits, fcfo, fcf)
#define OF() nr_of(fk, fa, fb, fr, fbits, fofo, fof)
/* inc as translated: only the carry is kept (override), no other field */
#define INC_CF() do { uint32_t cf__ = CF(); fcfo = 1; fcf = cf__; } while (0)
/* x_shl32 / x_shr32 (count 1..31) */
#define SHL32(v_, n_) ({ uint32_t v__ = (v_), n__ = (n_) & 31u, r__ = v__; if (n__) { r__ = v__ << n__; SETF(XK_LOGIC, 0, 0, r__, 32); \
                         fcfo = 1; fcf = (v__ >> (32u - n__)) & 1u; fofo = 1; fof = (r__ >> 31) ^ fcf; } r__; })
#define SHR32(v_, n_) ({ uint32_t v__ = (v_), n__ = (n_) & 31u, r__ = v__; if (n__) { r__ = v__ >> n__; SETF(XK_LOGIC, 0, 0, r__, 32); \
                         fcfo = 1; fcf = (v__ >> (n__ - 1u)) & 1u; fofo = 1; fof = (v__ >> 31) & 1u; } r__; })
/* fnstsw ax; test ah,imm */
#define FNSTSW() (eax = (eax & 0xFFFF0000u) | fsw)
#define TEST_AH(imm) do { uint8_t r__ = (uint8_t)((eax >> 8) & (imm)); SETF(XK_LOGIC, 0, 0, r__, 8); } while (0)
#define LO8(r, v) ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))
#define LO16(r, v) ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))
#define SX16(v) ((uint32_t)(int32_t)(int16_t)(uint16_t)(v))

/* x87_compare: condition codes of this compare, the TOP field OR-ed in (never cleared) */
static inline __attribute__((always_inline)) uint16_t nr_cc(double a, double b)
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
#define FCMP(a_, b_, depth_) (fsw = (uint16_t)((fsw & ~0x4700u) | nr_cc((a_), (b_)) | (((fsp0 - (depth_)) & 7u) << 11)))

/* Registers in locals. Around a call the callee gets them all (it pushes the callee-saved ones) and returns eax, ecx,
 * edx and esp; ebx/ebp/esi/edi come back from its pops unchanged (the saved slots are its own frame, which no store of
 * the call but its pushes reaches), so the caller keeps its locals. */
#define NR_REGS_IN uint32_t eax = R->eax, ecx = R->ecx, edx = R->edx, ebx = R->ebx, esp = R->esp, ebp = R->ebp, esi = R->esi, edi = R->edi
#define NR_REGS_OUT() (R->eax = eax, R->ecx = ecx, R->edx = edx, R->ebx = ebx, R->esp = esp, R->ebp = ebp, R->esi = esi, R->edi = edi)
#define NR_LOCALS nr_mem mm_ = s->m; const nr_mem *const m = &mm_; NR_REGS_IN; NR_FL_LOCALS; \
    const uint32_t fsp0 = s->fsp0; uint16_t fsw = s->fsw; uint32_t be = s->be; (void)fsp0
/* x87 slots d1..dN a function writes: locals, saved at calls and exits, reloaded after calls (the callee's writes win) */
#define NR_CALL_SAVE(X87SAVE) (NR_REGS_OUT(), s->fcf = fcf, s->fof = fof, s->fsw = fsw, X87SAVE, s->be = be)
#define NR_CALL_LOAD(X87LOAD) (eax = R->eax, ecx = R->ecx, edx = R->edx, esp = R->esp, fcf = s->fcf, fof = s->fof, fsw = s->fsw, X87LOAD, be = s->be)
#define NR_EXIT(X87SAVE) (R->eax = eax, R->ecx = ecx, R->edx = edx, R->esp = esp, NR_FL_SAVE(), s->fsw = fsw, X87SAVE, s->be = be)
#define X12_SAVE (s->sl[1] = x1, s->sl[2] = x2)
#define X12_LOAD (x1 = s->sl[1], x2 = s->sl[2])
#define X123_SAVE (s->sl[1] = x1, s->sl[2] = x2, s->sl[3] = x3)
#define X123_LOAD (x1 = s->sl[1], x2 = s->sl[2], x3 = s->sl[3])
#define X1234_SAVE (s->sl[1] = x1, s->sl[2] = x2, s->sl[3] = x3, s->sl[4] = x4)
/* S writes (88B80's mov [ebp+1Ch],esi / mov [ebp+20h],bl / mov [ebp+24h],eax), also kept in s */
#define SW32(off, v) do { const uint32_t v__ = (v); if (s->hS) { uint8_t *p__ = s->hS + (off); nr_jlog(s->F + (off), p__, 4); memcpy(p__, &v__, 4); } \
                          else nr_w32(m, s->F + (off), v__); } while (0)
#define SW8(off, v) do { const uint8_t v__ = (uint8_t)(v); if (s->hS) { uint8_t *p__ = s->hS + (off); nr_jlog(s->F + (off), p__, 1); *p__ = v__; } \
                         else nr_w8(m, s->F + (off), v__); } while (0)
/* the axes table 0x1EAF30 + i (i: a byte offset): the cached words inside it, guest memory elsewhere */
#define AXIS(i) ((uint32_t)(i) <= 0x16u && !((i) & 1u) ? (uint32_t)(int32_t)s->axes[(uint32_t)(i) >> 1] : SX16(nr_r16(m, (uint32_t)(i) + NR_AXES)))

/* ---- f_00086E20: point in polygon. esp -> [ret][a0 word][bit array][surface][axis][side byte][&point]; eax = BSP;
 * ret 18h. The point is 889E0's frame floats (pu, pv: its own slots, which nothing below writes). Walks the surface's
 * edge ring from its first edge (the edge's side by its right-hand surface word), the 2D cross product of edge and
 * point must stay at or below zero; al = 1 when the ring closes, 0 at the first edge the point is outside of (or when
 * the surface's bit is clear in the caller's mask). The loop's jmp is a back-edge. ------------------------------- */
static __attribute__((noinline)) void nr_86e20(nrs *restrict s, nrr *restrict R, const uint32_t a0, const uint32_t a1, const uint32_t surf,
                                               const uint32_t axis, const uint32_t side, const uint32_t pt, const float pu, const float pv)
{
    NR_LOCALS;
    double x1 = s->sl[1], x2 = s->sl[2], x3 = s->sl[3], x4 = s->sl[4];
    const uint32_t E = esp;
    NR_FRAME(0x2Cu, 0xCu);                                        /* its pushes and locals, the two argument slots it rewrites */
    s->polys++;
    edx = surf;                                                   /* mov edx,[esp+0Ch] */
    esp = E - 0x1Cu;
    const uint32_t sv_ebx = ebx, sv_ebp = ebp, sv_esi = esi, sv_edi = edi;
    PUSH(ebx); PUSH(ebp); PUSH(esi); PUSH(edi);                   /* esp = E - 0x2C */
    edi = eax;                                                    /* the BSP (s->bsp) */
    ecx = s->surfsa;                                              /* mov ecx,[edi+40h] */
    eax = edx * 3u;
    eax = ecx + eax * 4u;
    { uint8_t r_ = nr_r8(m, eax + 8u) & 8u; SETF(XK_LOGIC, 0, 0, r_, 8); }
    if (ZF()) goto L_86E65;
    esi = nr_r8(m, eax + 9u);
    ecx = SX16(a0);                                               /* movsx ecx,word ptr [esp+30h] */
    SETF(XK_SUB, esi, ecx, esi - ecx, 32);
    if (SF() == OF()) goto L_86E65;
    ecx = esi & 0x1Fu;
    ebx = 1u;
    ebx = SHL32(ebx, (uint8_t)ecx);                               /* shl ebx,cl */
    ecx = a1;                                                     /* mov ecx,[esp+34h] */
    esi = SHR32(esi, 5u);
    { uint32_t r_ = nr_r32(m, ecx + esi * 4u) & ebx; SETF(XK_LOGIC, 0, 0, r_, 32); }
    if (ZF()) goto L_86F35;
L_86E65:;
    eax = nr_r32(m, eax + 4u);
    ecx = SX16(axis);                                             /* movsx ecx,word ptr [esp+3Ch] */
    ebx = s->vertsa;                                              /* mov ebx,[edi+58h] */
    FW32(esp + 0x10u, eax); const uint32_t first = eax;
    esi = eax;
    eax = s->edgesa;                                              /* mov eax,[edi+4Ch] */
    FW32(esp + 0x30u, eax); const uint32_t edges = eax;           /* over the a0 argument */
    eax = (uint8_t)side;                                          /* movzx eax,byte ptr [esp+40h] */
    eax = eax + ecx * 2u;
    eax = SHL32(eax, 2u);
    edi = AXIS(eax);                                              /* movsx edi,word ptr [eax+1EAF30h] */
    ebp = AXIS(eax + 2u);                                         /* movsx ebp,word ptr [eax+1EAF32h] */
    edi = SHL32(edi, 2u);
    FW32(esp + 0x34u, ebx); const uint32_t verts = ebx;           /* over the mask argument */
    ebp = SHL32(ebp, 2u);
    for (;;) {                                                    /* 86EA0 */
        s->edges++;
        eax = edges;                                              /* mov eax,[esp+30h] */
        ecx = esi * 3u;
        esi = eax + ecx * 8u;
        { uint32_t a_ = nr_r32(m, esi + 0x14u); SETF(XK_SUB, a_, edx, a_ - edx, 32); }
        LO8(ecx, ZF());                                           /* sete cl */
        edx = (uint8_t)ecx;                                       /* movzx edx,cl */
        eax = nr_r32(m, esi + edx * 4u);
        eax = SHL32(eax, 4u);
        eax = eax + ebx;
        ebx = 0;
        SETF(XK_LOGIC, 0, 0, (uint8_t)ecx, 8);                    /* test cl,cl */
        x1 = nr_rf(m, edi + eax);                                 /* depth 1 */
        LO8(ebx, ZF());                                           /* sete bl */
        x2 = nr_rf(m, eax + ebp);                                 /* depth 2 */
        eax = pt;                                                 /* mov eax,[esp+44h] */
        ecx = nr_r32(m, esi + ebx * 4u);
        ebx = verts;                                              /* mov ebx,[esp+34h] */
        ecx = SHL32(ecx, 4u);
        ecx = ecx + ebx;
        x3 = nr_rf(m, edi + ecx);                                 /* depth 3 */
        x4 = nr_rf(m, ecx + ebp);                                 /* depth 4 */
        const float w20 = FWF(esp + 0x20u, x4);                   /* fstp [esp+20h]: depth 3 */
        x4 = (double)pu;                                          /* fld [eax]: depth 4 */
        x4 = x4 - x1;                                             /* fsub st,st(3) */
        const float w14 = FWF(esp + 0x14u, x4);                   /* depth 3 */
        x4 = (double)pv;                                          /* fld [eax+4] */
        x4 = x4 - x2;                                             /* fsub st,st(2) */
        const float w18 = FWF(esp + 0x18u, x4);                   /* depth 3 */
        x3 = x3 - x1;                                             /* fsub st,st(2) */
        const float w24 = FWF(esp + 0x24u, x3);                   /* depth 2 */
        x3 = (double)w20;                                         /* fld [esp+20h]: depth 3 */
        x3 = x3 - x2;                                             /* fsub st,st(1) */
        x1 = x3;                                                  /* fstp st(2): depth 2; fstp st(0): depth 1 */
        x2 = (double)w14;                                         /* fld [esp+14h]: depth 2 */
        x2 = x2 * x1;                                             /* fmul st,st(1) */
        x3 = (double)w18;                                         /* fld [esp+18h]: depth 3 */
        x3 = x3 * (double)w24;                                    /* fmul [esp+24h] */
        x2 = x2 - x3;                                             /* fsubp: depth 2 */
        FCMP(x2, s->zero, 2);                                     /* fcomp [1F0A68]: depth 1 */
        FNSTSW();                                                 /* fstp st(0): depth 0 */
        TEST_AH(0x41);
        if (ZF()) goto L_86F35;
        esi = nr_r32(m, esi + edx * 4u + 8u);
        SETF(XK_SUB, esi, first, esi - first, 32);                /* cmp esi,[esp+10h] */
        if (ZF()) goto L_86F41;
        edx = surf;                                               /* mov edx,[esp+38h] */
        be++;                                                     /* jmp 86EA0 (a back-edge) */
    }
L_86F35:
    edi = sv_edi; esi = sv_esi; ebp = sv_ebp; LO8(eax, 0); ebx = sv_ebx;
    esp = E + 0x1Cu;
    NR_EXIT(X1234_SAVE);
    return;
L_86F41:
    edi = sv_edi; esi = sv_esi; ebp = sv_ebp; LO8(eax, 1); ebx = sv_ebx;
    esp = E + 0x1Cu;
    NR_EXIT(X1234_SAVE);
}

/* ---- f_000889E0 (+ f_0017ADD0 inline): a leaf's BSP2D references on the crossed plane. esp -> [ret][a0 word][a1]
 * [plane][t0][hit byte]; ebx = BSP, ecx = leaf, esi = delta, edi = point, ebp = S. ret 14h; eax = surface or -1.
 * Per reference whose plane word (sign bit off) is the crossed plane: the dominant axis of the plane normal, the point
 * at t0 projected on the other two axes (the axes table 0x1EAF30), its BSP2D surface (17ADD0), then, when the hit
 * byte is set, the point in that surface's polygon (86E20). The reference loop's jl is a back-edge, and so is 17ADD0's
 * jns. ------------------------------------------------------------------------------------------------------------ */
static __attribute__((noinline)) void nr_889e0(nrs *restrict s, nrr *restrict R, const uint32_t a0, const uint32_t a1, const uint32_t pln,
                                               const uint32_t t0w, const uint32_t hitw)
{
    NR_LOCALS;
    double x1 = s->sl[1], x2 = s->sl[2], x3 = s->sl[3];
    const uint32_t E = esp;
    NR_FRAME(0x48u, 0x18u);                                       /* its frame, 17ADD0's push, the pushes for 86E20 */
    s->leaves++;
    esp = E - 0x28u;
    eax = s->leavesa;                                             /* mov eax,[ebx+1Ch] */
    edx = SX16(nr_r16(m, eax + ecx * 8u + 2u));
    eax = eax + ecx * 8u;
    const uint32_t sv_ebp = ebp;
    PUSH(ebp);                                                    /* esp = Q = E - 0x2C */
    const uint32_t Q = esp;
    ebp = nr_r32(m, eax + 4u);
    eax = edx + ebp;
    SETF(XK_SUB, ebp, eax, ebp - eax, 32);
    FW32(Q + 0xCu, ebp); uint32_t q0c = ebp;
    FW32(Q + 0x14u, eax); const uint32_t q14 = eax;
    uint32_t q08 = 0, q04 = 0;
    if (SF() == OF()) goto L_88B67;
    ecx = s->refsa;                                               /* mov ecx,[ebx+28h] */
    edx = ecx + ebp * 8u;
    FW32(Q + 8u, edx); q08 = edx;
    for (;;) {
        /* 88A10 */
        ecx = q08;                                                /* mov ecx,[esp+8] */
        ecx = nr_r32(m, ecx);
        edx = ecx & 0x7FFFFFFFu;
        SETF(XK_SUB, edx, pln, edx - pln, 32);                    /* cmp edx,[esp+38h] */
        if (!ZF()) goto L_88B4F;
        s->refs++;
        edx = pln;                                                /* mov edx,[esp+38h] */
        eax = s->planes;                                          /* mov eax,[ebx+10h] */
        edx = SHL32(edx, 4u);
        x1 = nr_rf(m, edx + eax);                                 /* depth 1 */
        edx = edx + eax;
        x1 = fabs(x1);
        x2 = nr_rf(m, edx + 4u); x2 = fabs(x2);                   /* depth 2 */
        x3 = nr_rf(m, edx + 8u); x3 = fabs(x3);                   /* depth 3 */
        FCMP(x3, x2, 3);                                          /* fcom */
        FNSTSW(); TEST_AH(1);
        if (!ZF()) goto L_88A63;
        FCMP(x3, x1, 3);                                          /* fcomp st(2): depth 2 */
        FNSTSW(); TEST_AH(1);
        if (!ZF()) goto L_88A65;
        FW32(Q + 4u, 2u); q04 = 2u;                               /* fstp st(0); mov [esp+4],2; fstp st(0): depth 0 */
        goto L_88A80;
    L_88A63:;                                                     /* fstp st(0): depth 2 */
    L_88A65:;
        FCMP(x2, x1, 2);                                          /* fcomp: depth 1 */
        FW32(Q + 4u, 1u); q04 = 1u;
        FNSTSW();                                                 /* fstp st(0): depth 0 */
        TEST_AH(1);
        if (ZF()) goto L_88A80;
        FW32(Q + 4u, 0u); q04 = 0u;
    L_88A80:;
        ebp = SX16(q04);                                          /* movsx ebp,word ptr [esp+4] */
        x1 = nr_rf(m, edx + ebp * 4u);                            /* depth 1 */
        FCMP(x1, s->zero, 1);                                     /* fcomp: depth 0 */
        FNSTSW(); TEST_AH(0x41);
        if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }   /* xor eax,eax */
        else eax = 1u;
        x1 = (double)nr_u2f(t0w);                                 /* fld [esp+3Ch]: depth 1 */
        ecx &= 0x80000000u;
        x1 = x1 * s->D[0];                                        /* fmul [esi] */
        ecx = 0u - ecx;                                           /* neg ecx (no flags in the translation) */
        eax = (uint8_t)eax;                                       /* movzx eax,al */
        { const uint32_t cf_ = CF(), r_ = ecx - ecx - cf_; SETFC(XK_SBB, ecx, ecx, r_, 32, cf_); ecx = r_; }   /* sbb ecx,ecx */
        x1 = x1 + s->P[0];                                        /* fadd [edi] */
        ecx = 0u - ecx;
        SETF(XK_SUB, eax, ecx, eax - ecx, 32);                    /* cmp eax,ecx */
        LO8(eax, !ZF());                                          /* setne al */
        const float p0 = FWF(Q + 0x20u, x1);                      /* fstp [esp+20h]: depth 0 */
        ecx = (uint8_t)eax;                                       /* movzx ecx,al */
        x1 = (double)nr_u2f(t0w);
        x1 = x1 * s->D[1];
        FW8(Q + 0x10u, eax);                                      /* mov [esp+10h],al */
        eax = ecx + ebp * 2u;
        x1 = x1 + s->P[1];
        eax = SHL32(eax, 2u);
        edx = AXIS(eax);                                          /* movsx edx,word ptr [eax+1EAF30h] */
        const float p1 = FWF(Q + 0x24u, x1);
        x1 = (double)nr_u2f(t0w);
        eax = AXIS(eax + 2u);                                     /* movsx eax,word ptr [eax+1EAF32h] */
        x1 = x1 * s->D[2];
        ecx = ebx + 0x30u;
        x1 = x1 + s->P[2];
        const float p2 = FWF(Q + 0x28u, x1);
        /* fld [esp+edx*4+20h]: one of the three floats just stored, anything else from memory */
        x1 = edx < 3u ? (double)(edx == 0u ? p0 : edx == 1u ? p1 : p2) : nr_rf(m, Q + edx * 4u + 0x20u);
        edx = q08;                                                /* mov edx,[esp+8] */
        const float pu = FWF(Q + 0x18u, x1);
        x1 = eax < 3u ? (double)(eax == 0u ? p0 : eax == 1u ? p1 : p2) : nr_rf(m, Q + eax * 4u + 0x20u);
        eax = nr_r32(m, edx + 4u);
        const float pv = FWF(Q + 0x1Cu, x1);                      /* depth 0 */
        edx = Q + 0x18u;
        FW32(Q - 4u, 0x88B18u);                                   /* call 0017ADD0h */
        {   /* f_0017ADD0: eax = root, ecx = BSP + 0x30, edx = the point (pu, pv) */
            SETF(XK_LOGIC, 0, 0, eax, 32);                        /* test eax,eax */
            if (!SF()) {
                FW32(Q - 8u, esi);                                /* push esi */
                const uint32_t sv = esi;
                esi = s->nodes2a;                                 /* mov esi,[ecx+4] */
                for (;;) {                                        /* 17ADE0 */
                    s->steps2++;
                    eax = eax * 5u;
                    x1 = nr_rf(m, esi + eax * 4u + 4u);           /* depth 1 */
                    ecx = esi + eax * 4u;
                    x1 = x1 * (double)pv;                         /* fmul [edx+4] */
                    x2 = nr_rf(m, ecx);                           /* depth 2 */
                    x2 = x2 * (double)pu;                         /* fmul [edx] */
                    x1 = x1 + x2;                                 /* faddp: depth 1 */
                    x1 = x1 - nr_rf(m, ecx + 8u);
                    FCMP(x1, s->zero, 1);                         /* fcomp: depth 0 */
                    FNSTSW(); TEST_AH(1);
                    if (!ZF()) { SETF(XK_LOGIC, eax, eax, 0, 32); eax = 0; }   /* xor eax,eax */
                    else eax = 1u;
                    eax = nr_r32(m, ecx + eax * 4u + 0xCu);
                    SETF(XK_LOGIC, 0, 0, eax, 32);                /* test eax,eax */
                    if (SF()) break;
                    be++;                                         /* jns 17ADE0 (a back-edge) */
                }
                esi = sv;                                         /* pop esi */
            }
            SETF(XK_SUB, eax, 0xFFFFFFFFu, eax + 1u, 32);         /* cmp eax,-1 */
            if (ZF()) eax = 0xFFFFFFFFu; else eax &= 0x7FFFFFFFu;
        }
        ebp = eax;                                                /* mov ebp,eax */
        LO8(eax, hitw);                                           /* mov al,[esp+40h] */
        SETF(XK_LOGIC, 0, 0, (uint8_t)eax, 8);
        if (ZF()) goto L_88B71;
        ecx = FR32(Q + 0x10u);                                    /* mov ecx,[esp+10h]: the byte above and 3 older ones */
        edx = q04;                                                /* mov edx,[esp+4] */
        eax = Q + 0x18u;
        esp = Q;
        PUSH(eax);
        eax = a1;                                                 /* mov eax,[esp+38h] */
        PUSH(ecx);
        { const uint32_t side_ = ecx;
          ecx = a0;                                               /* mov ecx,[esp+38h] */
          PUSH(edx); PUSH(ebp); PUSH(eax); PUSH(ecx);
          eax = ebx;
          PUSH(0x88B43u);                                         /* call 00086E20h */
          NR_CALL_SAVE(X123_SAVE); nr_86e20(s, R, a0, a1, ebp, edx, side_, Q + 0x18u, pu, pv); NR_CALL_LOAD(X123_LOAD); }
        SETF(XK_LOGIC, 0, 0, (uint8_t)eax, 8);
        if (!ZF()) goto L_88B71;
        eax = q14;                                                /* mov eax,[esp+14h] */
        ebp = q0c;                                                /* mov ebp,[esp+0Ch] */
    L_88B4F:
        edx = q08;
        INC_CF(); ebp = ebp + 1u;                                 /* inc ebp */
        edx = edx + 8u;
        SETF(XK_SUB, ebp, eax, ebp - eax, 32);
        FW32(Q + 0xCu, ebp); q0c = ebp;
        FW32(Q + 8u, edx); q08 = edx;
        if (SF() == OF()) break;
        be++;                                                     /* jl 88A10 (a back-edge) */
    }
L_88B67:
    eax = 0xFFFFFFFFu;
    ebp = sv_ebp;
    esp = E + 0x18u;
    NR_EXIT(X123_SAVE);
    return;
L_88B71:
    eax = ebp;
    ebp = sv_ebp;
    esp = E + 0x18u;
    NR_EXIT(X123_SAVE);
}

/* ---- f_00088B80: segment traversal. esp -> [ret][t0][t1]; ecx = S (88E90's frame), edx = node. ret 8; al = 1 when
 * the segment hit something that ends the walk.
 * The recursion runs on an explicit stack of levels (nr_lvl: what a level keeps across its child - its entry esp,
 * arguments, frame translation, the caller's registers it restores, the split t, which call returned), so the guest
 * state (registers, flags, x87 slots, status word, budget) stays in this function's locals across levels, as it is
 * one machine state in the guest. A level's frame is [E-0x34, E+0xC): its pushes, its three floats at E-0xC/E-8/E-4
 * (delta, origin, last; the split t), the pushes for its calls, the byte it writes over its t1 argument. t0w / t1w:
 * the argument words the caller pushed (nothing below writes them). The callee-saved registers after a child are the
 * ones the child popped: this level's own. A child deeper than NR_MAXDEPTH levels runs in the translation. ------ */
typedef struct { uint32_t E, t0w, t1w, sv_ebx, sv_ebp, sv_esi, sv_edi, ts, site; uint8_t *hf; } nr_lvl;
static __thread nr_lvl *nr_lv;       /* NR_MAXDEPTH levels per thread, allocated on first use */
void f_00088B80(xctx *);
static void nr_cache(nrs *s);
enum { NR_NEAR, NR_FAR, NR_ONE_SIDE };
static __attribute__((noinline)) void nr_88b80(nrs *restrict s, nrr *restrict R, uint32_t t0w, uint32_t t1w)
{
    NR_LOCALS;
    double x1 = s->sl[1], x2 = s->sl[2];
    nr_lvl *const lv = nr_lv;
    unsigned d = 0;                                               /* this level's index: levels 0..d-1 wait in lv[] */
    nr_lvl *L;                                                    /* this level's entry: what it keeps across a child */
    uint32_t E, site;
    uint8_t *hf; nr_stk k = { 0, NULL, NULL };                   /* k: only for a frame across a page end */
    float ts = 0.0f;
call:
    /* a level starts: esp at the pushed return address, ecx = S, edx = the node, t0w / t1w its arguments */
    if (__builtin_expect(d >= NR_MAXDEPTH, 0)) {
        /* the translated f_00088B80 on the guest context: the state is the guest's at this call boundary */
        xctx *c = s->c;
        c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = esp; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi;
        /* its first flag write (test edx,edx) comes before any flag read: of the record only the stale cells reach it */
        c->f_kind = XK_LOGIC; c->f_op1 = 0; c->f_op2 = 0; c->f_res = 0; c->f_bits = 32;
        c->f_cf_override = 0; c->f_cf = fcf; c->f_of_override = 0; c->f_of = fof;
        c->fsp = fsp0; c->fsw = fsw;
        c->st[(fsp0 - 1u) & 7u] = x1; c->st[(fsp0 - 2u) & 7u] = x2; c->st[(fsp0 - 3u) & 7u] = s->sl[3]; c->st[(fsp0 - 4u) & 7u] = s->sl[4];
        const int32_t budget = c->preempt;
        c->preempt = 1 << 30;
        f_00088B80(c);
        be += (uint32_t)((1 << 30) - c->preempt);
        c->preempt = budget;
        eax = c->r[0]; ecx = c->r[1]; edx = c->r[2]; ebx = c->r[3]; esp = c->r[4]; ebp = c->r[5]; esi = c->r[6]; edi = c->r[7];
        fk = c->f_kind; fa = c->f_op1; fb = c->f_op2; fr = c->f_res; fbits = c->f_bits;
        fcfo = c->f_cf_override; fcf = c->f_cf; fofo = c->f_of_override; fof = c->f_of;
        fsw = c->fsw;
        x1 = c->st[(fsp0 - 1u) & 7u]; x2 = c->st[(fsp0 - 2u) & 7u]; s->sl[3] = c->st[(fsp0 - 3u) & 7u]; s->sl[4] = c->st[(fsp0 - 4u) & 7u];
        /* what the native keeps in s, from guest memory again (the translation's frames may reach below W) */
        nr_cache(s);
        s->s1c = nr_r32(m, s->F + 0x1Cu); s->s20 = nr_r8(m, s->F + 0x20u); s->s24 = nr_r32(m, s->F + 0x24u);
        s->rt = nr_r32(m, s->rec); s->rcount = nr_r32(m, s->rec + 0x14u);
        s->deleg++;
        if (s->m.jon) s->jfail = 1;               /* its writes are not journaled: the call cannot be undone */
        goto ret;
    }
    if (d + 1u > s->maxdepth) s->maxdepth = d + 1u;
    L = &lv[d];
    E = esp;
    if (__builtin_expect(((E - 0x34u) >> 12) == ((E + 0xBu) >> 12), 1)) hf = NR_P(E - 0x34u) + 0x34u;
    else { hf = NULL; nr_stk_at(m, &k, E - 0x34u); }
    L->E = E; L->hf = hf; L->t0w = t0w; L->t1w = t1w;
    esp = E - 0xCu;
    SETF(XK_LOGIC, 0, 0, edx, 32);                                /* test edx,edx */
    L->sv_ebx = ebx; L->sv_ebp = ebp; L->sv_esi = esi; L->sv_edi = edi;
    PUSH(ebx); PUSH(ebp); PUSH(esi); PUSH(edi);                   /* esp = E - 0x1C */
    ebp = ecx;
    if (SF()) goto leaf;
    /* ---- a node ---- */
    s->nodes++;
    ecx = s->bsp;                                                 /* mov ecx,[ebp+4] */
    eax = edx * 3u;
    edx = s->nodes3;                                              /* mov edx,[ecx+4] */
    esi = edx + eax * 4u;
    edx = s->planes;                                              /* mov edx,[ecx+10h] */
    ecx = s->pt;                                                  /* mov ecx,[ebp+10h] */
    eax = nr_r32(m, esi);
    {   float o, dl, la;
        /* 88BA5..88BF8 (xv_bsp_plane_interval): distance of the point and of the delta to the plane, the segment's
         * signed distances at t0 and t1; last at depth 2 (a dead slot), first at depth 1. The plane's normal is read
         * twice, around the origin store, as the guest does. */
        eax = SHL32(eax, 4u) + edx;
        const uint32_t plane = eax;
        const uint8_t *hp = nr_span(m, plane, 16u);
        double origin, delta;
        ecx = s->dir;                                             /* mov ecx,[ebp+14h] */
        if (__builtin_expect(hp != NULL, 1)) {
            origin = (s->P[2] * (double)nr_ldf(hp + 8) + s->P[1] * (double)nr_ldf(hp + 4)) + (double)nr_ldf(hp) * s->P[0];
            origin = origin - (double)nr_ldf(hp + 12);
            o = FWF(E - 8u, origin);                              /* [esp+14h] */
            delta = (s->D[2] * (double)nr_ldf(hp + 8) + s->D[1] * (double)nr_ldf(hp + 4)) + (double)nr_ldf(hp) * s->D[0];
        } else {
            origin = (s->P[2] * nr_rf(m, plane + 8u) + s->P[1] * nr_rf(m, plane + 4u)) + nr_rf(m, plane) * s->P[0];
            origin = origin - nr_rf(m, plane + 12u);
            o = FWF(E - 8u, origin);
            delta = (s->D[2] * nr_rf(m, plane + 8u) + s->D[1] * nr_rf(m, plane + 4u)) + nr_rf(m, plane) * s->D[0];
        }
        dl = FWF(E - 0xCu, delta);                                /* [esp+10h] */
        const double first = delta * (double)nr_u2f(t0w) + (double)o;
        const double last = (double)dl * (double)nr_u2f(t1w) + (double)o;
        la = FWF(E - 4u, last);                                   /* [esp+18h] */
        x2 = last; x1 = first;
        FCMP(x1, s->zero, 1);                                     /* fcom [1F0A68] */
        FNSTSW(); TEST_AH(5);
        if (!PF()) goto L_88C1B;
        x2 = (double)la;                                          /* fld [esp+18h]: depth 2 */
        FCMP(x2, s->zero, 2);                                     /* fcomp: depth 1 */
        FNSTSW(); TEST_AH(5);
        if (!PF()) goto L_88C1B;
        SETF(XK_LOGIC, (uint8_t)ecx, (uint8_t)ecx, 0, 8); LO8(ecx, 0);   /* xor cl,cl */
        goto L_88C1D;
    L_88C1B:
        LO8(ecx, 1);                                              /* mov cl,1 */
    L_88C1D:
        FCMP(x1, s->zero, 1);                                     /* fcomp: depth 0 */
        FNSTSW(); TEST_AH(1);
        if (ZF()) goto L_88C3F;
        x1 = (double)la;                                          /* fld [esp+18h]: depth 1 */
        FCMP(x1, s->zero, 1);                                     /* fcomp: depth 0 */
        FNSTSW(); TEST_AH(1);
        if (ZF()) goto L_88C3F;
        SETF(XK_LOGIC, (uint8_t)eax, (uint8_t)eax, 0, 8); LO8(eax, 0);   /* xor al,al */
        goto L_88C41;
    L_88C3F:
        LO8(eax, 1);                                              /* mov al,1 */
    L_88C41:
        SETF(XK_LOGIC, 0, 0, (uint8_t)ecx, 8);                    /* test cl,cl */
        if (ZF()) goto L_88CBA;
        SETF(XK_LOGIC, 0, 0, (uint8_t)eax, 8);                    /* test al,al */
        if (ZF()) goto L_88CBA;
        /* the segment crosses the plane: split at t = -origin/delta, the near child on [t0, t] first */
        x1 = (double)dl;                                          /* fld [esp+10h]: depth 1 */
        FCMP(x1, s->zero, 1);                                     /* fcomp: depth 0 */
        FNSTSW(); TEST_AH(0x41);
        if (!ZF()) { SETF(XK_LOGIC, (uint8_t)ebx, (uint8_t)ebx, 0, 8); LO8(ebx, 0); }   /* xor bl,bl */
        else LO8(ebx, 1);                                         /* mov bl,1 */
        x1 = (double)o;                                           /* fld [esp+14h]: depth 1 */
        eax = t0w;                                                /* mov eax,[esp+20h] */
        x1 = x1 / (double)dl;                                     /* fdiv [esp+10h] */
    }
    ecx = 0;                                                      /* xor ecx,ecx */
    SETF(XK_LOGIC, 0, 0, (uint8_t)ebx, 8);                        /* test bl,bl */
    LO8(ecx, ZF());                                               /* sete cl */
    edx = nr_r32(m, esi + ecx * 4u + 4u);
    ecx = ebp;
    x1 = -x1;                                                     /* fchs */
    ts = FWF(E - 4u, x1);                                         /* fstp [esp+18h]: depth 0 */
    edi = nr_f2u(ts);                                             /* mov edi,[esp+18h] */
    PUSH(edi); PUSH(eax); PUSH(0x88C8Au);                         /* call 00088B80h */
    site = NR_NEAR;
    goto descend;
after_near:
    SETF(XK_LOGIC, 0, 0, (uint8_t)eax, 8);                        /* test al,al */
    if (!ZF()) goto L_88E27;
    edx = s->rec;                                                 /* mov edx,[ebp+18h] */
    x1 = (double)nr_u2f(s->rt);                                   /* fld [edx]: depth 1 */
    FCMP(x1, (double)ts, 1);                                      /* fcomp [esp+18h]: depth 0 */
    FNSTSW(); TEST_AH(0x41);
    if (!PF()) goto L_88E76;
    ecx = t1w;                                                    /* mov ecx,[esp+24h] */
    eax = nr_r32(m, esi);                                         /* mov eax,[esi] */
    edx = (uint8_t)ebx;                                           /* movzx edx,bl */
    PUSH(ecx);
    SW32(0x24u, eax); s->s24 = eax;                               /* mov [ebp+24h],eax */
    edx = nr_r32(m, esi + edx * 4u + 4u);
    PUSH(edi);
    ecx = ebp;                                                    /* the far child on [t, t1] */
    PUSH(0x88CD2u);                                               /* call 00088B80h */
    site = NR_FAR;
    goto descend;
L_88CBA:
    /* one side only: that child on [t0, t1] (al: the side) */
    ecx = t1w;                                                    /* mov ecx,[esp+24h] */
    edx = t0w;                                                    /* mov edx,[esp+20h] */
    eax = (uint8_t)eax;                                           /* movzx eax,al */
    PUSH(ecx); PUSH(edx);
    edx = nr_r32(m, esi + eax * 4u + 4u);
    ecx = ebp;
    PUSH(0x88CD2u);                                               /* call 00088B80h */
    site = NR_ONE_SIDE;
    goto descend;
after_far:
after_one_side:
    SETF(XK_LOGIC, 0, 0, (uint8_t)eax, 8);                        /* test al,al */
    if (ZF()) goto L_88E76;
    goto L_88E27;

leaf: {
    /* ---- a leaf (edx < 0; -1: outside the BSP) ---- */
    s->leaves++;
    esi = 0xFFFFFFFFu;                                            /* or esi,-1 */
    SETF(XK_SUB, edx, 0xFFFFFFFFu, edx + 1u, 32);                 /* cmp edx,-1 */
    LO8(ebx, 3);                                                  /* mov bl,3 */
    FW32(E - 8u, esi); uint32_t s14 = esi;                        /* mov [esp+14h],esi */
    FW8(E + 8u, ebx); uint8_t s24 = 3;                            /* mov [esp+24h],bl */
    FW8(E - 4u, 0);                                               /* mov byte ptr [esp+18h],0 */
    if (ZF()) goto L_88D1E;
    ecx = s->bsp;                                                 /* mov ecx,[ebp+4] */
    eax = s->leavesa;                                             /* mov eax,[ecx+1Ch] */
    edx &= 0x7FFFFFFFu;
    { uint8_t r_ = nr_r8(m, eax + edx * 8u) & 1u; SETF(XK_LOGIC, 0, 0, r_, 8); }
    LO8(eax, !ZF());                                              /* setne al */
    INC_CF(); LO8(eax, (uint8_t)eax + 1u);                        /* inc al */
    FW32(E - 8u, edx); s14 = edx;                                 /* mov [esp+14h],edx */
    FW8(E + 8u, eax); s24 = (uint8_t)eax;                         /* mov [esp+24h],al */
    LO8(ebx, eax);                                                /* mov bl,al */
    esi = edx;
L_88D1E:
    ecx = s->sflags;                                              /* mov ecx,[ebp] */
    edx = ecx;
    { const uint32_t a_ = edx, r_ = a_ & 1u; SETF(XK_LOGIC, a_, 1u, r_, 32); edx = r_; }   /* and edx,1 */
    FW32(E - 0xCu, ecx); const uint32_t s10 = ecx;                /* mov [esp+10h],ecx */
    if (ZF()) goto L_88D41;
    LO8(eax, s->s20);                                             /* mov al,[ebp+20h] */
    { const uint8_t a_ = (uint8_t)eax; SETF(XK_SUB, a_, 1u, (uint8_t)(a_ - 1u), 8); }
    if (ZF()) goto L_88D37;
    { const uint8_t a_ = (uint8_t)eax; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }
    if (!ZF()) goto L_88D41;
L_88D37:
    { const uint8_t a_ = (uint8_t)ebx; SETF(XK_SUB, a_, 3u, (uint8_t)(a_ - 3u), 8); }
    if (!ZF()) goto L_88D41;
    ecx = s->s1c;                                                 /* mov ecx,[ebp+1Ch] */
    goto L_88D86;
L_88D41:
    SETF(XK_LOGIC, 0, 0, (uint8_t)ecx & 2u, 8);                   /* test cl,2 */
    if (ZF()) goto L_88D5A;
    { const uint8_t a_ = s->s20; SETF(XK_SUB, a_, 3u, (uint8_t)(a_ - 3u), 8); }   /* cmp byte ptr [ebp+20h],3 */
    if (!ZF()) goto L_88D5A;
    { const uint8_t a_ = (uint8_t)ebx; SETF(XK_SUB, a_, 1u, (uint8_t)(a_ - 1u), 8); }
    if (ZF()) goto L_88D56;
    { const uint8_t a_ = (uint8_t)ebx; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }
    if (!ZF()) goto L_88D5A;
L_88D56:
    ecx = esi;
    goto L_88D86;
L_88D5A:
    SETF(XK_LOGIC, 0, 0, (uint8_t)ecx & 4u, 8);                   /* test cl,4 */
    if (!ZF()) goto L_88E3B;
    { const uint8_t a_ = s->s20; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }   /* cmp byte ptr [ebp+20h],2 */
    if (!ZF()) goto L_88E3B;
    { const uint8_t a_ = (uint8_t)ebx; SETF(XK_SUB, a_, 2u, (uint8_t)(a_ - 2u), 8); }
    if (!ZF()) goto L_88E3B;
    SETF(XK_LOGIC, 0, 0, edx, 32);                                /* test edx,edx */
    if (ZF()) ecx = esi; else ecx = s->s1c;
    FW8(E - 4u, 1);                                               /* mov byte ptr [esp+18h],1 */
L_88D86:
    SETF(XK_SUB, ecx, 0xFFFFFFFFu, ecx + 1u, 32);                 /* cmp ecx,-1 */
    if (ZF()) goto L_88E3B;
    edx = FR32(E - 4u);                                           /* mov edx,[esp+18h]: the byte above and 3 older ones */
    eax = t0w;                                                    /* mov eax,[esp+20h] */
    ebx = s->bsp;                                                 /* mov ebx,[ebp+4] */
    esi = s->dir;                                                 /* mov esi,[ebp+14h] */
    edi = s->pt;                                                  /* mov edi,[ebp+10h] */
    PUSH(edx);
    { const uint32_t hit_ = edx;
      edx = s->s24;                                               /* mov edx,[ebp+24h] */
      PUSH(eax);
      eax = s->sa1;                                               /* mov eax,[ebp+0Ch] */
      PUSH(edx);
      const uint32_t pln_ = edx;
      edx = 0;
      LO16(edx, s->sword);                                        /* mov dx,[ebp+8] */
      PUSH(eax); PUSH(edx);
      PUSH(0x88DB6u);                                             /* call 000889E0h */
      NR_CALL_SAVE(X12_SAVE); nr_889e0(s, R, edx, eax, pln_, t0w, hit_); NR_CALL_LOAD(X12_LOAD); }
    SETF(XK_SUB, eax, 0xFFFFFFFFu, eax + 1u, 32);                 /* cmp eax,-1 */
    if (ZF()) goto L_88E33;
    edx = s->surfsa;                                              /* mov edx,[ebx+40h] */
    ecx = eax * 3u;
    ecx = edx + ecx * 4u;
    LO8(edx, nr_r8(m, ecx + 8u));                                 /* mov dl,[ecx+8] */
    SETF(XK_LOGIC, 0, 0, (uint8_t)edx & 2u, 8);
    if (ZF()) goto L_88DD3;
    SETF(XK_LOGIC, 0, 0, (uint8_t)s10 & 8u, 8);                   /* test byte ptr [esp+10h],8 */
    if (!ZF()) goto L_88E33;
L_88DD3:
    SETF(XK_LOGIC, 0, 0, (uint8_t)edx & 8u, 8);
    if (ZF()) goto L_88DDF;
    SETF(XK_LOGIC, 0, 0, (uint8_t)s10 & 0x10u, 8);                /* test byte ptr [esp+10h],10h */
    if (!ZF()) goto L_88E33;
L_88DDF:
    /* the hit: t, plane, surface, the surface's words */
    s->hits++;
    edx = s->rec;                                                 /* mov edx,[ebp+18h] */
    esi = t0w;                                                    /* mov esi,[esp+20h] */
    nr_w32(m, edx, esi); s->rt = esi;
    edx = s->bsp;                                                 /* mov edx,[ebp+4] */
    edi = s->planes;                                              /* mov edi,[edx+10h] */
    esi = s->s24;                                                 /* mov esi,[ebp+24h] */
    edx = s->rec;
    esi = SHL32(esi, 4u);
    { const uint32_t a_ = esi, b_ = edi, r_ = a_ + b_; SETF(XK_ADD, a_, b_, r_, 32); esi = r_; }   /* add esi,edi */
    nr_w32(m, edx + 4u, esi);
    edx = s->rec;
    nr_w32(m, edx + 8u, eax);
    edx = nr_r32(m, ecx);
    eax = s->rec;
    nr_w32(m, eax + 0xCu, edx);
    LO8(edx, nr_r8(m, ecx + 8u));
    eax = s->rec;
    nr_w8(m, eax + 0x10u, (uint8_t)edx);
    eax = s->rec;
    LO8(edx, nr_r8(m, ecx + 9u));
    nr_w8(m, eax + 0x11u, (uint8_t)edx);
    eax = s->rec;
    LO16(ecx, nr_r16(m, ecx + 0xAu));
    nr_w16(m, eax + 0x12u, (uint16_t)ecx);
    goto L_88E27;
L_88E33:
    LO8(ebx, s24);                                                /* mov bl,[esp+24h] */
    esi = s14;                                                    /* mov esi,[esp+14h] */
L_88E3B:
    SETF(XK_SUB, esi, 0xFFFFFFFFu, esi + 1u, 32);                 /* cmp esi,-1 */
    if (ZF()) goto L_88E70;
    eax = s->rec;                                                 /* mov eax,[ebp+18h] */
    ecx = s->rcount;                                              /* mov ecx,[eax+14h] */
    SETF(XK_SUB, ecx, 0x100u, ecx - 0x100u, 32);
    if (SF() == OF()) goto L_88E6A;
    nr_w32(m, eax + ecx * 4u + 0x18u, esi);                       /* the leaf list */
    eax = s->rec;
    { INC_CF(); const uint32_t v_ = s->rcount + 1u; nr_w32(m, eax + 0x14u, v_); s->rcount = v_; }   /* inc dword ptr [eax+14h] */
    edi = L->sv_edi;                                              /* pop edi */
    SW32(0x1Cu, esi); s->s1c = esi;                               /* mov [ebp+1Ch],esi */
    esi = L->sv_esi;                                              /* pop esi */
    SW8(0x20u, ebx); s->s20 = (uint8_t)ebx;                       /* mov [ebp+20h],bl */
    ebp = L->sv_ebp;                                              /* pop ebp */
    LO8(eax, 0);                                                  /* xor al,al */
    ebx = L->sv_ebx;                                              /* pop ebx */
    esp = E + 12u;
    goto ret;
L_88E6A:
    nr_w32(m, eax + 0x414u, esi);
L_88E70:
    SW32(0x1Cu, esi); s->s1c = esi;                               /* mov [ebp+1Ch],esi */
    SW8(0x20u, ebx); s->s20 = (uint8_t)ebx;                       /* mov [ebp+20h],bl */
    goto L_88E76;
    }
L_88E27:
    edi = L->sv_edi; esi = L->sv_esi; ebp = L->sv_ebp; LO8(eax, 1); ebx = L->sv_ebx;   /* pop edi/esi/ebp; mov al,1; pop ebx */
    esp = E + 12u;                                                /* add esp,0Ch; ret 8 */
    goto ret;
L_88E76:
    edi = L->sv_edi; esi = L->sv_esi; ebp = L->sv_ebp; LO8(eax, 0); ebx = L->sv_ebx;   /* pop edi/esi/ebp; xor al,al; pop ebx */
    esp = E + 12u;
    goto ret;

descend:
    /* a child: this level waits in lv[d] (esp at the return address just pushed; ecx = S, edx = the child) */
    L->site = site; L->ts = nr_f2u(ts);
    d++;
    if (site == NR_NEAR) t1w = nr_f2u(ts);                        /* [t0, t] */
    else if (site == NR_FAR) t0w = nr_f2u(ts);                    /* [t, t1] */
    goto call;
ret:
    /* a level returned (ret 8): its caller continues, or the cast does */
    if (d == 0) {
        (void)edi; (void)esi; (void)ebp; (void)ebx;
        s->sl[1] = x1; s->sl[2] = x2;
        NR_EXIT((void)0);
        return;
    }
    /* the caller level: every call returns to a flag write (test al,al), so of the record only the stale cells
     * matter from here (the constants end the other fields' live ranges at the child's exits) */
    fk = XK_LOGIC; fa = fb = fr = 0; fbits = 32; fcfo = fofo = 0;
    d--;
    L = &lv[d];
    E = L->E; t0w = L->t0w; t1w = L->t1w; ts = nr_u2f(L->ts); hf = L->hf;
    if (__builtin_expect(hf == NULL, 0)) nr_stk_at(m, &k, E - 0x34u);
    if (L->site == NR_NEAR) goto after_near;
    if (L->site == NR_FAR) goto after_far;
    goto after_one_side;
}

/* ---- f_00088E90: esp -> [ret][a0 word][a1][point][delta][t limit]; eax = flags, ecx = the result record, edx = BSP.
 * ret 14h. S = its frame F = E - 0x28. ------------------------------------------------------------------------ */
static void nr_88e90(nrs *restrict s, nrr *restrict R)
{
    NR_LOCALS;
    double x1;
    const uint32_t E = esp;
    NR_FRAME(0x34u, 0x18u);                                       /* its frame S, the pushes for 88B80, its arguments */
    const uint32_t F = E - 0x28u;
    esp = F;                                                      /* sub esp,28h */
    x1 = nr_rf(m, F + 0x3Cu);                                     /* fld [esp+3Ch]: depth 1 */
    FW32(F, eax); s->sflags = eax;                                /* mov [esp],eax */
    LO16(eax, nr_r16(m, F + 0x2Cu));                              /* mov ax,[esp+2Ch] */
    FCMP(x1, s->zero, 1);                                         /* fcomp: depth 0 */
    FW16(F + 8u, eax); s->sword = (uint16_t)eax;                  /* mov [esp+8],ax */
    eax = nr_r32(m, F + 0x34u);
    FW32(F + 4u, edx); s->bsp = edx;
    edx = nr_r32(m, F + 0x30u);
    FW32(F + 0x10u, eax); s->pt = eax;
    FNSTSW();
    FW32(F + 0xCu, edx); s->sa1 = edx;
    TEST_AH(5);
    edx = nr_r32(m, F + 0x38u);
    FW32(F + 0x14u, edx); s->dir = edx;
    FW32(F + 0x18u, ecx); s->rec = ecx;
    if (PF()) x1 = nr_rf(m, F + 0x3Cu);                           /* fld [esp+3Ch] */
    else x1 = s->zero;                                            /* fld [1F0A68] */
    s->rt = nr_f2u(nr_wf(m, ecx, x1));                            /* fstp [ecx]: depth 0 */
    eax = 0xFFFFFFFFu;                                            /* or eax,-1 */
    x1 = nr_rf(m, F + 0x3Cu);                                     /* depth 1 */
    FW32(F + 0x1Cu, eax); s->s1c = eax;
    FCMP(x1, s->zero, 1);                                         /* fcomp: depth 0 */
    FW32(F + 0x24u, eax); s->s24 = eax;
    edx = 0;                                                      /* xor edx,edx */
    nr_w32(m, ecx + 0x14u, edx); s->rcount = edx;
    FW8(F + 0x20u, edx); s->s20 = (uint8_t)edx;
    FNSTSW(); TEST_AH(5);
    if (PF()) {
        x1 = nr_rf(m, F + 0x3Cu);
        FCMP(x1, s->one, 1);                                      /* fcomp [1F0A78] */
        FNSTSW(); TEST_AH(0x41);
        if (!ZF()) { eax = nr_r32(m, F + 0x3Cu); FW32(F + 0x2Cu, eax); }
        else FW32(F + 0x2Cu, 0x3F800000u);
    } else FW32(F + 0x2Cu, edx);
    ecx = nr_r32(m, F + 0x2Cu);                                   /* mov ecx,[esp+2Ch] */
    PUSH(ecx); PUSH(edx);
    const uint32_t t1w = ecx, t0w = edx;
    edx = 0;                                                      /* xor edx,edx */
    ecx = esp + 8u;                                               /* lea ecx,[esp+8] */
    PUSH(0x88F3Fu);                                               /* call 00088B80h */
    /* 88B80 writes the whole flag record before it reads a flag: only the stale cells travel */
    (void)fk; (void)fa; (void)fb; (void)fr; (void)fbits; (void)fcfo; (void)fofo;
    s->sl[1] = x1; s->fcf = fcf; s->fof = fof; s->fsw = fsw; s->be = be;
    NR_REGS_OUT();
    nr_88b80(s, R, t0w, t1w);
    /* the cast's result: 88B80's eax/ecx/edx, whole flag record, status word, slots, back-edges; ebx/ebp/esi/edi are
     * 88E90's own (88B80 pops them; R's copies are whatever the last call inside wrote) */
    R->ebx = ebx; R->ebp = ebp; R->esi = esi; R->edi = edi;
    esp = R->esp + 0x28u;                                         /* add esp,28h */
    R->esp = esp + 24u;                                           /* ret 14h */
}

/* ---- modes, budget, counters ------------------------------------------------------------------------------ */
/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go at the end (xk_native_visibility.c). */
static inline void nr_budget(xctx *c, uint32_t backedges)
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
static inline uint64_t nr_ns(void) { return xk_os_monotonic_us() * 1000u; }
#else
#include <time.h>
static inline uint64_t nr_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
#endif

enum { NR_CALLS, NR_VERIFIED, NR_MISMATCHED, NR_DECLINED, NR_JOURNAL_FAIL, NR_NODES, NR_LEAVES, NR_REFS, NR_POLYS, NR_EDGES,
       NR_STEPS2, NR_HITS, NR_BACKEDGES, NR_DELEG, NR_MAXD, NR_FROM_1721B0, NR_FROM_1731D0, NR_FROM_OTHER, NR_TIMED_NATIVE,
       NR_TIMED_GUEST, NR_NAN_WORDS, NR_COUNTERS };
static unsigned nr_counter[NR_COUNTERS], nr_mismatch_total;
static uint64_t nr_native_ns, nr_guest_ns;
#define NR_ADD(i, v) __atomic_fetch_add(&nr_counter[i], (unsigned)(v), __ATOMIC_RELAXED)

static int nr_mode_value = -1, nr_detail;
static int nr_mode(void)
{
    int mode = __atomic_load_n(&nr_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_1721B0"); mode = e ? atoi(e) : XV_NATIVE_1721B0_DEFAULT;
        { const char *t = getenv("XV_NATIVE_1721B0_TIME"); nr_detail = mode == 1 || (t && atoi(t) != 0); }
        if (mode < 0 || mode > 2) mode = 0;
        int expected = -1;
        if (__atomic_compare_exchange_n(&nr_mode_value, &expected, mode, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            XK_LOG("[native-1721b0] f_00088E90 BSP segment cast (+88B80/889E0/17ADD0/86E20) under 1721B0/1731D0: %s\n",
                   mode == 2 ? "native" : mode == 1 ? "verify (native vs translation, guest result kept)" : "off");
        else mode = expected;
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_1721b0_force(int mode) { __atomic_store_n(&nr_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); nr_detail = 1; }
void xv_native_1721b0_detail(int on) { nr_detail = on; }   /* tests: the per-call counters off (mode 2 without timing) */
static int nr_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_1721B0_TIME"); on = e && atoi(e) != 0; }
    return on;
}

/* The layout the native relies on (else the translation runs): esp 4-aligned with 64 KB of room below and the
 * arguments below the top of the address space; W = [esp - 64 KB, esp + 0x18) holds every frame of the call (the
 * recursion is bounded at NR_MAXDEPTH levels of 0x28 bytes, 40 KB), S and 88E90's arguments. The result record (0x418
 * bytes: t, plane, surface, words, the leaf count and list, the overflow slot) lies outside W with its t inside one
 * page (the integer and the page-split float accesses to it then agree), and the BSP header, the
 * point and delta vectors and the image constants the native caches (0.0, 1.0, the axes table) outside W and the
 * record. The call's stores all land in W or the record, so no store of the call can reach a frame slot but its
 * owner's, S's words but 88B80's, the record's t and count but its own, or anything cached. */
static inline int nr_ovl(uint32_t a, uint32_t n, uint32_t b, uint32_t len) { return a < b + len && b < a + n; }
static int nr_layout(const xctx *c)
{
    const uint32_t E = c->r[4], rec = c->r[1], bsp = c->r[2];
    if ((E & 3u) || E < 0x20000u || E > 0xFFFFFF00u) return 0;
    const uint32_t lo = E - 0x10000u, wl = 0x10018u;
    if (rec > 0xFFFFFFFFu - 0x418u || nr_ovl(rec, 0x418u, lo, wl)) return 0;
    if ((rec & 0xFFFu) > 0xFFCu) return 0;   /* t (float stores and loads, integer store at a hit) within one page */
    const uint32_t pt = X_M32(E + 0xCu), dir = X_M32(E + 0x10u);
    const uint32_t ra[6] = { bsp, pt, dir, NR_AXES, NR_ZERO, NR_ONE }, rl[6] = { 0x5Cu, 12u, 12u, 0x18u, 4u, 4u };
    for (unsigned i = 0; i < 6; ++i)
        if (ra[i] > 0xFFFFFFFFu - rl[i] || nr_ovl(ra[i], rl[i], lo, wl) || nr_ovl(ra[i], rl[i], rec, 0x418u)) return 0;
    return 1;
}
/* what the call caches (nr_layout: no store of the call reaches it) */
static void nr_cache(nrs *s)
{
    const nr_mem *m = &s->m;
    const uint32_t b = s->bsp;
    s->nodes3 = nr_r32(m, b + 4u); s->planes = nr_r32(m, b + 0x10u); s->leavesa = nr_r32(m, b + 0x1Cu); s->refsa = nr_r32(m, b + 0x28u);
    s->nodes2a = nr_r32(m, b + 0x34u); s->surfsa = nr_r32(m, b + 0x40u); s->edgesa = nr_r32(m, b + 0x4Cu); s->vertsa = nr_r32(m, b + 0x58u);
    for (unsigned k = 0; k < 3; ++k) { s->P[k] = nr_rf(m, s->pt + 4u * k); s->D[k] = nr_rf(m, s->dir + 4u * k); }
    s->zero = nr_rf(m, NR_ZERO); s->one = nr_rf(m, NR_ONE);
    for (unsigned i = 0; i < 12; ++i) s->axes[i] = (int16_t)nr_r16(m, NR_AXES + 2u * i);
}
/* The whole cast from the state of `call 00088E90h` (esp at the pushed return address) to after its `ret 14h`. */
static void nr_run(xctx *c, nrs *s, int journal)
{
    s->m.ram = g_xram; s->m.pt = X_PT; s->m.jon = journal;
    s->fk = c->f_kind; s->fa = c->f_op1; s->fb = c->f_op2; s->fr = c->f_res; s->fbits = c->f_bits;
    s->fcfo = c->f_cf_override; s->fcf = c->f_cf; s->fofo = c->f_of_override; s->fof = c->f_of;
    s->fsp0 = c->fsp; s->fsw = c->fsw;
    for (unsigned d = 1; d <= 4; ++d) s->sl[d] = c->st[(s->fsp0 - d) & 7u];
    s->be = 0; s->maxdepth = 0; s->jfail = 0; s->c = c;
    s->nodes = s->leaves = s->refs = s->polys = s->edges = s->steps2 = s->hits = s->deleg = 0;
    const uint32_t E = c->r[4];
    s->F = E - 0x28u;
    {   const nr_mem *m = &s->m;
        s->hS = (s->F & 0xFFFu) + 0x28u <= 0x1000u ? NR_P(s->F) : NULL;
        s->bsp = c->r[2]; s->rec = c->r[1]; s->pt = nr_r32(m, E + 0xCu); s->dir = nr_r32(m, E + 0x10u); }
    nr_cache(s);
    nrr R = { c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7] };
    nr_88e90(s, &R);
    c->r[0] = R.eax; c->r[1] = R.ecx; c->r[2] = R.edx; c->r[3] = R.ebx; c->r[4] = R.esp; c->r[5] = R.ebp; c->r[6] = R.esi; c->r[7] = R.edi;
    c->f_kind = s->fk; c->f_op1 = s->fa; c->f_op2 = s->fb; c->f_res = s->fr; c->f_bits = s->fbits;
    c->f_cf_override = s->fcfo; c->f_cf = s->fcf; c->f_of_override = s->fofo; c->f_of = s->fof;
    c->fsp = s->fsp0; c->fsw = s->fsw;
    for (unsigned d = 1; d <= 4; ++d) c->st[(s->fsp0 - d) & 7u] = s->sl[d];
}
static void nr_count(const nrs *s, uint32_t ret)
{
    NR_ADD(NR_CALLS, 1);
    if (!nr_detail) return;
    NR_ADD(NR_NODES, s->nodes); NR_ADD(NR_LEAVES, s->leaves); NR_ADD(NR_REFS, s->refs); NR_ADD(NR_POLYS, s->polys);
    NR_ADD(NR_EDGES, s->edges); NR_ADD(NR_STEPS2, s->steps2); NR_ADD(NR_HITS, s->hits); NR_ADD(NR_BACKEDGES, s->be);
    NR_ADD(NR_DELEG, s->deleg);
    NR_ADD(ret == 0x172295u ? NR_FROM_1721B0 : ret == 0x1732C7u ? NR_FROM_1731D0 : NR_FROM_OTHER, 1);
    unsigned d = s->maxdepth, cur = __atomic_load_n(&nr_counter[NR_MAXD], __ATOMIC_RELAXED);
    while (d > cur && !__atomic_compare_exchange_n(&nr_counter[NR_MAXD], &cur, d, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

/* ---- verify mode ------------------------------------------------------------------------------------------- */
enum { NR_REGIONS = 3, NR_REGION_CAP = 0x40000u };
static int nr_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
/* A float NaN in both results: which NaN payload an operation propagates is the host compiler's operand order
 * (the guest bodies built -O0/-O2 already differ); such floats only reach frames, x87 slots and the result t. */
static inline int nr_nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static void nr_report_mismatch(const char *what, uint32_t a, uint32_t b, const nrs *s)
{
    if (__atomic_add_fetch(&nr_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-1721b0] MISMATCH %s native %08X guest %08X (cast: %u nodes, %u leaves, %u refs, %u polygons)\n",
               what, a, b, s->nodes, s->leaves, s->refs, s->polys);
}

void f_00088E90(xctx *c);
static __thread int nr_in_guest;   /* the hook's own call of the translation: the hook stands aside */
static void nr_guest(xctx *c) { nr_in_guest = 1; f_00088E90(c); nr_in_guest = 0; }

static void nr_verify(xctx *c, int timed)
{
    const xctx before = *c;
    nrs s;
    nr_j.n = 0; nr_j.overflow = 0;
    const uint64_t t0 = timed ? nr_ns() : 0;
    nr_run(c, &s, 1);
    if (timed) { __atomic_fetch_add(&nr_native_ns, nr_ns() - t0, __ATOMIC_RELAXED); NR_ADD(NR_TIMED_NATIVE, 1); }
    const xctx native = *c;
    const uint32_t ret = X_M32(before.r[4]);
    /* regions the guest may write: the stack from below the deepest native write to 88E90's arguments, and the
     * result record (ecx at entry, 0x418 bytes). Everything the native wrote is journaled too. */
    uint32_t low = before.r[4] - 0x100u;
    for (unsigned i = 0; i < nr_j.n; ++i) {
        const uint32_t a = nr_j.e[i].addr;
        if (a < before.r[4] + 0x18u && before.r[4] - a < 0x100000u && a < low) low = a;
    }
    uint32_t reg_addr[NR_REGIONS], reg_len[NR_REGIONS]; unsigned nreg = 0;
    reg_addr[nreg] = low - 0x100u; reg_len[nreg] = before.r[4] + 0x18u - (low - 0x100u);
    if (reg_len[nreg] > NR_REGION_CAP) reg_len[nreg] = NR_REGION_CAP;
    nreg++;
    reg_addr[nreg] = before.r[1]; reg_len[nreg] = 0x418u; nreg++;
    uint32_t total = 0;
    for (unsigned i = 0; i < nreg; ++i) total += reg_len[i];
    uint8_t *img = malloc(total), *cur = malloc(total);
    const unsigned nj = nr_j.n;
    nr_jent *j = malloc((nj ? nj : 1) * sizeof *j);
    if (!img || !cur || !j || nr_j.overflow || s.jfail) {
        /* cannot verify this call: keep the native result (it is complete), account for its budget */
        NR_ADD(NR_JOURNAL_FAIL, 1);
        free(img); free(cur); free(j);
        nr_budget(c, s.be); nr_count(&s, ret);
        return;
    }
    for (unsigned i = 0, o = 0; i < nreg; o += reg_len[i], ++i) x_guest_read_pages(img + o, reg_addr[i], reg_len[i]);
    const nr_mem *m = &s.m;
    for (unsigned i = 0; i < nj; ++i) {
        j[i] = nr_j.e[i]; memcpy(j[i].now, NR_P(j[i].addr), j[i].size);
        x_guest_read_pages(&j[i].word, j[i].addr & ~3u, 4);
    }
    for (unsigned i = nj; i-- > 0;) memcpy(NR_P(j[i].addr), j[i].old, j[i].size);
    nr_count(&s, ret);
    /* the translation on the same state, unbounded budget */
    *c = before; c->preempt = 1 << 30;
    const uint64_t t1 = timed ? nr_ns() : 0;
    nr_guest(c);
    if (timed) { __atomic_fetch_add(&nr_guest_ns, nr_ns() - t1, __ATOMIC_RELAXED); NR_ADD(NR_TIMED_GUEST, 1); }
    const uint32_t guest_backedges = (uint32_t)((1 << 30) - c->preempt);
    unsigned bad = 0, nan_words = 0;
    const xctx *n = &native;
#define NR_CMP(what, a, b) do { if ((a) != (b)) { bad++; nr_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &s); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) NR_CMP(rn[i], n->r[i], c->r[i]);
    NR_CMP("backedges", s.be, guest_backedges);
    NR_CMP("fsp", n->fsp, c->fsp); NR_CMP("fcw", n->fcw, c->fcw); NR_CMP("fsw", n->fsw, c->fsw); NR_CMP("df", n->df, c->df);
    NR_CMP("f_kind", n->f_kind, c->f_kind); NR_CMP("f_op1", n->f_op1, c->f_op1); NR_CMP("f_op2", n->f_op2, c->f_op2);
    NR_CMP("f_res", n->f_res, c->f_res); NR_CMP("f_bits", n->f_bits, c->f_bits);
    NR_CMP("f_cf_override", n->f_cf_override, c->f_cf_override); NR_CMP("f_of_override", n->f_of_override, c->f_of_override);
    NR_CMP("f_cf", n->f_cf, c->f_cf); NR_CMP("f_of", n->f_of, c->f_of);
    NR_CMP("fs_base", n->fs_base, c->fs_base); NR_CMP("scratch", n->scratch, c->scratch); NR_CMP("eip_hint", n->eip_hint, c->eip_hint);
    for (unsigned i = 0; i < 8; ++i)
        if (!nr_same_double(n->st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &n->st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st[%u] fsp %u (lo word)", i, c->fsp); bad++; nr_report_mismatch(w, (uint32_t)a, (uint32_t)b, &s);
        }
    if (memcmp(n->xmm, c->xmm, sizeof c->xmm)) { bad++; nr_report_mismatch("xmm", 0, 1, &s); }
    if (memcmp(n->mm, c->mm, sizeof c->mm)) { bad++; nr_report_mismatch("mm", 0, 1, &s); }
    /* every byte the native wrote holds the same final value after the guest (NaN words: any payload) */
    for (unsigned i = 0; i < nj; ++i) {
        uint8_t now[4]; memcpy(now, NR_P(j[i].addr), j[i].size);
        if (memcmp(now, j[i].now, j[i].size)) {
            uint32_t gw; x_guest_read_pages(&gw, j[i].addr & ~3u, 4);
            if (nr_nan32(gw) && nr_nan32(j[i].word)) { nan_words++; continue; }
            uint32_t a = 0, b = 0; memcpy(&a, j[i].now, j[i].size); memcpy(&b, now, j[i].size);
            char w[40]; snprintf(w, sizeof w, "write@%08X", j[i].addr); bad++; nr_report_mismatch(w, a, b, &s);
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
                if (nr_nan32(gw) && nr_nan32(nw)) { nan_words++; k = w0 + 3u; continue; }
            }
            char w[48]; snprintf(w, sizeof w, "region%u@%08X (byte)", i, a);
            bad++; nr_report_mismatch(w, img[o + k], cur[o + k], &s); break;
        }
    }
#undef NR_CMP
    c->preempt = before.preempt;
    nr_budget(c, guest_backedges);
    NR_ADD(NR_VERIFIED, 1); NR_ADD(NR_NAN_WORDS, nan_words);
    if (bad) NR_ADD(NR_MISMATCHED, 1);
    free(img); free(cur); free(j);
}

#if !defined(__vita__) && !defined(XV_NATIVE_1721B0_TEST)
/* Host harness only: XV_NATIVE_1721B0_CAPTURE=<file>[:count[:skip]] writes the guest arena and page table once, then
 * count (default 2000) entry states the native accepts (xctx + the guest stack from esp - 64 KB to esp + 64 KB), for
 * tools/tests/native_1721b0.c --replay (realistic speed and exactness offline). */
extern uint32_t xk_mem_arena_size(void);
static void nr_capture(const xctx *c)
{
    static int state = -1; static FILE *f; static unsigned n, max, skip; static char path[512];
    if (state == 0) return;
    if (state < 0) {
        const char *e = getenv("XV_NATIVE_1721B0_CAPTURE"); state = 0;
        if (!e) return;
        snprintf(path, sizeof path, "%s", e);   /* <file>[:count[:skip]] */
        max = 2000; skip = 0;
        char *c1 = strchr(path, ':');
        if (c1) { *c1 = 0; max = (unsigned)atoi(c1 + 1); char *c2 = strchr(c1 + 1, ':'); if (c2) skip = (unsigned)atoi(c2 + 1); }
        state = 2;
    }
    if (state == 2) {
        if (skip) { skip--; return; }
        if (!(f = fopen(path, "wb"))) { state = 0; return; }
        state = 1;
        const uint32_t hdr[4] = { 0x3142374Eu, xk_mem_arena_size(), 1u << 20, 0x20000u };
        fwrite(hdr, sizeof hdr, 1, f); fwrite(g_xpt, 4, 1u << 20, f); fwrite(g_xram, 1, hdr[1], f);
        XK_LOG("[native-1721b0] capture: arena %u bytes to %s, %u casts\n", hdr[1], path, max);
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
    if (++n == max) { fclose(f); f = NULL; state = 0; XK_LOG("[native-1721b0] capture: %u casts written\n", n); }
}
#endif

/* ---- the hook: first thing in f_00088E90 (tools/patch_native_1721b0_hooks.py). Nonzero: handled (the translated
 * body returns at once). ------------------------------------------------------------------------------------- */
int xv_native_1721b0_ray(xctx *c)
{
    if (nr_in_guest) return 0;
    const int mode = nr_mode();
    const int timed = nr_timing();
#if !defined(__vita__) && !defined(XV_NATIVE_1721B0_TEST)
    {   /* one capturing thread at a time (the owner, object workers and the scene helper all cast) */
        static int busy;
        if (nr_layout(c) && !__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE)) { nr_capture(c); __atomic_store_n(&busy, 0, __ATOMIC_RELEASE); }
    }
#endif
    if (!mode) {
        if (!timed) return 0;
        const uint64_t t0 = nr_ns();
        nr_guest(c);
        __atomic_fetch_add(&nr_guest_ns, nr_ns() - t0, __ATOMIC_RELAXED); NR_ADD(NR_TIMED_GUEST, 1);
        return 1;
    }
    if (!nr_layout(c)) { NR_ADD(NR_DECLINED, 1); return 0; }
    if (__builtin_expect(!nr_lv, 0) && !(nr_lv = malloc(NR_MAXDEPTH * sizeof *nr_lv))) { NR_ADD(NR_DECLINED, 1); return 0; }
    if (mode == 1) { nr_verify(c, timed); return 1; }
    nrs s;
    const uint32_t ret = nr_detail ? X_M32(c->r[4]) : 0;
    const uint64_t t0 = timed ? nr_ns() : 0;
    nr_run(c, &s, 0);
    if (timed) { __atomic_fetch_add(&nr_native_ns, nr_ns() - t0, __ATOMIC_RELAXED); NR_ADD(NR_TIMED_NATIVE, 1); }
    nr_budget(c, s.be); nr_count(&s, ret);
    return 1;
}

/* ---- the 60-frame report (recomp/kernel/xd3d.c's weak report chain) ----------------------------------------- */
void xv_native_1721b0_report(unsigned frames)
{
    if (__atomic_load_n(&nr_mode_value, __ATOMIC_RELAXED) < 0) return;   /* never called yet */
    unsigned v[NR_COUNTERS];
    for (unsigned i = 0; i < NR_COUNTERS; ++i) v[i] = __atomic_exchange_n(&nr_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t nn = __atomic_exchange_n(&nr_native_ns, 0u, __ATOMIC_RELAXED), gn = __atomic_exchange_n(&nr_guest_ns, 0u, __ATOMIC_RELAXED);
    if (!v[NR_CALLS] && !v[NR_TIMED_GUEST] && !v[NR_DECLINED]) return;
    char t[120] = "";
    if (v[NR_TIMED_NATIVE] || v[NR_TIMED_GUEST])
        snprintf(t, sizeof t, "; us/call native %.3f guest %.3f (%u timed)", v[NR_TIMED_NATIVE] ? nn / 1000.0 / v[NR_TIMED_NATIVE] : 0.0,
                 v[NR_TIMED_GUEST] ? gn / 1000.0 / v[NR_TIMED_GUEST] : 0.0, v[NR_TIMED_GUEST]);
    XK_LOG("[native-1721b0] %u frames: calls %u verified %u mismatched %u (total mismatches %u) declined %u journal-fail %u; "
           "from 1721B0 %u 1731D0 %u other %u; nodes %u leaves %u refs %u polygons %u edges %u bsp2d-steps %u hits %u back-edges %u "
           "delegated %u max-depth %u nan-words %u%s\n",
           frames, v[NR_CALLS], v[NR_VERIFIED], v[NR_MISMATCHED], __atomic_load_n(&nr_mismatch_total, __ATOMIC_RELAXED), v[NR_DECLINED],
           v[NR_JOURNAL_FAIL], v[NR_FROM_1721B0], v[NR_FROM_1731D0], v[NR_FROM_OTHER], v[NR_NODES], v[NR_LEAVES], v[NR_REFS],
           v[NR_POLYS], v[NR_EDGES], v[NR_STEPS2], v[NR_HITS], v[NR_BACKEDGES], v[NR_DELEG], v[NR_MAXD], v[NR_NAN_WORDS], t);
}
