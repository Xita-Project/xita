/* xk_native_visibility.c - native Halo CE (Xbox 3925) BSP subcluster visibility pass: f_00052E10.
 *
 * f_000539C0 (the scene's cluster-visibility pass, once per frame) clears the cluster and surface bitmaps, runs the
 * portal traversal f_00053540 (guest, unchanged), and then calls f_00052E10(root) when cluster 0 has subclusters.
 * f_00052E10 walks the visible-cluster list (int16 count at 0x30BE0C, 0x1A0-byte entries at 0x2FEE0C: +0 int16
 * cluster index, +0x14 the entry's clipped view frustum), tests every subcluster AABB of each visible cluster
 * against that frustum (the global view frustum 0x2FEBE4 when [0x39CC15] != 0 or [0x2FEDC4] == -1) with
 * f_0005C300 (enclosing-box reject, then 8 corners x 4 planes, x87 doubles against the float at 0x1F0A68), and for
 * every visible subcluster ORs its surface indices into the surface bitmap 0x30BE10, counting new bits in the
 * int16 at 0x38BE10 (no new bit once it reaches 0x4000). That is the ~4.5 ms of "539C0 self" on the Vita.
 *
 * The native replaces f_00052E10 and its only callee f_0005C300 (leaf: no calls, no HLE). Exact by construction:
 *  - same memory reads/writes in the same order where they can interact (bitmap words, count, stack slots), same
 *    guest-address translation (the thread's page table, X_IMG rules of the build), same float semantics (float
 *    loads widened to double, the guest's operand order per plane, no contraction: -ffp-contract=off);
 *  - every register the guest leaves (eax/ecx/edx exit values included), esp (+8, ret 4), the canonical lazy-flag
 *    record of the last compare, the x87 stack pointer, status word and the two scratch slots f_0005C300 wrote;
 *  - the dead stack below esp as the guest leaves it (52E10 locals and pushes, the last f_0005C300 frame);
 *  - the back-edge budget (c->preempt) decremented by exactly the guest's back-edge count, with xv_preempt()
 *    called the same number of times, but at the end of the pass instead of mid-loop (scheduling point only;
 *    on the scene helper under the overlap xv_preempt never yields).
 * Assumptions, checked in verify mode: tag data (BSP clusters, subclusters, surface lists) does not alias the
 * guest stack or the bitmap/count; a surface index outside [0, 0x400000) would write outside the bitmap: the
 * native then writes exactly the same word and reloads everything it caches (counted as "oob").
 * Not reproduced: the stale lazy-flag carry/overflow cells (f_cf/f_of with overrides 0 are unobservable) and
 * NaN payload bits of the dead x87 scratch slots for NaN inputs (which NaN an addition propagates is the host
 * compiler's operand order: the guest body built -O0 and -O2 already disagrees; map data has no NaNs).
 *
 * XV_NATIVE_VISIBILITY build flag (hook tools/patch_native_visibility_hooks.py at the entry of f_00052E10);
 * env XV_NATIVE_VISIBILITY: 0 off (default XV_NATIVE_VISIBILITY_DEFAULT), 1 verify (native then guest on the same
 * state, compare everything, keep the guest result), 2 native. XV_NATIVE_VISIBILITY_TIME=1 times the pass (both
 * paths in verify mode). Counters: [native-visibility] every 60 frames. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_NATIVE_VISIBILITY_DEFAULT
#define XV_NATIVE_VISIBILITY_DEFAULT 0
#endif

enum {
    NV_BITS = 0x30BE10u, NV_BITS_BYTES = 0x80000u, NV_COUNT = 0x38BE10u, NV_VIEWS = 0x30BE0Cu,
    NV_ENTRIES = 0x2FEE0Cu, NV_ENTRY = 0x1A0u, NV_GLOBAL_FRUSTUM = 0x2FEBE4u, NV_PVS_FLAG = 0x39CC15u,
    NV_CUR_CLUSTER = 0x2FEDC4u, NV_EPS = 0x1F0A68u, NV_CAP = 0x4000, NV_RET_BOUNDS = 0x52EC6u,
};

/* Guest memory exactly as the generated code addresses it: one page-table translation per access (a 4-byte
 * integer access at a page end reads on in the same host page, like X_M32), float loads/stores split across pages
 * like x87_load_f32/x87_store_f32 (x_guest_read/x_guest_write). */
typedef struct { uint8_t *ram; const uint32_t *pt; uint8_t *img; } nv_mem;
#define NV_P(a) (m->ram + m->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define NV_I(a) NV_P(a)                       /* render view: image globals translate like everything else */
#else
#define NV_I(a) (m->img + (uint32_t)(a))
#endif
static inline uint32_t nv_r32(const nv_mem *m, uint32_t a) { uint32_t v; memcpy(&v, NV_P(a), 4); return v; }
static inline uint16_t nv_r16(const nv_mem *m, uint32_t a) { uint16_t v; memcpy(&v, NV_P(a), 2); return v; }
static inline void nv_w32(const nv_mem *m, uint32_t a, uint32_t v) { memcpy(NV_P(a), &v, 4); }
static inline uint32_t nv_i32(const nv_mem *m, uint32_t a) { uint32_t v; memcpy(&v, NV_I(a), 4); return v; }
static inline uint16_t nv_i16(const nv_mem *m, uint32_t a) { uint16_t v; memcpy(&v, NV_I(a), 2); return v; }
static inline uint8_t nv_i8(const nv_mem *m, uint32_t a) { return *(uint8_t *)NV_I(a); }
static inline void nv_wi16(const nv_mem *m, uint32_t a, uint16_t v) { memcpy(NV_I(a), &v, 2); }
static inline double nv_f32(const nv_mem *m, uint32_t a)
{
    float v;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(&v, NV_P(a), 4); else x_guest_read_pages(&v, a, 4);
    return (double)v;
}
static inline void nv_wf32(const nv_mem *m, uint32_t a, double d)
{
    float v = (float)d;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(NV_P(a), &v, 4); else x_guest_write_pages(a, &v, 4);
}

/* f_0005C300's inputs: 4 planes (n0 n1 n2 d at +0x78/+0x88/+0x98/+0xA8) and the enclosing box (+0x128..+0x13C). */
typedef struct { uint32_t at; double pl[16], enc[6]; } nv_frustum;
static void nv_load_frustum(const nv_mem *m, uint32_t f, nv_frustum *fr)
{
    fr->at = f;
    for (unsigned i = 0; i < 16; ++i) fr->pl[i] = nv_f32(m, f + 0x78u + 4u * i);
    for (unsigned i = 0; i < 6; ++i) fr->enc[i] = nv_f32(m, f + 0x128u + 4u * i);
}
/* Plane dot products in the guest's instruction order (products: pushed value * memory operand; faddp: earlier +
 * later; fsub: - d). Outcode bits per plane: 1, 2, 8, 4 when the result is > eps (ordered). */
static inline double nv_dot0(const double *p, double x, double y, double z) { return ((x * p[0] + y * p[1]) + p[2] * z) - p[3]; }
static inline double nv_dot1(const double *p, double x, double y, double z) { return ((p[4] * x + p[6] * z) + y * p[5]) - p[7]; }
static inline double nv_dot2(const double *p, double x, double y, double z) { return ((p[10] * z + y * p[9]) + p[8] * x) - p[11]; }
static inline double nv_dot3(const double *p, double x, double y, double z) { return ((p[14] * z + y * p[13]) + p[12] * x) - p[15]; }
static inline unsigned nv_outcode(const double *p, double x, double y, double z, double eps)
{
    return (nv_dot0(p, x, y, z) > eps ? 1u : 0u) | (nv_dot1(p, x, y, z) > eps ? 2u : 0u) |
           (nv_dot2(p, x, y, z) > eps ? 8u : 0u) | (nv_dot3(p, x, y, z) > eps ? 4u : 0u);
}
static inline uint16_t nv_cc(double a, double b)    /* x87_compare's condition codes */
{
    return (a != a || b != b) ? 0x4500 : (a < b) ? 0x0100 : (a == b) ? 0x4000 : 0;
}
/* Enclosing-box reject (the first six compares): index of the rejecting test, or 6 when all pass. */
static const uint8_t nv_enc_index[6] = { 1, 3, 5, 0, 2, 4 };
static inline unsigned nv_reject(const nv_frustum *fr, const double *b)
{
    if (fr->enc[1] < b[0]) return 0;
    if (fr->enc[3] < b[2]) return 1;
    if (fr->enc[5] < b[4]) return 2;
    if (fr->enc[0] > b[1]) return 3;
    if (fr->enc[2] > b[3]) return 4;
    if (fr->enc[4] > b[5]) return 5;
    return 6;
}
/* The corner coordinates f_0005C300 stores (fst dword) and reloads (fld dword): the box floats round-tripped. */
static inline void nv_corner_values(const nv_mem *m, uint32_t sub, double *raw, double *cv)
{
    for (unsigned i = 0; i < 6; ++i) { raw[i] = nv_f32(m, sub + 4u * i); float s = (float)raw[i]; cv[i] = (double)s; }
}
/* Classification (f_0005C300 with arg 0): 0 outside, 1 partial, 2 inside. */
static unsigned nv_classify(const nv_frustum *fr, const double *cv, double eps)
{
    unsigned any = 0, all = 0x3F;
    for (unsigned k = 0; k < 8; ++k) {
        unsigned o = nv_outcode(fr->pl, cv[k & 1], cv[2 + ((k >> 1) & 1)], cv[4 + ((k >> 2) & 1)], eps);
        all &= o; any |= o;
        if (any && !all) return 1;               /* the OR can only grow and the AND only shrink */
    }
    return !any ? 2u : all ? 0u : 1u;
}

typedef struct {                  /* one pass: exit state and dead-state provenance */
    uint32_t eax, ecx, edx;
    uint16_t fl_op1, fl_op2;      /* the last compare (XK_SUB, 16-bit) */
    uint32_t backedges;
    unsigned calls, full_calls, visible, published, oob;
    int last_call;                /* -1 none, 0..5 enclosing test that rejected, 6 full path */
    uint32_t last_f, last_sub;
    int any_full; uint32_t full_f, full_sub, full_ebx, full_ebp, full_esi;
} nv_pass;

static void nv_run(xctx *c, nv_pass *o)
{
    nv_mem mm = { g_xram, X_PT, X_IMG_BASE }; const nv_mem *m = &mm;
    const uint32_t E = c->r[4];
    const uint32_t B0 = c->r[3], P0 = c->r[5], S0 = c->r[6], I0 = c->r[7];
    uint32_t ecx = c->r[1], edx = c->r[2], ebp = P0, esi = S0, icnt = 0, backedges = 0;
    memset(o, 0, sizeof *o); o->last_call = -1;
    /* 52E10: sub esp,0Ch; xor eax,eax; cmp [30BE0C],ax; mov [esp+8],eax; jle */
    uint16_t views = nv_i16(m, NV_VIEWS);
    uint16_t fl1 = views, fl2 = 0;
    nv_w32(m, E - 4u, 0);
    if ((int16_t)views > 0) {
        nv_w32(m, E - 0x10u, B0); nv_w32(m, E - 0x14u, P0); nv_w32(m, E - 0x18u, S0); nv_w32(m, E - 0x1Cu, I0);
        uint16_t count = nv_i16(m, NV_COUNT);
        double eps = nv_f32(m, NV_EPS);
        nv_frustum fr; fr.at = 0; int have_fr = 0;
        for (;;) {                                               /* L_52E30 */
            fl1 = count; fl2 = NV_CAP;
            if ((int16_t)count >= NV_CAP) break;
            if (o->oob) icnt = nv_r32(m, E - 4u);                   /* locals re-read only if a stray write may have hit them */
            uint32_t root = nv_r32(m, E + 4u), clusters = nv_r32(m, root + 0x138u);
            uint8_t pvs = nv_i8(m, NV_PVS_FLAG);
            uint32_t entry = (uint32_t)((int32_t)(int16_t)icnt * (int32_t)NV_ENTRY) + NV_ENTRIES;
            uint32_t cluster = (uint32_t)((int32_t)(int16_t)nv_r16(m, entry) * 0x68) + clusters;
            uint32_t f = (!pvs && nv_i32(m, NV_CUR_CLUSTER) != 0xFFFFFFFFu) ? entry + 0x14u : NV_GLOBAL_FRUSTUM;
            nv_w32(m, E - 0xCu, f);
            edx = clusters; ecx = nv_r32(m, cluster + 0x34u);
            nv_w32(m, E - 8u, 0);
            uint32_t jcnt = 0;
            if ((int32_t)ecx > 0) {
                for (;;) {                                       /* L_52EA0 */
                    fl1 = count; fl2 = NV_CAP;
                    if ((int16_t)count >= NV_CAP) break;
                    if (o->oob) { f = nv_r32(m, E - 0xCu); jcnt = nv_r32(m, E - 8u); }
                    uint32_t sub = nv_r32(m, cluster + 0x38u) + (uint32_t)((int32_t)(int16_t)jcnt * 9) * 4u;
                    if (!have_fr || fr.at != f) { nv_load_frustum(m, f, &fr); have_fr = 1; }
                    double raw[6], cv[6];
                    nv_corner_values(m, sub, raw, cv);
                    unsigned t = nv_reject(&fr, raw), cls = 0;
                    o->calls++; o->last_call = (int)t; o->last_f = f; o->last_sub = sub;
                    if (t == 6) {
                        cls = nv_classify(&fr, cv, eps);
                        backedges += 7; o->full_calls++;
                        o->any_full = 1; o->full_f = f; o->full_sub = sub; o->full_ebx = cluster; o->full_ebp = ebp; o->full_esi = esi;
                    }
                    if (cls) {                                   /* L_52ECB */
                        uint32_t n = nv_r32(m, sub + 0x18u);
                        esi = nv_r32(m, sub + 0x1Cu); ebp = 0; o->visible++;
                        if ((int32_t)n > 0) {
                            for (;;) {                           /* L_52ED7 */
                                uint32_t s = nv_r32(m, esi);
                                uint32_t w = (uint32_t)((int32_t)s >> 5) * 4u + NV_BITS, bit = 1u << (s & 31u);
                                uint32_t bits = nv_r32(m, w);
                                if (!(bits & bit)) {
                                    if ((int16_t)count >= NV_CAP) { fl1 = count; fl2 = NV_CAP; break; }   /* -> L_52F19 */
                                    nv_w32(m, w, bits | bit);
                                    if (__builtin_expect(w - NV_BITS >= NV_BITS_BYTES, 0)) {
                                        /* outside the bitmap: the word may be anything we cached */
                                        o->oob++; count = nv_i16(m, NV_COUNT); eps = nv_f32(m, NV_EPS); have_fr = 0;
                                    }
                                    count = (uint16_t)(count + 1u); nv_wi16(m, NV_COUNT, count); o->published++;
                                    if (!have_fr) { nv_load_frustum(m, f, &fr); have_fr = 1; }
                                }
                                n = nv_r32(m, sub + 0x18u); esi += 4u; ebp++;
                                if ((int32_t)(int16_t)ebp < (int32_t)n) { backedges++; continue; }
                                break;
                            }
                        }
                    }
                    if (o->oob) jcnt = nv_r32(m, E - 8u);
                    jcnt++; nv_w32(m, E - 8u, jcnt);                /* L_52F19 */
                    ecx = nv_r32(m, cluster + 0x34u); edx = (uint32_t)(int32_t)(int16_t)jcnt;
                    if ((int32_t)edx < (int32_t)ecx) { backedges++; continue; }
                    break;
                }
            }
            if (o->oob) icnt = nv_r32(m, E - 4u);
            icnt++; nv_w32(m, E - 4u, icnt);                        /* L_52F30 */
            views = nv_i16(m, NV_VIEWS);
            fl1 = (uint16_t)icnt; fl2 = views;
            if ((int16_t)icnt < (int16_t)views) { backedges++; continue; }
            break;
        }
    }
    o->eax = icnt; o->ecx = ecx; o->edx = edx; o->fl_op1 = fl1; o->fl_op2 = fl2; o->backedges = backedges;
}

/* Registers, flags, x87 and the dead stack as the guest leaves them. */
static void nv_finish(xctx *c, const nv_pass *o)
{
    nv_mem mm = { g_xram, X_PT, X_IMG_BASE }; const nv_mem *m = &mm;
    const uint32_t E = c->r[4];
    if (o->calls) { nv_w32(m, E - 0x20u, 0); nv_w32(m, E - 0x24u, NV_RET_BOUNDS); }
    const unsigned t1 = (c->fsp - 1u) & 7u, t2 = (c->fsp - 2u) & 7u;
    if (o->any_full) {            /* the last full-path f_0005C300 frame: S = its esp after sub esp,64h */
        const uint32_t S = E - 0x88u;
        double raw[6], cv[6]; nv_corner_values(m, o->full_sub, raw, cv);
        nv_w32(m, S - 4u, o->full_ebx); nv_w32(m, S - 8u, o->full_ebp); nv_w32(m, S - 0xCu, o->full_esi);
        static const uint8_t xs[8] = { 0x04, 0x10, 0x1C, 0x28, 0x34, 0x40, 0x4C, 0x58 };
        for (unsigned k = 0; k < 8; ++k) {
            nv_wf32(m, S + xs[k], raw[k & 1]); nv_wf32(m, S + xs[k] + 4u, raw[2 + ((k >> 1) & 1)]);
            nv_wf32(m, S + xs[k] + 8u, raw[4 + ((k >> 2) & 1)]);
        }
        nv_w32(m, S, 0);           /* the corner loop counter, 8 -> 0 */
        nv_frustum fr; nv_load_frustum(m, o->full_f, &fr);
        c->st[t2] = fr.pl[12] * cv[1];                   /* plane 3: fld [A8]; fmul x (corner 7: xhi) */
    }
    if (o->last_call >= 0) {
        nv_frustum fr; nv_load_frustum(m, o->last_f, &fr);
        uint16_t cc;
        if (o->last_call == 6) {
            double raw[6], cv[6]; nv_corner_values(m, o->last_sub, raw, cv);
            double d = nv_dot3(fr.pl, cv[1], cv[3], cv[5]);
            c->st[t1] = d; cc = nv_cc(d, nv_f32(m, NV_EPS));
        } else {
            double e = fr.enc[nv_enc_index[o->last_call]];
            c->st[t1] = e; cc = o->last_call < 3 ? 0x0100 : 0;   /* the rejecting test: a < b, or a > b */
        }
        c->fsw = (uint16_t)((c->fsw & ~0x4700u) | cc | (t1 << 11));
    }
    c->r[0] = o->eax; c->r[1] = o->ecx; c->r[2] = o->edx;
    X_FLAGS(XK_SUB, o->fl_op1, o->fl_op2, (uint16_t)(o->fl_op1 - o->fl_op2), 16);
    c->r[4] = E + 8u;             /* ret 4 */
}

/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go at the end of the pass. */
static void nv_budget(xctx *c, uint32_t backedges)
{
    while (backedges) {
        int32_t n = c->preempt >= 1 ? c->preempt : 1;
        if ((uint32_t)n > backedges) { c->preempt -= (int32_t)backedges; return; }
        backedges -= (uint32_t)n; c->preempt -= n;
        xv_preempt(c);
    }
}

/* Window counters: updated by the thread running the scene, read and reset by the 60-frame report. */
enum { NV_PASSES, NV_VERIFIED, NV_MISMATCHED, NV_OOB, NV_SUBCLUSTERS, NV_FULL, NV_VISIBLE, NV_PUBLISHED,
       NV_TIMED_NATIVE, NV_TIMED_GUEST, NV_COUNTERS };
static unsigned nv_counter[NV_COUNTERS], nv_mismatch_total;
static uint64_t nv_native_us, nv_guest_us;
#define NV_ADD(i, v) __atomic_fetch_add(&nv_counter[i], (unsigned)(v), __ATOMIC_RELAXED)
static int nv_in_guest;
extern void f_00052E10(xctx *);

static int nv_mode_value = -1;
static int nv_mode(void)
{
    int mode = nv_mode_value;
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_VISIBILITY"); mode = e ? atoi(e) : XV_NATIVE_VISIBILITY_DEFAULT;
        if (mode < 0 || mode > 2) mode = 0;
        XK_LOG("[native-visibility] f_00052E10 subcluster pass: %s\n", mode == 2 ? "native" : mode == 1 ? "verify (native vs guest, guest result kept)" : "off");
        nv_mode_value = mode;
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_visibility_force(int mode) { nv_mode_value = mode < 0 || mode > 2 ? 0 : mode; }
static int nv_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_VISIBILITY_TIME"); on = e && atoi(e) != 0; }
    return on;
}
static void nv_count(const nv_pass *o)
{
    NV_ADD(NV_PASSES, 1); NV_ADD(NV_SUBCLUSTERS, o->calls); NV_ADD(NV_FULL, o->full_calls); NV_ADD(NV_VISIBLE, o->visible);
    NV_ADD(NV_PUBLISHED, o->published); NV_ADD(NV_OOB, o->oob);
}

enum { NV_STACK_LO = 0x94u, NV_STACK_BYTES = 0x94u + 8u };
static uint8_t nv_bits_before[NV_BITS_BYTES + 2], nv_bits_native[NV_BITS_BYTES + 2];

/* Dead x87 slots: two NaNs compare equal (the NaN payload an addition propagates is the host compiler's operand
 * order, which differs between builds of the guest body itself; tools/test_native_visibility.py). */
static int nv_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
static void nv_report_mismatch(const char *what, uint32_t a, uint32_t b, const nv_pass *o)
{
    if (__atomic_add_fetch(&nv_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-visibility] MISMATCH %s native %08X guest %08X (pass: %u subclusters, %u full, %u visible, %u new bits)\n",
               what, a, b, o->calls, o->full_calls, o->visible, o->published);
}

/* Entry hook of f_00052E10: 1 = handled (guest body skipped). */
int xv_native_visibility(xctx *c)
{
    int mode = nv_mode();
    if (nv_in_guest) return 0;
    const int timed = nv_timing();
    if (!mode) {                   /* off: only XV_NATIVE_VISIBILITY_TIME=1 measures the guest body here */
        if (!timed) return 0;
        uint64_t t0 = xk_os_monotonic_us();
        nv_in_guest = 1; f_00052E10(c); nv_in_guest = 0;
        __atomic_fetch_add(&nv_guest_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); NV_ADD(NV_TIMED_GUEST, 1); NV_ADD(NV_PASSES, 1);
        return 1;
    }
    nv_pass o;
    if (mode == 2) {
        uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
        nv_run(c, &o); nv_finish(c, &o);
        if (timed) { __atomic_fetch_add(&nv_native_us, xk_os_monotonic_us() - t0, __ATOMIC_RELAXED); NV_ADD(NV_TIMED_NATIVE, 1); }
        nv_budget(c, o.backedges); nv_count(&o);
        return 1;
    }
    /* verify: snapshot what either path can write (bitmap + count, the stack window), run the native, keep its
     * result, restore, run the guest with an unbounded budget (no scheduling inside the check), compare. */
    const uint32_t E = c->r[4], lo = E - NV_STACK_LO;
    uint8_t stack_before[NV_STACK_BYTES], stack_native[NV_STACK_BYTES], stack_guest[NV_STACK_BYTES];
    x_guest_read_pages(nv_bits_before, NV_BITS, NV_BITS_BYTES + 2);
    x_guest_read_pages(stack_before, lo, NV_STACK_BYTES);
    const xctx before = *c;
    uint64_t t0 = timed ? xk_os_monotonic_us() : 0;
    nv_run(c, &o); nv_finish(c, &o);
    uint64_t t1 = timed ? xk_os_monotonic_us() : 0;
    const xctx native = *c;
    x_guest_read_pages(nv_bits_native, NV_BITS, NV_BITS_BYTES + 2);
    x_guest_read_pages(stack_native, lo, NV_STACK_BYTES);
    x_guest_write_pages(NV_BITS, nv_bits_before, NV_BITS_BYTES + 2);
    x_guest_write_pages(lo, stack_before, NV_STACK_BYTES);
    *c = before; c->preempt = 1 << 30;
    uint64_t t2 = timed ? xk_os_monotonic_us() : 0;
    nv_in_guest = 1; f_00052E10(c); nv_in_guest = 0;
    uint64_t t3 = timed ? xk_os_monotonic_us() : 0;
    if (timed) {
        __atomic_fetch_add(&nv_native_us, t1 - t0, __ATOMIC_RELAXED); __atomic_fetch_add(&nv_guest_us, t3 - t2, __ATOMIC_RELAXED);
        NV_ADD(NV_TIMED_NATIVE, 1); NV_ADD(NV_TIMED_GUEST, 1);
    }
    uint32_t guest_backedges = (uint32_t)((1 << 30) - c->preempt);
    c->preempt = before.preempt;
    x_guest_read_pages(stack_guest, lo, NV_STACK_BYTES);
    unsigned bad = 0;
#define NV_CMP(what, a, b) do { if ((a) != (b)) { bad++; nv_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), &o); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) NV_CMP(rn[i], native.r[i], c->r[i]);
    NV_CMP("backedges", o.backedges, guest_backedges);
    NV_CMP("fsp", native.fsp, c->fsp); NV_CMP("fcw", native.fcw, c->fcw); NV_CMP("fsw", native.fsw, c->fsw);
    NV_CMP("df", native.df, c->df);
    NV_CMP("f_kind", native.f_kind, c->f_kind); NV_CMP("f_op1", native.f_op1, c->f_op1); NV_CMP("f_op2", native.f_op2, c->f_op2);
    NV_CMP("f_res", native.f_res, c->f_res); NV_CMP("f_bits", native.f_bits, c->f_bits);
    NV_CMP("f_cf_override", native.f_cf_override, c->f_cf_override); NV_CMP("f_of_override", native.f_of_override, c->f_of_override);
    for (unsigned i = 0; i < 8; ++i)
        if (!nv_same_double(native.st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &native.st[i], 8); memcpy(&b, &c->st[i], 8);
            bad++; nv_report_mismatch(i == ((c->fsp - 1u) & 7u) ? "st[-1] (lo word)" : "st[other] (lo word)", (uint32_t)a, (uint32_t)b, &o);
        }
    for (unsigned i = 0; i < NV_STACK_BYTES; i += 4) {
        uint32_t a, b; memcpy(&a, stack_native + i, 4); memcpy(&b, stack_guest + i, 4);
        if (a != b) { bad++; char w[48]; snprintf(w, sizeof w, "stack[esp%+d]", (int)(lo + i - E)); nv_report_mismatch(w, a, b, &o); }
    }
    x_guest_read_pages(nv_bits_before, NV_BITS, NV_BITS_BYTES + 2);   /* now the guest's result */
    if (memcmp(nv_bits_before, nv_bits_native, NV_BITS_BYTES + 2)) {
        for (unsigned i = 0; i < NV_BITS_BYTES + 2; i += 2) {
            uint16_t a, b; memcpy(&a, nv_bits_native + i, 2); memcpy(&b, nv_bits_before + i, 2);
            if (a != b) { bad++; char w[48]; snprintf(w, sizeof w, "bitmap+%05X (u16)", i); nv_report_mismatch(w, a, b, &o); break; }
        }
    }
#undef NV_CMP
    nv_budget(c, guest_backedges);
    nv_count(&o); NV_ADD(NV_VERIFIED, 1);
    if (bad) NV_ADD(NV_MISMATCHED, 1);
    return 1;
}

void xv_native_visibility_report(unsigned frames)
{
    unsigned n[NV_COUNTERS];
    for (unsigned i = 0; i < NV_COUNTERS; ++i) n[i] = __atomic_exchange_n(&nv_counter[i], 0u, __ATOMIC_RELAXED);
    uint64_t native_us = __atomic_exchange_n(&nv_native_us, 0, __ATOMIC_RELAXED), guest_us = __atomic_exchange_n(&nv_guest_us, 0, __ATOMIC_RELAXED);
    if (!n[NV_PASSES]) return;
    char timing[96] = "";
    if (n[NV_TIMED_NATIVE])
        snprintf(timing, sizeof timing, "; us/pass native %.1f", (double)native_us / n[NV_TIMED_NATIVE]);
    if (n[NV_TIMED_GUEST])
        snprintf(timing + strlen(timing), sizeof timing - strlen(timing), "%s guest %.1f", n[NV_TIMED_NATIVE] ? "" : "; us/pass", (double)guest_us / n[NV_TIMED_GUEST]);
    XK_LOG("[native-visibility] %u frames: passes %u verified %u mismatched %u (total mismatches %u); subclusters %u full %u visible %u new-bits %u oob %u%s\n",
           frames, n[NV_PASSES], n[NV_VERIFIED], n[NV_MISMATCHED], __atomic_load_n(&nv_mismatch_total, __ATOMIC_RELAXED), n[NV_SUBCLUSTERS],
           n[NV_FULL], n[NV_VISIBLE], n[NV_PUBLISHED], n[NV_OOB], timing);
}
