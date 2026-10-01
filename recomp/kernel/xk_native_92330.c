/* xk_native_92330.c - native Halo CE (Xbox 3925) light cluster query: f_00056670 and its leaf subtree.
 *
 * f_00092330 (light update, ~87 calls/frame from f_0008D760 in the a10 cinematic on the Vita, 8.9 ms/frame
 * inclusive, "6.96 ms self" in the phase timers) spends most of that "self" time in a callee the timers do not
 * wrap: f_00056670, called at 925AB for every connected light. Its own x87 work is ~20 instructions. Measured with
 * extra timers (host x86 and Pi 4, a10 checkpoint with movement): f_00056670 is 71-74 % of f_00092330 inclusive,
 * the rest is 8D650, 8B220/8B910/8B0F0 and the body. The smallest subtree that holds that cost:
 *
 *   f_00056670  cluster query: flood the BSP cluster graph from the light's cluster within its radius (epoch
 *               0x2D2FAC, visited stamps 0x2D2FB0[cluster], in-use byte 0x2D2FA9), then for each found cluster
 *               (at most 64) allocate two reference datums and link them into the light's and the cluster's lists
 *   f_00052240  recursive flood over cluster portals (records up to `count` clusters into the caller's list)
 *   f_00051E90  portal test: sphere vs portal plane, vs the portal's bounding sphere, then projects the sphere onto
 *               the plane's dominant axes and tests the 2D circle against the portal polygon
 *   f_00011840  dominant axis of a plane normal
 *   f_000B77C0  2D circle vs convex polygon (edge half-planes)
 *   f_000A9330  datum_new (first free slot from the array's hint, zero it, salt, counters)
 *
 * No other calls, no HLE. The native replaces the whole body of f_00056670 after its preamble (phase scope, object
 * math guard, census token, typed worker query), for every caller (f_00092330 and f_0008E970).
 *
 * Exact by construction (a transliteration of the generated code, not a re-derivation):
 *  - every guest memory read and write at the same program point and in the same order, through the same
 *    translation (thread page table; image globals through the table under XV_RENDER_VIEW); integer accesses
 *    single-translation like X_M32/X_W32, floats page-split like x87_load_f32/x87_store_f32; the rep stos of
 *    datum_new goes through x_str_stos itself;
 *  - the dead guest stack is written through (pushes, return addresses, locals, the projected polygon), so the
 *    bytes below esp are the guest's; stack slots are read back from locals only while no write can have hit them
 *    (a visited-stamp write into the live stack window or a portal of more than 128 vertices sets `dirty` and every
 *    later stack read comes from guest memory);
 *  - x87: doubles with the guest's operand order and rounding points (float loads widened, float stores rounded,
 *    -ffp-contract=off), the final value of every scratch slot st[(fsp0-k)&7] the subtree wrote, and the status word
 *    exactly as x87_compare leaves it (condition codes of the last compare, TOP bits OR-ed in by every compare);
 *  - registers on exit (eax/ecx/edx exact, ebx/ebp/esi from the guest's pops, esp + 0x14), the lazy-flag record
 *    of the last flag-writing instruction (including f_cf when its override is set);
 *  - the back-edge budget: c->preempt drops by exactly the guest's back-edge count and xv_preempt() is called the
 *    same number of times, at the end of the query instead of mid-loop (a scheduling point only, as in
 *    xk_native_visibility.c);
 *  - the object-math guard of each datum_new (recursive on a worker lane, parks like the guest's).
 * Not reproduced (unobservable): the stale f_cf/f_of cells while their overrides are 0 (no flag read consults them
 * for the SUB/LOGIC records left here) and NaN payload bits (which NaN an operation propagates is the host
 * compiler's operand order; the guest body built -O0/-O2 already differs). Declines (runs the guest) while the
 * light census has a token open or the census is on.
 *
 * XV_NATIVE_92330 build flag (hooks: tools/patch_native_92330_hooks.py). Env XV_NATIVE_92330: 0 off (default
 * XV_NATIVE_92330_DEFAULT), 1 verify (native, undo its writes from a journal, run the guest body on the same state,
 * compare registers/flags/x87/budget, every byte the native wrote and the regions the guest may write, keep the
 * guest result), 2 native. XV_NATIVE_92330_TIME=1: us/call (mode 0 guest, 2 native, 1 both). [native-92330] line
 * every 60 frames. */
#include "xk.h"
/* xv_x86rt.h comes through xk.h (one path: the unit is also compiled from a copy by tools/test_native_92330.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_NATIVE_92330_DEFAULT
#define XV_NATIVE_92330_DEFAULT 0
#endif
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
int xv_object_math_lock(void);
void xv_object_math_unlock(int *locked);
#endif
#ifdef XV_LIGHT_QUERY_CENSUS
extern unsigned xv_light_census_enabled;
#endif

enum {
    N9_EPOCH = 0x2D2FACu, N9_INUSE = 0x2D2FA9u, N9_STAMPS = 0x2D2FB0u, N9_EPS = 0x1F0A68u, N9_BSP = 0x39BE58u,
    N9_BSP3D = 0x39BE50u, N9_AXES = 0x1EAF30u, N9_MAXV = 128,
};

/* ---- guest memory, as the generated code addresses it ----------------------------------------------------- */
typedef struct { uint8_t *ram; const uint32_t *pt; uint8_t *img; int jon; } n9_mem;   /* jon: journal the writes (verify) */
#define N9_P(a) (m->ram + m->pt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu))
#if defined(XV_RENDER_VIEW) && XV_RENDER_VIEW
#define N9_I(a) N9_P(a)                      /* render view: image globals translate like everything else */
#else
#define N9_I(a) (m->img + (uint32_t)(a))
#endif

/* Verify mode journals every native write (address, size, old bytes) so the pre-state can be restored. */
typedef struct { uint32_t addr; uint8_t size, pad[3]; uint8_t old[4], now[4]; uint32_t word; } n9_jent;   /* word: the aligned 32 bits after the native */
typedef struct { n9_jent *e; unsigned n, cap; int overflow; } n9_journal;
static __thread n9_journal n9_j;
static void n9_jlog_slow(uint32_t a, const void *host, unsigned size)
{
    if (n9_j.n == n9_j.cap) {
        unsigned cap = n9_j.cap ? n9_j.cap * 2u : 4096u;
        n9_jent *e = realloc(n9_j.e, cap * sizeof *e);
        if (!e) { n9_j.overflow = 1; return; }
        n9_j.e = e; n9_j.cap = cap;
    }
    n9_jent *j = &n9_j.e[n9_j.n++]; j->addr = a; j->size = (uint8_t)size; memcpy(j->old, host, size);
}
#define n9_jlog(a, host, size) do { if (__builtin_expect(m->jon, 0)) n9_jlog_slow((a), (host), (size)); } while (0)

static inline uint32_t n9_r32(const n9_mem *m, uint32_t a) { uint32_t v; memcpy(&v, N9_P(a), 4); return v; }
static inline uint16_t n9_r16(const n9_mem *m, uint32_t a) { uint16_t v; memcpy(&v, N9_P(a), 2); return v; }
static inline uint32_t n9_i32(const n9_mem *m, uint32_t a) { uint32_t v; memcpy(&v, N9_I(a), 4); return v; }
static inline void n9_w32(const n9_mem *m, uint32_t a, uint32_t v) { uint8_t *p = N9_P(a); n9_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline void n9_w16(const n9_mem *m, uint32_t a, uint16_t v) { uint8_t *p = N9_P(a); n9_jlog(a, p, 2); memcpy(p, &v, 2); }
static inline void n9_wi32(const n9_mem *m, uint32_t a, uint32_t v) { uint8_t *p = N9_I(a); n9_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline void n9_wi8(const n9_mem *m, uint32_t a, uint8_t v) { uint8_t *p = N9_I(a); n9_jlog(a, p, 1); *p = v; }
static inline double n9_rf(const n9_mem *m, uint32_t a)       /* x87_load_f32 */
{
    float v;
    if ((a & 0xFFFu) <= 0xFFCu) memcpy(&v, N9_P(a), 4); else x_guest_read_pages(&v, a, 4);
    return (double)v;
}
static inline float n9_wf(const n9_mem *m, uint32_t a, double d)  /* x87_store_f32; returns the stored float */
{
    float v = (float)d;
    if ((a & 0xFFFu) <= 0xFFCu) { uint8_t *p = N9_P(a); n9_jlog(a, p, 4); memcpy(p, &v, 4); }
    else {
        if (__builtin_expect(m->jon, 0))
            for (unsigned i = 0; i < 4; ++i) { uint32_t b = a + i; n9_jlog(b, N9_P(b), 1); }
        x_guest_write_pages(a, &v, 4);
    }
    return v;
}

static int n9_watch;         /* XV_WATCH_ADDR set: every rep stos goes through x_str_stos (its write-watch report) */
static int n9_detail;        /* the per-query detail counters (each an atomic) only when reported: _TIME or verify */

/* ---- per-query state --------------------------------------------------------------------------------------- */
typedef struct {
    n9_mem m;
    xctx *c;
    uint32_t E;              /* esp at entry of f_00056670 ([E] return address) */
    uint32_t fsp0;
    uint16_t fsw;            /* running x87 status word (x87_compare semantics) */
    unsigned touched;        /* bit k: scratch slot k (st[(fsp0-k)&7]) written */
    double sl[8];
    uint32_t backedges;
    uint32_t low;            /* lowest guest stack address the subtree can write so far */
    int dirty;               /* a write may have hit a stack slot a caller keeps in a local */
    int16_t cl_min, cl_max;  /* stamped cluster range (verify regions) */
    unsigned portals, full, clusters, datums, flood;
#ifdef XV_NATIVE_52240_TEST
    uint32_t flood_flag_a, flood_flag_b;
    unsigned flood_flag_kind;
#endif
} n9;

static inline uint16_t n9_cmp_(uint16_t *fsw, uint32_t fsp0, double a, double b, unsigned depth)   /* x87_compare */
{
    uint16_t cc = (a != a || b != b) ? 0x4500 : (a < b) ? 0x0100 : (a == b) ? 0x4000 : 0;
    *fsw = (uint16_t)((*fsw & ~0x4700u) | cc | (((fsp0 - depth) & 7u) << 11));
    return cc;
}
/* In each function: the status word, the scratch slots' base and the memory translation in locals (the state struct
 * may alias guest memory for the compiler: every guest store would reload it). */
#define n9_cmp(s_, a, b, depth) n9_cmp_(&fsw, fsp0, (a), (b), (depth))
#define N9_LOCALS const n9_mem mm_ = s->m; const n9_mem *const m = &mm_; const uint32_t fsp0 = s->fsp0; uint16_t fsw = s->fsw; uint32_t be = 0
#define N9_SL(k, v) (s->sl[k] = (v))
#define N9_LO(x) ((x) & 0xFFFF0000u)

typedef struct { uint32_t eax, ecx, edx, ebx, ebp, esi, edi; } n9_regs;

/* Stack frames: the host pointers of the (at most two) pages a frame spans, translated once per frame. Integer
 * stores through them are X_W32's single translation (a store at a page end continues in the same host page); float
 * stores that would cross the page end take the page-split path, like x87_store_f32. Only for addresses whose page is
 * p0 or p0 + 1. */
typedef struct { uint32_t p0; uint8_t *h0, *h1; } n9_stk;
static inline void n9_stk_at(const n9_mem *m, n9_stk *k, uint32_t lo)
{
    k->p0 = lo >> 12; k->h0 = m->ram + m->pt[k->p0]; k->h1 = m->ram + m->pt[(k->p0 + 1u) & 0xFFFFFu];
}
#define N9_SP(k, a) ((((uint32_t)(a) >> 12) == (k)->p0 ? (k)->h0 : (k)->h1) + ((uint32_t)(a) & 0xFFFu))
static inline void n9_sw32(const n9_mem *m, const n9_stk *k, uint32_t a, uint32_t v) { uint8_t *p = N9_SP(k, a); n9_jlog(a, p, 4); memcpy(p, &v, 4); }
static inline void n9_sw16(const n9_mem *m, const n9_stk *k, uint32_t a, uint16_t v) { uint8_t *p = N9_SP(k, a); n9_jlog(a, p, 2); memcpy(p, &v, 2); }
static inline float n9_swf(const n9_mem *m, const n9_stk *k, uint32_t a, double d)
{
    if ((a & 0xFFFu) > 0xFFCu) return n9_wf(m, a, d);
    float v = (float)d; uint8_t *p = N9_SP(k, a); n9_jlog(a, p, 4); memcpy(p, &v, 4); return v;
}
/* Guest records read through one translation when [a, a+len) lies in one page: host(a) + off is then exactly the
 * translation of a + off (integer and float reads alike); otherwise NULL and every field is translated. */
static inline const uint8_t *n9_span(const n9_mem *m, uint32_t a, uint32_t len) { return (a & 0xFFFu) + len <= 0x1000u ? N9_P(a) : NULL; }
static inline uint32_t n9_hr32(const n9_mem *m, const uint8_t *h, uint32_t a, uint32_t off)
{
    if (h) { uint32_t v; memcpy(&v, h + off, 4); return v; }
    return n9_r32(m, a + off);
}
static inline uint16_t n9_hr16(const n9_mem *m, const uint8_t *h, uint32_t a, uint32_t off)
{
    if (h) { uint16_t v; memcpy(&v, h + off, 2); return v; }
    return n9_r16(m, a + off);
}
static inline double n9_hrf(const n9_mem *m, const uint8_t *h, uint32_t a, uint32_t off)
{
    if (h) { float v; memcpy(&v, h + off, 4); return (double)v; }
    return n9_rf(m, a + off);
}

/* Register liveness at the subtree's internal returns (used to pass only what a caller can observe):
 *  - after f_00051E90 returns into f_00052240, eax and ecx are overwritten before any read on both paths (hit:
 *    52312/5230E and the callee's 52255; miss: 5232C/52330) and ebx/ebp/esi/edi are the values it pushed; its edx
 *    can survive to f_00052240's exit (a miss on the last portal) - so the portal test returns al and edx;
 *  - inside f_00051E90, f_000B77C0's eax and ecx are dead the same way (only al is tested) and its ebx/ebp/esi/edi
 *    are its pushes; its edx is 51E90's exit edx;
 *  - after a nested f_00052240 returns into its caller, ecx and edx are overwritten (5232C/52330, 52324) and only
 *    eax (the count) and the popped ebx/ebp/esi/edi are used; the outermost call's ecx/edx reach f_00056670's exit
 *    only when the count is <= 0 as int16 (after 32768 clusters), and are returned for that. */

/* f_000B77C0: 2D circle vs polygon. esp = U ([U+4] count, [U+8] &center, [U+0xC] radius float), ecx = points.
 * pts/ctr: the points and center 51E90 just wrote (valid unless dirty). Returns al; *edx_io = its exit edx. */
static inline __attribute__((always_inline)) unsigned n9_circle(n9 *s, const n9_stk *k, uint32_t U, float r2f_in, uint32_t cnt,
                                                                 uint32_t ecx, uint32_t *edx_io, uint32_t ebx, uint32_t ebp,
                                                                 uint32_t esi, uint32_t edi, const float *pts, const float *ctr)
{
    N9_LOCALS;
    uint32_t edx = *edx_io;
    const uint32_t V = U - 0x10u, ctr_addr = U + 0x30u;   /* [U+8] = H + 0x24 = U + 0x30: the center */
    const int mem = s->dirty;
    unsigned al = 1;
    double k1 = (double)r2f_in;
    edx = N9_LO(edx) | (cnt & 0xFFFFu);
    k1 = k1 * (double)r2f_in;
    n9_sw32(m, k, U - 4u, ebx); n9_sw32(m, k, U - 8u, ebp);
    n9_sw32(m, k, U - 0xCu, esi);
    const float r2f = n9_swf(m, k, U + 8u, k1);
    n9_sw32(m, k, V, edi);
    N9_SL(1, k1); s->touched |= 1u << 1;
    if ((int16_t)edx > 0) {
        const int32_t n = (int16_t)edx;
        for (int32_t i = 0;;) {
            const uint32_t eax = (uint32_t)i, j = i + 1 >= n ? 0u : (uint32_t)(i + 1);
            edx = j;
            double pju, piu, pjv, piv, cu, cv;
            if (!mem && eax < N9_MAXV && j < N9_MAXV) {
                pju = pts[2 * j]; piu = pts[2 * eax]; pjv = pts[2 * j + 1]; piv = pts[2 * eax + 1];
                cu = ctr[0]; cv = ctr[1];
            } else {
                pju = n9_rf(m, ecx + j * 8u); piu = n9_rf(m, ecx + eax * 8u);
                pjv = n9_rf(m, ecx + j * 8u + 4u); piv = n9_rf(m, ecx + eax * 8u + 4u);
                cu = n9_rf(m, ctr_addr); cv = n9_rf(m, ctr_addr + 4u);
            }
            double ex = pju - piu, ey = pjv - piv, wx = cu - piu, wy = cv - piv;
            double l2 = ex * ex, ey2 = ey * ey;
            l2 = l2 + ey2;
            const float l2f = n9_swf(m, k, V + 0x1Cu, l2);
            uint16_t cc = n9_cmp(s, l2, n9_rf(m, N9_EPS), 5);
            s->touched |= 0x7Eu;
            if (cc == 0x4000) {                              /* jnp: length equals eps exactly: skip the edge */
                N9_SL(1, ex); N9_SL(2, ey); N9_SL(3, wx); N9_SL(4, wy); N9_SL(5, l2); N9_SL(6, ey2);
            } else {
                double t4 = wx * ey;                         /* fxch; fmul st,st(2) */
                double t4b = wy * ex;                        /* fxch; fmul st,st(3) */
                double cross = t4 - t4b;                     /* fsubp: (wx*ey) - (wy*ex) */
                cc = n9_cmp(s, cross, n9_rf(m, N9_EPS), 1);
                if (cc & 0x4100) {
                    N9_SL(1, cross); N9_SL(2, ey); N9_SL(3, cross); N9_SL(4, t4b); N9_SL(5, l2); N9_SL(6, ey2);
                } else {
                    double c2 = cross * cross;
                    double lr = (double)l2f * (double)r2f;
                    cc = n9_cmp(s, lr, c2, 3);
                    N9_SL(1, cross); N9_SL(2, c2); N9_SL(3, lr); N9_SL(4, t4b); N9_SL(5, l2); N9_SL(6, ey2);
                    if (cc == 0x0100) { al = 0; break; }     /* len2 * r^2 < cross^2: the circle misses this edge */
                }
            }
            i = (int16_t)(i + 1);                            /* inc edi; cmp di,[esp+14h]: 16-bit */
            if (i < n) { be++; continue; }
            break;
        }
    }
    s->fsw = fsw; s->backedges += be;
    *edx_io = edx;
    return al;
}

/* f_00051E90 after the two sphere tests (from 51F2F): the dominant axis (f_00011840), the projection of the sphere
 * onto the portal plane, the polygon copy and f_000B77C0. Returns al; *edx_out = the exit edx. */
static __attribute__((noinline)) unsigned n9_portal_full(n9 *s, const n9_stk *k, uint32_t T, double Rd, float distf,
                                                         uint32_t ecx, uint32_t ebx, uint32_t ebp, uint32_t esi, uint32_t edi,
                                                         uint32_t *edx_out)
{
    N9_LOCALS;
    uint32_t eax, edx;
    const uint32_t F = T - 0x424u, G = F - 4u, H = G - 4u, U = H - 0xCu;
    double k1, k2;
    uint16_t cc;
    s->full++;
    edx = n9_i32(m, N9_BSP3D);
    n9_sw32(m, k, G, esi);
    esi = n9_r32(m, edx + 0x10u) + ecx;
    eax = esi;
    n9_sw32(m, k, G - 4u, 0x51F42u);
    {   /* f_00011840: dominant axis of the normal at eax */
        double a1 = fabs(n9_rf(m, eax)), a2 = fabs(n9_rf(m, eax + 4u)), a3 = fabs(n9_rf(m, eax + 8u));
        N9_SL(3, a3);
        unsigned axis;
        cc = n9_cmp(s, a3, a2, 3);
        if (!(cc & 0x100)) {
            cc = n9_cmp(s, a3, a1, 3);
            if (!(cc & 0x100)) { axis = 2; goto axis_done; }
        }
        cc = n9_cmp(s, a2, a1, 2);
        axis = (cc & 0x100) ? 0u : 1u;
    axis_done:
        ecx = axis;                                          /* movsx ecx,ax */
    }
    k1 = n9_rf(m, esi + ecx * 4u);
    cc = n9_cmp(s, k1, n9_rf(m, N9_EPS), 1);
    eax = (cc & 0x4100) ? 0u : 1u;
    k1 = (double)distf;
    k1 = -k1;
    k2 = k1 * n9_rf(m, esi);
    eax = (eax + ecx * 2u) << 2;
    ecx = (uint32_t)(int32_t)(int16_t)n9_r16(m, eax + N9_AXES);
    k2 = k2 + n9_rf(m, edi);
    edx = (uint32_t)(int32_t)(int16_t)n9_r16(m, eax + N9_AXES + 2u);
    n9_swf(m, k, G + 0x14u, k2);
    k2 = k1 * n9_rf(m, esi + 4u);
    eax = n9_r32(m, ebx + 0x34u);
    ecx <<= 2; edx <<= 2;
    k2 = k2 + n9_rf(m, edi + 4u);
    n9_swf(m, k, G + 0x18u, k2);
    k1 = k1 * n9_rf(m, esi + 8u);
    k1 = k1 + n9_rf(m, edi + 8u);
    edi = 0;
    n9_swf(m, k, G + 0x1Cu, k1);
    float ctr[2];
    ctr[0] = n9_swf(m, k, G + 0x20u, n9_rf(m, G + ecx + 0x14u));
    ctr[1] = n9_swf(m, k, G + 0x24u, n9_rf(m, G + edx + 0x14u));
    float pts[2 * N9_MAXV];
    if ((int32_t)eax > 0) {
        eax = 0;
        for (;;) {
            const uint32_t at = G + 0x28u + eax * 8u, idx = eax;
            esi = at;                                        /* lea esi,[esp+eax*8+28h] (pushed by 77C0) */
            eax = n9_r32(m, ebx + 0x38u) + eax * 12u;
            double v = n9_rf(m, edx + eax);
            ebp = n9_r32(m, ecx + eax);
            edi += 1u;
            if (idx < N9_MAXV) {                             /* inside this frame */
                const float vf = n9_swf(m, k, at + 4u, v);
                n9_sw32(m, k, at, ebp);
                pts[2 * idx + 1] = vf; memcpy(&pts[2 * idx], &ebp, 4);
            } else {                                         /* at or above T: the callers' frames */
                n9_wf(m, at + 4u, v);
                n9_w32(m, at, ebp);
                s->dirty = 1;
            }
            eax = (uint32_t)(int32_t)(int16_t)edi;
            if ((int32_t)eax < (int32_t)n9_r32(m, ebx + 0x34u)) { be++; continue; }
            break;
        }
    }
    const double R1 = s->dirty ? n9_rf(m, T + 4u) : Rd, R2 = s->dirty ? n9_rf(m, T + 4u) : Rd;
    k1 = R1 * R2;
    edx = n9_r16(m, ebx + 0x34u);
    k2 = (double)distf;
    n9_sw32(m, k, H, ecx);
    k2 = k2 * (double)distf;
    k1 = k1 - k2;
    k1 = sqrt(k1);
    const float r2d = n9_swf(m, k, H, k1);
    n9_sw32(m, k, H - 4u, H + 0x24u);
    n9_sw32(m, k, H - 8u, edx);
    n9_sw32(m, k, U, 0x52014u);
    N9_SL(1, k1); N9_SL(2, k2); s->touched |= 0xEu;       /* slot 3 = |nz| from 11840 (set above) */
    s->fsw = fsw; s->backedges += be;
    const unsigned al = n9_circle(s, k, U, r2d, edx, H + 0x2Cu, &edx, ebx, ebp, esi, edi, pts, ctr);
    *edx_out = edx;
    return al;
}

/* f_00051E90: portal test. esp = T ([T+4] radius bits), eax = BSP, ecx = &position, edx: dx = portal index,
 * ebx/ebp/esi/edi as the flood has them. Returns al (1 hit / 0 miss) and the exit edx. The sphere tests here. */
static __attribute__((noinline)) unsigned n9_portal(n9 *s, uint32_t T, uint32_t radius_bits, uint32_t eax, uint32_t ecx,
                                                    uint32_t *edx_io, uint32_t ebx, uint32_t ebp, uint32_t esi, uint32_t edi)
{
    N9_LOCALS;
    const uint32_t F = T - 0x424u, edx = *edx_io;
    n9_stk k; n9_stk_at(m, &k, T - 0x448u);
    float rf; memcpy(&rf, &radius_bits, 4);
    const double Rd = (double)rf;
    s->portals++;
    n9_sw32(m, &k, T - 0x41Cu, ebx);
    ebx = (uint32_t)(int32_t)(int16_t)edx;
    n9_sw32(m, &k, T - 0x420u, ebp);
    const uint8_t *hb = n9_span(m, eax + 0xB4u, 0xA8);      /* BSP +0xB4 .. +0x15B */
    ebp = n9_hr32(m, hb, eax + 0xB4u, 0xA4);
    eax = n9_hr32(m, hb, eax + 0xB4u, 0);
    eax = n9_r32(m, eax + 0x10u);
    ebx = (ebx << 6) + ebp;
    n9_sw32(m, &k, F, edi);
    edi = ecx;
    const uint8_t *hp = n9_span(m, ebx + 4u, 0x14);          /* portal +4 .. +0x17 */
    ecx = n9_hr32(m, hp, ebx + 4u, 0) << 4;
    eax += ecx;
    const uint8_t *hl = n9_span(m, eax, 16), *hq = n9_span(m, edi, 12);
    double k1 = n9_hrf(m, hl, eax, 4) * n9_hrf(m, hq, edi, 4);
    double k2 = n9_hrf(m, hl, eax, 8) * n9_hrf(m, hq, edi, 8);
    k1 = k1 + k2;
    k2 = n9_hrf(m, hq, edi, 0) * n9_hrf(m, hl, eax, 0);
    k1 = k1 + k2;
    k1 = k1 - n9_hrf(m, hl, eax, 0xC);
    const float distf = n9_swf(m, &k, F + 0xCu, k1);
    k1 = fabs(k1);
    uint16_t cc = n9_cmp(s, Rd, k1, 2);
    if (cc & 0x4100) {
        N9_SL(1, k1); N9_SL(2, Rd); s->touched |= 0x6u;
        s->fsw = fsw;
        return 0;
    }
    double dx = n9_hrf(m, hp, ebx + 4u, 4) - n9_hrf(m, hq, edi, 0);
    double dy = n9_hrf(m, hp, ebx + 4u, 8) - n9_hrf(m, hq, edi, 4);
    double dz = n9_hrf(m, hp, ebx + 4u, 0xC) - n9_hrf(m, hq, edi, 8);
    double rr = Rd + n9_hrf(m, hp, ebx + 4u, 0x10);
    double d2 = dz * dz, t = dx * dx;
    d2 = d2 + t;
    t = dy * dy;
    d2 = d2 + t;
    double rr2 = rr * rr;
    cc = n9_cmp(s, rr2, d2, 6);
    N9_SL(4, rr); N9_SL(5, d2); N9_SL(6, rr2); s->touched |= 0x7Eu;
    s->fsw = fsw;
    if (cc & 0x4100) { N9_SL(1, dx); N9_SL(2, dy); N9_SL(3, dz); return 0; }
    (void)be;
    return n9_portal_full(s, &k, T, Rd, distf, ecx, ebx, ebp, esi, edi, edx_io);
}

/* f_00052240: recursive flood. esp = S ([S+4] radius bits, [S+8] count, [S+0xC] list pointer), cx = cluster,
 * edx = &position, ebx/ebp/esi/edi the caller's (pushed). Returns eax (the count; its upper half from the loop
 * index) and, through ecx_out and edx_out, the exit ecx and edx (observable only at the outermost level). */
static uint32_t n9_flood(n9 *s, uint32_t S, uint32_t radius, uint32_t count, uint32_t list, uint32_t ecx, uint32_t edx,
                         uint32_t ebx, uint32_t ebp, uint32_t esi, uint32_t edi, uint32_t *ecx_out, uint32_t *edx_out)
{
    N9_LOCALS;
    uint32_t eax;
    if (S - 0x46Cu < s->low) s->low = S - 0x46Cu;
    s->flood++;
    n9_stk k; n9_stk_at(m, &k, S - 0x2Cu);                /* this frame: [S-0x2C, S+0x10) */
    n9_sw32(m, &k, S - 0x10u, ebx);
    ebx = N9_LO(ebx) | (ecx & 0xFFFFu);
    ecx = n9_i32(m, N9_BSP);
    n9_sw32(m, &k, S - 0x14u, ebp);
    ebp = n9_r32(m, ecx + 0x138u);
    n9_sw32(m, &k, S - 0x18u, esi);
    eax = (uint32_t)(int32_t)(int16_t)ebx;
    esi = eax * 0x68u + ebp;
    ebp = count;
    uint32_t pos = edx;
    n9_sw32(m, &k, S - 4u, edx);
    edx = N9_LO(edx) | (ebp & 0xFFFFu);
    ebp -= 1u;
    n9_sw32(m, &k, S - 0x1Cu, edi);
    edi = list;
    uint16_t cl = (uint16_t)ebx;
    n9_sw16(m, &k, S - 0xCu, cl);
    uint32_t bsp = ecx;
    n9_sw32(m, &k, S - 8u, ecx);
    if ((int16_t)edx > 0) { n9_w16(m, edi, (uint16_t)ebx); edi += 2u; }
    edx = n9_i32(m, N9_EPOCH);
    {
        const uint32_t a = eax * 4u + N9_STAMPS;
        if (n9_r32(m, a) != edx) {
            n9_w32(m, a, edx);
            if (a - s->low < s->E + 0x20u - s->low) s->dirty = 1;
            if ((int16_t)eax < s->cl_min) s->cl_min = (int16_t)eax;
            if ((int16_t)eax > s->cl_max) s->cl_max = (int16_t)eax;
        }
    }
    s->clusters++;
    uint32_t found = 1u, i = 0u;
    if (s->dirty) {
        pos = n9_r32(m, S - 4u); cl = n9_r16(m, S - 0xCu); bsp = n9_r32(m, S - 8u); radius = n9_r32(m, S + 4u);
    }
    const uint8_t *hc = n9_span(m, esi + 0x5Cu, 8);         /* cluster +0x5C portal count, +0x60 portal indices */
    edx = n9_hr32(m, hc, esi + 0x5Cu, 0);
    eax = 1u;
    n9_sw32(m, &k, S + 0xCu, 1u);
    n9_sw32(m, &k, S + 8u, 0u);
    if ((int32_t)edx > 0) {
        eax = 0;
        int first = 1;
        for (;;) {
            if (!first) { ecx = bsp; ebx = N9_LO(ebx) | cl; }
            first = 0;
            edx = n9_hr32(m, hc, esi + 0x5Cu, 4);
            edx = N9_LO(edx) | n9_r16(m, edx + eax * 2u);
            eax = (uint32_t)(int32_t)(int16_t)edx << 6;
            eax += n9_r32(m, ecx + 0x158u);
            const uint8_t *hp = n9_span(m, eax, 4);           /* portal +0 front, +2 back */
            ecx = N9_LO(ecx) | n9_hr16(m, hp, eax, 0);
            if ((uint16_t)ecx == (uint16_t)ebx) ebx = N9_LO(ebx) | n9_hr16(m, hp, eax, 2);
            else ebx = ecx;
            ecx = n9_i32(m, N9_EPOCH);
            eax = (uint32_t)(int32_t)(int16_t)ebx;
            if (n9_r32(m, eax * 4u + N9_STAMPS) != ecx) {
                n9_sw32(m, &k, S - 0x20u, radius);
                n9_sw32(m, &k, S - 0x24u, 0x5230Au);
                s->backedges += be; be = 0;
                const unsigned al = n9_portal(s, S - 0x24u, radius, bsp, pos, &edx, ebx, ebp, esi, edi);
                if (s->dirty) {
                    pos = n9_r32(m, S - 4u); cl = n9_r16(m, S - 0xCu); bsp = n9_r32(m, S - 8u);
                    radius = n9_r32(m, S + 4u); i = n9_r32(m, S + 8u); found = n9_r32(m, S + 0xCu);
                }
                if (al) {
                    edx = pos;
                    n9_sw32(m, &k, S - 0x20u, edi); n9_sw32(m, &k, S - 0x24u, ebp); n9_sw32(m, &k, S - 0x28u, radius);
                    n9_sw32(m, &k, S - 0x2Cu, 0x52320u);
                    uint32_t c2, d2;
                    eax = n9_flood(s, S - 0x2Cu, radius, ebp, edi, ebx, edx, ebx, ebp, esi, edi, &c2, &d2);
                    /* its pops (read from its frame like the guest) */
                    ebx = n9_r32(m, S - 0x2Cu - 0x10u); ebp = n9_r32(m, S - 0x2Cu - 0x14u);
                    esi = n9_r32(m, S - 0x2Cu - 0x18u); edi = n9_r32(m, S - 0x2Cu - 0x1Cu);
                    hc = n9_span(m, esi + 0x5Cu, 8);             /* esi is the pop: new when a frame was overrun */
                    if (s->dirty) {
                        pos = n9_r32(m, S - 4u); cl = n9_r16(m, S - 0xCu); bsp = n9_r32(m, S - 8u);
                        radius = n9_r32(m, S + 4u); i = n9_r32(m, S + 8u); found = n9_r32(m, S + 0xCu);
                    }
                    found += eax; n9_sw32(m, &k, S + 0xCu, found);
                    edx = (uint32_t)(int32_t)(int16_t)eax;
                    ebp -= eax;
                    edi += edx * 2u;
                }
            }
            eax = i;
            ecx = n9_hr32(m, hc, esi + 0x5Cu, 0);
            eax += 1u;
            i = eax; n9_sw32(m, &k, S + 8u, eax);
            eax = (uint32_t)(int32_t)(int16_t)eax;
            if ((int32_t)eax < (int32_t)ecx) { be++; continue; }
            break;
        }
        if (s->dirty) found = n9_r32(m, S + 0xCu);
#ifdef XV_NATIVE_52240_TEST
        s->flood_flag_kind = XK_SUB; s->flood_flag_a = eax; s->flood_flag_b = ecx;
#endif
        eax = N9_LO(eax) | (found & 0xFFFFu);
    }
#ifdef XV_NATIVE_52240_TEST
    else { s->flood_flag_kind = XK_LOGIC; s->flood_flag_a = edx; s->flood_flag_b = 0; }
#endif
    (void)fsw; (void)fsp0; s->backedges += be;
    *ecx_out = ecx; *edx_out = edx;
    return eax;
}

/* f_000A9330 (datum_new) called at esp = S: edx = data array. Updates eax, ecx and the popped ebx/ebp/esi/edi. */
static inline __attribute__((always_inline)) void n9_datum_new(n9 *s, n9_regs *R, uint32_t S)
{
    N9_LOCALS;
    xctx *c = s->c;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    int locked = xv_object_math_lock();
#endif
    uint32_t eax, ecx, edx = R->edx, ebx = R->ebx, ebp = R->ebp, esi = R->esi, edi = R->edi;
    s->datums++;
    n9_stk k; n9_stk_at(m, &k, S - 0x10u);                /* the pushes: [S-0x10, S) */
    ecx = (uint32_t)(int32_t)(int16_t)n9_r16(m, edx + 0x22u);
    n9_sw32(m, &k, S - 4u, ebx);
    ebx = N9_LO(ebx) | n9_r16(m, edx + 0x2Cu);
    n9_sw32(m, &k, S - 8u, ebp);
    ebp = n9_r32(m, edx + 0x34u);
    n9_sw32(m, &k, S - 0xCu, esi);
    esi = (uint32_t)(int32_t)(int16_t)ebx * ecx + ebp;
    eax = 0xFFFFFFFFu;
    {
        /* The scan only reads (salts and the count limit at +0x20), so the limit is loaded once and each salt goes
         * through the translation of its page, done once per page (X_M16 semantics: one translation per access, the
         * same host pointer for every address of a page). */
        const int16_t max = (int16_t)n9_r16(m, edx + 0x20u);
        if ((int16_t)ebx < max) {
            /* back-edges taken = slots stepped over before the free one (or all but the last when none is free) */
            const uint32_t b0 = ebx;
            for (;;) {
                const uint32_t page = esi >> 12;
                const uint8_t *const hb = m->ram + m->pt[page];
                /* the slots of this page: same host page for every salt address in it */
                for (;;) {
                    uint16_t salt; memcpy(&salt, hb + (esi & 0xFFFu), 2);
                    if (salt == 0) { be += ebx - b0; goto found; }
                    ebx += 1u; esi += ecx;
                    if (!((int16_t)ebx < max)) { be += ebx - b0 - 1u; goto pop; }
                    if ((esi >> 12) != page) break;
                }
            }
        }
    }
    goto pop;
found:
    ebp = ecx;
    ecx >>= 2;
    n9_sw32(m, &k, S - 0x10u, edi);
    eax = 0; edi = esi;
    if (__builtin_expect(m->jon, 0)) {                    /* journal the element the rep stos clears */
        uint32_t n = (ecx * 4u) + (ebp & 3u);
        if (!c->df) for (uint32_t k = 0; k < n; ++k) { uint32_t b = edi + k; n9_jlog(b, N9_P(b), 1); }
        else n9_j.overflow = 1;
    }
    {
        const uint32_t n = ecx * 4u + (ebp & 3u);
        if (!c->df && !n9_watch && n <= 0x1000u - (edi & 0xFFFu)) {   /* (n from a negative element size is huge) */
            /* x_str_stos with df = 0 and a zero value clears the bytes page by page through the translation: in one
             * page that is one memset (its XV_WATCH_ADDR diagnostic is honoured by taking the call below instead) */
            memset(N9_P(edi), 0, n); edi += n; ecx = 0;
        } else {
            c->r[0] = eax; c->r[1] = ecx; c->r[7] = edi;
            x_str_stos(c, 4, X_STR_REP);
            c->r[1] = ebp & 3u;
            x_str_stos(c, 1, X_STR_REP);
            edi = c->r[7]; ecx = c->r[1];
        }
    }
    eax = N9_LO(eax) | n9_r16(m, edx + 0x32u);
    n9_w16(m, esi, (uint16_t)eax);
    n9_w16(m, edx + 0x32u, (uint16_t)(n9_r16(m, edx + 0x32u) + 1u));
    if (n9_r16(m, edx + 0x32u) == 0) n9_w16(m, edx + 0x32u, 0x8000u);
    edi = n9_r32(m, S - 0x10u);
    n9_w16(m, edx + 0x30u, (uint16_t)(n9_r16(m, edx + 0x30u) + 1u));
    {
        const int16_t hw = (int16_t)n9_r16(m, edx + 0x2Eu);
        eax = ebx + 1u;
        n9_w16(m, edx + 0x2Cu, (uint16_t)eax);
        if (!(hw > (int16_t)ebx)) n9_w16(m, edx + 0x2Eu, (uint16_t)eax);
    }
    eax = (uint32_t)(int32_t)(int16_t)n9_r16(m, esi);
    ecx = (uint32_t)(int32_t)(int16_t)ebx;
    eax = (eax << 16) | ecx;
pop:
    (void)fsw; (void)fsp0; s->backedges += be;
    R->esi = n9_r32(m, S - 0xCu); R->ebp = n9_r32(m, S - 8u); R->ebx = n9_r32(m, S - 4u);
    R->edi = edi; R->eax = eax; R->ecx = ecx;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    xv_object_math_unlock(&locked);
#endif
}

/* f_00056670 after its preamble: esp = E ([E+4] light, [E+8] &light list head, [E+0xC] &position, [E+0x10]
 * radius), eax = &location (cluster at +4), edi = the reference structure. */
static void n9_query_at(n9 *s, int tail_only)
{
    N9_LOCALS;
    xctx *c = s->c;
    const uint32_t E = s->E;
    uint32_t eax = c->r[0], ecx = c->r[1], edx = c->r[2], ebx = c->r[3], ebp = c->r[5], esi = c->r[6], edi = c->r[7];
    s->low = E - 0xA4u;
    n9_stk k; n9_stk_at(m, &k, E - 0x98u);                /* this frame: [E-0x98, E-0x7E) */
    if (tail_only) goto publish_tail;
    ecx = N9_LO(ecx) | n9_r16(m, eax + 4u);
    eax = 0;
    n9_sw32(m, &k, E - 0x88u, ebp);
    ebp = n9_r32(m, E + 8u);
    if ((uint16_t)ecx != 0xFFFFu) {
        double k1 = n9_rf(m, E + 0x10u);
        uint16_t cc = n9_cmp(s, k1, n9_rf(m, N9_EPS), 1);
        N9_SL(1, k1); s->touched |= 1u << 1;
        eax = fsw;
        if (cc & 0x4100) {
            eax = 1;
            n9_sw16(m, &k, E - 0x80u, (uint16_t)ecx);
        } else {
            eax = n9_i32(m, N9_EPOCH) + 1u;
            edx = E - 0x80u;
            n9_sw32(m, &k, E - 0x8Cu, edx);
            edx = n9_r32(m, E + 0xCu);
            n9_wi32(m, N9_EPOCH, eax);
            eax = n9_r32(m, E + 0x10u);
            n9_sw32(m, &k, E - 0x90u, 0x40u);
            n9_sw32(m, &k, E - 0x94u, eax);
            n9_wi8(m, N9_INUSE, 1);
            n9_sw32(m, &k, E - 0x98u, 0x566CBu);
            s->fsw = fsw;
            eax = n9_flood(s, E - 0x98u, eax, 0x40u, E - 0x80u, ecx, edx, ebx, ebp, esi, edi, &ecx, &edx);
            fsw = s->fsw;
            ebx = n9_r32(m, E - 0x98u - 0x10u); ebp = n9_r32(m, E - 0x98u - 0x14u);   /* its pops */
            esi = n9_r32(m, E - 0x98u - 0x18u); edi = n9_r32(m, E - 0x98u - 0x1Cu);
            n9_wi8(m, N9_INUSE, 0);
        }
    }
publish_tail:
    /* 566DE: prefix state is already present for a continuation. */
    if ((int16_t)eax > 0x40) eax = 0x40u;
    if ((int16_t)eax <= 0) {
        c->f_kind = XK_LOGIC; c->f_op1 = 0; c->f_op2 = 0; c->f_res = (uint16_t)eax; c->f_bits = 16;
        c->f_cf_override = 0; c->f_of_override = 0;
    } else {
        ecx = (uint16_t)eax;
        n9_sw32(m, &k, E - 0x8Cu, ebx);
        n9_sw32(m, &k, E - 0x90u, esi);
        ebx = E - 0x80u;
        n9_sw32(m, &k, E - 0x84u, ecx);
        for (;;) {
            edx = n9_r32(m, edi + 8u);
            esi = N9_LO(esi) | n9_r16(m, ebx);
            n9_sw32(m, &k, E - 0x94u, 0x5670Bu);
            n9_regs r = { eax, ecx, edx, ebx, ebp, esi, edi };
            n9_datum_new(s, &r, E - 0x94u);
            eax = r.eax; ecx = r.ecx; ebx = r.ebx; ebp = r.ebp; esi = r.esi; edi = r.edi;
            if (eax != 0xFFFFFFFFu) {
                edx = n9_r32(m, edx + 0x34u);
                ecx = edx + (eax & 0xFFFFu) * 12u;
                edx = (uint32_t)(int32_t)(int16_t)esi;
                n9_w32(m, ecx + 4u, edx);
                edx = n9_r32(m, ebp);
                n9_w32(m, ecx + 8u, edx);
                n9_w32(m, ebp, eax);
            }
            ecx = n9_r32(m, edi);
            edx = n9_r32(m, edi + 4u);
            eax = (uint32_t)(int32_t)(int16_t)esi;
            esi = ecx + eax * 4u;
            n9_sw32(m, &k, E - 0x94u, 0x56740u);
            r = (n9_regs){ eax, ecx, edx, ebx, ebp, esi, edi };
            n9_datum_new(s, &r, E - 0x94u);
            eax = r.eax; ecx = r.ecx; ebx = r.ebx; ebp = r.ebp; esi = r.esi; edi = r.edi;
            if (eax != 0xFFFFFFFFu) {
                edx = n9_r32(m, edx + 0x34u);
                ecx = edx + (eax & 0xFFFFu) * 12u;
                edx = n9_r32(m, E + 4u);
                n9_w32(m, ecx + 4u, edx);
                edx = n9_r32(m, esi);
                n9_w32(m, ecx + 8u, edx);
                n9_w32(m, esi, eax);
            }
            eax = n9_r32(m, E - 0x84u);
            const uint32_t b0 = ebx;
            ebx += 2u;
            const uint32_t a0 = eax;
            eax -= 1u;
            n9_sw32(m, &k, E - 0x84u, eax);
            if (eax) { be++; continue; }
            c->f_kind = XK_SUB; c->f_op1 = a0; c->f_op2 = 1; c->f_res = eax; c->f_bits = 32;
            c->f_cf_override = 1; c->f_cf = ebx < b0; c->f_of_override = 0;
            break;
        }
        esi = n9_r32(m, E - 0x90u);
        ebx = n9_r32(m, E - 0x8Cu);
    }
    ebp = n9_r32(m, E - 0x88u);
    c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx; c->r[3] = ebx; c->r[4] = E + 0x14u; c->r[5] = ebp; c->r[6] = esi; c->r[7] = edi;
    for (unsigned k = 1; k < 8; ++k) if (s->touched & (1u << k)) c->st[(s->fsp0 - k) & 7u] = s->sl[k];
    c->fsw = fsw; s->fsw = fsw; s->backedges += be;
}

static void n9_query(n9 *s) { n9_query_at(s, 0); }

/* ---- modes, budget, counters ------------------------------------------------------------------------------ */
/* The guest's back-edge budget: X_PREEMPT() per back-edge, in one go at the end (xk_native_visibility.c). */
static inline void n9_budget(xctx *c, uint32_t backedges)
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
static inline uint64_t n9_ns(void) { return xk_os_monotonic_us() * 1000u; }
#else
#include <time.h>
static inline uint64_t n9_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
#endif

enum { N9_CALLS, N9_VERIFIED, N9_MISMATCHED, N9_DECLINED, N9_FLOODS, N9_CLUSTERS, N9_PORTALS, N9_FULL, N9_DATUMS,
       N9_DIRTY, N9_TIMED_NATIVE, N9_TIMED_GUEST, N9_JOURNAL_FAIL, N9_NAN_WORDS, N9_BACKEDGES, N9_OVERLAP_SKIP, N9_TAILS, N9_COUNTERS };
static unsigned n9_counter[N9_COUNTERS], n9_mismatch_total;
static uint64_t n9_native_ns, n9_guest_ns;
#define N9_ADD(i, v) __atomic_fetch_add(&n9_counter[i], (unsigned)(v), __ATOMIC_RELAXED)

static int n9_mode_value = -1;
static int n9_mode(void)
{
    int mode = __atomic_load_n(&n9_mode_value, __ATOMIC_RELAXED);
    if (mode < 0) {
        const char *e = getenv("XV_NATIVE_92330"); mode = e ? atoi(e) : XV_NATIVE_92330_DEFAULT;
        n9_watch = getenv("XV_WATCH_ADDR") != NULL;
        { const char *t = getenv("XV_NATIVE_92330_TIME"); n9_detail = mode == 1 || (t && atoi(t) != 0); }
        if (mode < 0 || mode > 2) mode = 0;
        int expected = -1;
        if (__atomic_compare_exchange_n(&n9_mode_value, &expected, mode, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            XK_LOG("[native-92330] f_00056670 light cluster query (+52240/51E90/11840/B77C0/A9330): %s\n",
                   mode == 2 ? "native" : mode == 1 ? "verify (native vs guest, guest result kept)" : "off");
        else mode = expected;
    }
    return mode;
}
/* Tests: select the mode directly (0 off, 1 verify, 2 native), bypassing the environment. */
void xv_native_92330_force(int mode) { __atomic_store_n(&n9_mode_value, mode < 0 || mode > 2 ? 0 : mode, __ATOMIC_RELAXED); n9_detail = 1; }
static int n9_timing(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("XV_NATIVE_92330_TIME"); on = e && atoi(e) != 0; }
    return on;
}
static inline void n9_init(n9 *s, xctx *c)
{
    s->m.ram = g_xram; s->m.pt = X_PT; s->m.img = X_IMG_BASE; s->m.jon = 0;
    s->c = c; s->E = c->r[4]; s->fsp0 = c->fsp; s->fsw = c->fsw; s->touched = 0; s->backedges = 0; s->dirty = 0;
    s->cl_min = 0x7FFF; s->cl_max = -0x8000; s->portals = s->full = s->clusters = s->datums = s->flood = 0;
}
#ifdef XV_NATIVE_566DE_TEST
int xv_native_566de(xctx *c);
int xv_native_566de_test(xctx *c) { return xv_native_566de(c); }
#endif
#ifdef XV_NATIVE_52240_TEST
/* Differential-test candidate only: no runtime hook or user setting. The
 * encompassing light query normally overwrites these exit flags. */
static uint64_t n52240_test_counts[3];
void xv_native_52240_test_counts(uint64_t out[3]) { memcpy(out, n52240_test_counts, sizeof n52240_test_counts); }
void xv_native_52240_test(xctx *c)
{
    n9 s; n9_init(&s, c); s.low = s.E;
    const n9_mem *m = &s.m;
    const uint32_t E = s.E;
    uint32_t ecx, edx;
    uint32_t eax = n9_flood(&s, E, n9_r32(m, E + 4), n9_r32(m, E + 8),
                           n9_r32(m, E + 12), c->r[1], c->r[2], c->r[3],
                           c->r[5], c->r[6], c->r[7], &ecx, &edx);
    c->r[0] = eax; c->r[1] = ecx; c->r[2] = edx;
    c->r[3] = n9_r32(m, E - 0x10); c->r[5] = n9_r32(m, E - 0x14);
    c->r[6] = n9_r32(m, E - 0x18); c->r[7] = n9_r32(m, E - 0x1C);
    c->r[4] = E + 16;
    c->f_kind = s.flood_flag_kind;
    c->f_op1 = s.flood_flag_kind == XK_SUB ? s.flood_flag_a : 0;
    c->f_op2 = s.flood_flag_b;
    c->f_res = s.flood_flag_a - s.flood_flag_b; c->f_bits = 32;
    c->f_cf_override = c->f_of_override = 0;
    for (unsigned k = 1; k < 8; ++k)
        if (s.touched & (1u << k)) c->st[(s.fsp0 - k) & 7u] = s.sl[k];
    c->fsw = s.fsw;
    n9_budget(c, s.backedges);
    n52240_test_counts[0]++; n52240_test_counts[1] += s.flood; n52240_test_counts[2] += s.portals;
}
#endif
static void n9_count(const n9 *s)
{
    N9_ADD(N9_CALLS, 1);
    if (!n9_detail) return;
    N9_ADD(N9_FLOODS, s->flood ? 1 : 0); N9_ADD(N9_CLUSTERS, s->clusters); N9_ADD(N9_PORTALS, s->portals);
    N9_ADD(N9_FULL, s->full); N9_ADD(N9_DATUMS, s->datums); N9_ADD(N9_DIRTY, s->dirty ? 1 : 0); N9_ADD(N9_BACKEDGES, s->backedges);
}

/* ---- verify mode ------------------------------------------------------------------------------------------- */
enum { N9_REGIONS = 8, N9_REGION_CAP = 0x40000u };
typedef struct {
    int timing_only;
    uint64_t t0;
    xctx before, native;
    uint32_t native_backedges;
    n9_jent *j; unsigned nj;
    unsigned nreg; uint32_t reg_addr[N9_REGIONS], reg_len[N9_REGIONS];
    uint8_t *img;                 /* the regions after the native, concatenated */
    unsigned portals, clusters, datums, nan_words;
} n9_verify;

static void n9_region(n9_verify *v, uint32_t a, uint32_t len)
{
    if (!len || v->nreg == N9_REGIONS) return;
    if (len > N9_REGION_CAP) len = N9_REGION_CAP;
    v->reg_addr[v->nreg] = a; v->reg_len[v->nreg] = len; v->nreg++;
}
/* What the guest body may write (by inspection of 56670/52240/51E90/B77C0/A9330): the stack below the entry esp
 * down to the deepest frame, the epoch/in-use globals, the visited stamps, both reference arrays (header and
 * elements), the per-cluster list heads and the light's list head. Everything the native wrote is journaled too. */
static void n9_regions(n9_verify *v, const n9 *s, const xctx *c0)
{
    const n9_mem *m = &s->m;
    const uint32_t E = c0->r[4], ref = c0->r[7];
    n9_region(v, s->low - 0x100u, E + 0x14u - (s->low - 0x100u));
    n9_region(v, 0x2D2FA8u, 8);
    const uint32_t bsp = n9_i32(m, N9_BSP);
    int32_t ncl = (int32_t)n9_r32(m, bsp + 0x134u);
    if (ncl < 0 || ncl > 0x8000) ncl = 0x8000;
    int32_t lo = s->cl_min < 0 ? s->cl_min : 0, hi = s->cl_max + 1 > ncl ? s->cl_max + 1 : ncl;
    n9_region(v, N9_STAMPS + 4u * (uint32_t)lo, 4u * (uint32_t)(hi - lo));
    for (unsigned k = 1; k <= 2; ++k) {
        const uint32_t A = n9_r32(m, ref + 4u * k);
        const int32_t max = (int16_t)n9_r16(m, A + 0x20u), size = (int16_t)n9_r16(m, A + 0x22u);
        n9_region(v, A, 0x38);
        if (max > 0 && size > 0) n9_region(v, n9_r32(m, A + 0x34u), (uint32_t)(max * size));
    }
    n9_region(v, n9_r32(m, ref) + 4u * (uint32_t)lo, 4u * (uint32_t)(hi - lo));
    n9_region(v, n9_r32(m, E + 8u), 4);
}
static int n9_same_double(double a, double b) { return !memcmp(&a, &b, sizeof a) || (a != a && b != b); }
/* A float NaN in both results: which NaN payload an operation propagates is the host compiler's register choice
 * (unobservable: the subtree only stores such floats to its dead stack frame and x87 scratch slots). */
static inline int n9_nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static void n9_report_mismatch(const char *what, uint32_t a, uint32_t b, const n9_verify *v)
{
    if (__atomic_add_fetch(&n9_mismatch_total, 1, __ATOMIC_RELAXED) <= 12)
        XK_LOG("[native-92330] MISMATCH %s native %08X guest %08X (query: %u clusters, %u portals, %u datums)\n",
               what, a, b, v->clusters, v->portals, v->datums);
}

int xv_scene_thread_overlapped(void) __attribute__((weak));     /* xk_scene_thread.c */
int xv_scene_thread_on_helper(void) __attribute__((weak));
int xv_scene_thread_helper_done(void) __attribute__((weak));
/* Called only after a successful typed prefix and census-end, while the
 * original 56670 transaction guard is still held. No undo/verify here: mode1
 * retains the translated tail. ESP is entryESP-0x88 at this continuation. */
int xv_native_566de(xctx *c)
{
    if (n9_mode() != 2) return 0;
    if (xv_scene_thread_on_helper && xv_scene_thread_on_helper()) return 0;
    n9 s; n9_init(&s, c); s.E += 0x88u;
    n9_query_at(&s, 1);
    n9_budget(c, s.backedges);
    n9_count(&s); N9_ADD(N9_TAILS, 1);
    return 1;
}

/* Hook inside f_00056670 after its preamble: 1 = handled (the guest body is skipped). 0 = run the guest body; a
 * token set in *token asks the body's return sites to call xv_native_92330_post. */
int xv_native_92330(xctx *c, void **token)
{
    const int mode = n9_mode();
    const int timed = n9_timing();
#ifdef XV_LIGHT_QUERY_CENSUS
    if (__atomic_load_n(&xv_light_census_enabled, __ATOMIC_RELAXED)) { N9_ADD(N9_DECLINED, 1); return 0; }
#endif
    /* The query is tick work. On the scene helper a stage with XV_QSERIAL routes the serial/stamp accesses (X_QS8/32)
     * to the helper's private copy, which the native does not model: never native there. */
    if (xv_scene_thread_on_helper && xv_scene_thread_on_helper()) { N9_ADD(N9_DECLINED, 1); return 0; }
    if (!mode) {
        if (!timed) return 0;
        n9_verify *v = malloc(sizeof *v);
        if (!v) return 0;
        v->timing_only = 1; *token = v; v->t0 = n9_ns();
        return 0;
    }
    n9 s;
    if (mode == 2) {
        const uint64_t t0 = timed ? n9_ns() : 0;
        n9_init(&s, c);
        n9_query(&s);
        if (timed) { __atomic_fetch_add(&n9_native_ns, n9_ns() - t0, __ATOMIC_RELAXED); N9_ADD(N9_TIMED_NATIVE, 1); }
        n9_budget(c, s.backedges); n9_count(&s);
        return 1;
    }
    /* verify: native with a write journal, record its result, undo it, let the guest body run on the same state.
     * Not while an overlapped scene may still merge its render view: the merge copies every word the scene changed
     * into live memory where live still equals the scene's pristine copy (tick wins otherwise), and the epoch
     * 0x2D2FAC, the in-use byte and the visited stamps are shared with the scene's own cluster traversals - between
     * the undo and the guest run those words look untouched by the tick and would be overwritten under the check.
     * Such calls run the guest body alone (counted as overlap-skipped). */
    if (xv_scene_thread_overlapped && xv_scene_thread_overlapped() && !(xv_scene_thread_helper_done && xv_scene_thread_helper_done())) {
        N9_ADD(N9_OVERLAP_SKIP, 1);
        return 0;
    }
    n9_verify *v = calloc(1, sizeof *v);
    if (!v) return 0;
    v->before = *c;
    n9_j.n = 0; n9_j.overflow = 0;
    const uint64_t t0 = timed ? n9_ns() : 0;
    n9_init(&s, c); s.m.jon = 1;
    n9_query(&s);
    const uint64_t t1 = timed ? n9_ns() : 0;
    if (timed) { __atomic_fetch_add(&n9_native_ns, t1 - t0, __ATOMIC_RELAXED); N9_ADD(N9_TIMED_NATIVE, 1); }
    v->native = *c; v->native_backedges = s.backedges;
    v->portals = s.portals; v->clusters = s.clusters; v->datums = s.datums;
    n9_regions(v, &s, &v->before);
    uint32_t total = 0;
    for (unsigned i = 0; i < v->nreg; ++i) total += v->reg_len[i];
    v->img = malloc(total ? total : 1);
    v->nj = n9_j.n;
    v->j = malloc((v->nj ? v->nj : 1) * sizeof *v->j);
    if (!v->img || !v->j || n9_j.overflow) {
        /* cannot verify this call: keep the native result (it is complete), account for its budget */
        N9_ADD(N9_JOURNAL_FAIL, 1);
        free(v->img); free(v->j); free(v);
        n9_budget(c, s.backedges); n9_count(&s);
        return 1;
    }
    for (unsigned i = 0, o = 0; i < v->nreg; o += v->reg_len[i], ++i) x_guest_read_pages(v->img + o, v->reg_addr[i], v->reg_len[i]);
    const n9_mem *m = &s.m;
    for (unsigned i = 0; i < v->nj; ++i) {
        v->j[i] = n9_j.e[i]; memcpy(v->j[i].now, N9_P(v->j[i].addr), v->j[i].size);
        x_guest_read_pages(&v->j[i].word, v->j[i].addr & ~3u, 4);
    }
    for (unsigned i = v->nj; i-- > 0;) memcpy(N9_P(v->j[i].addr), v->j[i].old, v->j[i].size);
    n9_count(&s);
    *c = v->before; c->preempt = 1 << 30;
    *token = v; v->t0 = timed ? n9_ns() : 0;
    return 0;
}

/* Return sites of the guest body of f_00056670 (after `add esp`/`ret 10h`): timing, and in verify mode the
 * comparison of the guest's result with the native's. */
void xv_native_92330_post(xctx *c, void *token)
{
    n9_verify *v = token;
    if (n9_timing()) { __atomic_fetch_add(&n9_guest_ns, n9_ns() - v->t0, __ATOMIC_RELAXED); N9_ADD(N9_TIMED_GUEST, 1); }
    if (v->timing_only) { free(v); return; }
    const xctx *n = &v->native;
    const uint32_t guest_backedges = (uint32_t)((1 << 30) - c->preempt);
    unsigned bad = 0;
#define N9_CMP(what, a, b) do { if ((a) != (b)) { bad++; n9_report_mismatch(what, (uint32_t)(a), (uint32_t)(b), v); } } while (0)
    static const char *const rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    for (unsigned i = 0; i < 8; ++i) N9_CMP(rn[i], n->r[i], c->r[i]);
    N9_CMP("backedges", v->native_backedges, guest_backedges);
    N9_CMP("fsp", n->fsp, c->fsp); N9_CMP("fcw", n->fcw, c->fcw); N9_CMP("fsw", n->fsw, c->fsw); N9_CMP("df", n->df, c->df);
    N9_CMP("f_kind", n->f_kind, c->f_kind); N9_CMP("f_op1", n->f_op1, c->f_op1); N9_CMP("f_op2", n->f_op2, c->f_op2);
    N9_CMP("f_res", n->f_res, c->f_res); N9_CMP("f_bits", n->f_bits, c->f_bits);
    N9_CMP("f_cf_override", n->f_cf_override, c->f_cf_override); N9_CMP("f_of_override", n->f_of_override, c->f_of_override);
    if (c->f_cf_override) N9_CMP("f_cf", n->f_cf, c->f_cf);
    if (c->f_of_override) N9_CMP("f_of", n->f_of, c->f_of);
    N9_CMP("fs_base", n->fs_base, c->fs_base); N9_CMP("scratch", n->scratch, c->scratch); N9_CMP("eip_hint", n->eip_hint, c->eip_hint);
    for (unsigned i = 0; i < 8; ++i)
        if (!n9_same_double(n->st[i], c->st[i])) {
            uint64_t a, b; memcpy(&a, &n->st[i], 8); memcpy(&b, &c->st[i], 8);
            char w[40]; snprintf(w, sizeof w, "st[%u] fsp %u (lo word)", i, c->fsp); bad++; n9_report_mismatch(w, (uint32_t)a, (uint32_t)b, v);
        }
    if (memcmp(n->mm, c->mm, sizeof c->mm) || memcmp(n->xmm, c->xmm, sizeof c->xmm)) { bad++; n9_report_mismatch("mm/xmm", 0, 1, v); }
    /* every byte the native wrote holds the same final value after the guest (NaN words: any payload); read through
     * one translation like the write (an X_W32 at a page end continues in the same host page) */
    const n9_mem mm = { g_xram, X_PT, X_IMG_BASE, 0 }; const n9_mem *m = &mm;
    for (unsigned i = 0; i < v->nj; ++i) {
        uint8_t now[4]; memcpy(now, N9_P(v->j[i].addr), v->j[i].size);
        if (memcmp(now, v->j[i].now, v->j[i].size)) {
            uint32_t gw; x_guest_read_pages(&gw, v->j[i].addr & ~3u, 4);
            if (n9_nan32(gw) && n9_nan32(v->j[i].word)) { v->nan_words++; continue; }
            uint32_t a = 0, b = 0; memcpy(&a, v->j[i].now, v->j[i].size); memcpy(&b, now, v->j[i].size);
            char w[40]; snprintf(w, sizeof w, "write@%08X", v->j[i].addr); bad++; n9_report_mismatch(w, a, b, v);
            break;
        }
    }
    /* and the guest wrote nothing else in the regions it may write */
    uint32_t total = 0;
    for (unsigned i = 0; i < v->nreg; ++i) total += v->reg_len[i];
    uint8_t *cur = malloc(total ? total : 1);
    if (cur) {
        for (unsigned i = 0, o = 0; i < v->nreg; o += v->reg_len[i], ++i) {
            x_guest_read_pages(cur + o, v->reg_addr[i], v->reg_len[i]);
            if (!memcmp(cur + o, v->img + o, v->reg_len[i])) continue;
            for (uint32_t k = 0; k < v->reg_len[i]; ++k) {
                if (cur[o + k] == v->img[o + k]) continue;
                const uint32_t a = v->reg_addr[i] + k, w0 = (a & ~3u) - v->reg_addr[i];
                if ((a & ~3u) >= v->reg_addr[i] && w0 + 4u <= v->reg_len[i]) {
                    uint32_t gw, nw; memcpy(&gw, cur + o + w0, 4); memcpy(&nw, v->img + o + w0, 4);
                    if (n9_nan32(gw) && n9_nan32(nw)) { v->nan_words++; k = w0 + 3u; continue; }
                }
                char w[48]; snprintf(w, sizeof w, "region%u@%08X (byte)", i, a);
                bad++; n9_report_mismatch(w, v->img[o + k], cur[o + k], v); break;
            }
        }
        free(cur);
    }
#undef N9_CMP
    c->preempt = v->before.preempt;
    n9_budget(c, guest_backedges);
    N9_ADD(N9_VERIFIED, 1); N9_ADD(N9_NAN_WORDS, v->nan_words);
    if (bad) N9_ADD(N9_MISMATCHED, 1);
    free(v->img); free(v->j); free(v);
}

void xv_native_92330_report(unsigned frames)
{
    unsigned n[N9_COUNTERS];
    for (unsigned i = 0; i < N9_COUNTERS; ++i) n[i] = __atomic_exchange_n(&n9_counter[i], 0u, __ATOMIC_RELAXED);
    const uint64_t native_ns = __atomic_exchange_n(&n9_native_ns, 0, __ATOMIC_RELAXED), guest_ns = __atomic_exchange_n(&n9_guest_ns, 0, __ATOMIC_RELAXED);
    if (!n[N9_CALLS] && !n[N9_TIMED_GUEST] && !n[N9_DECLINED] && !n[N9_OVERLAP_SKIP]) return;
    char timing[112] = "";
    if (n[N9_TIMED_NATIVE])
        snprintf(timing, sizeof timing, "; us/call native %.3f", (double)native_ns / 1000.0 / n[N9_TIMED_NATIVE]);
    if (n[N9_TIMED_GUEST])
        snprintf(timing + strlen(timing), sizeof timing - strlen(timing), "%s guest %.3f (%u timed)", n[N9_TIMED_NATIVE] ? "" : "; us/call",
                 (double)guest_ns / 1000.0 / n[N9_TIMED_GUEST], n[N9_TIMED_GUEST]);
    XK_LOG("[native-92330] %u frames: calls %u verified %u mismatched %u (total mismatches %u) declined %u journal-fail %u overlap-skipped %u; "
           "floods %u clusters %u portals %u full %u datums %u back-edges %u dirty %u nan-words %u tails %u%s\n",
           frames, n[N9_CALLS], n[N9_VERIFIED], n[N9_MISMATCHED], __atomic_load_n(&n9_mismatch_total, __ATOMIC_RELAXED),
           n[N9_DECLINED], n[N9_JOURNAL_FAIL], n[N9_OVERLAP_SKIP], n[N9_FLOODS], n[N9_CLUSTERS], n[N9_PORTALS], n[N9_FULL], n[N9_DATUMS], n[N9_BACKEDGES], n[N9_DIRTY], n[N9_NAN_WORDS], n[N9_TAILS], timing);
}
