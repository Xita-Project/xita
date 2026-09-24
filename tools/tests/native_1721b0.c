/* Differential test: native f_00088E90 BSP segment cast (recomp/kernel/xk_native_1721b0.c) against the lifted guest
 * bodies of f_00088E90 + f_00088B80 + f_000889E0 + f_0017ADD0 + f_00086E20 and the kernel's xv_bsp_plane_interval
 * (extracted from a stage by tools/test_native_1721b0.py), on randomized casts in a synthetic guest arena: shuffled
 * physical pages (records that straddle page ends, split float loads), reversed stack pages, random BSP3D trees whose
 * planes straddle the segment, chains of planes across it (more than 0x100 leaves: the leaf list's overflow slot;
 * more than 1024 levels: the native hands the rest to the translation), leaves of both kinds and outside (-1), BSP2D
 * references on the crossed planes (and on others), BSP2D trees, surfaces with closed edge rings around the crossing
 * point (either orientation) or beside it, the surface filter bits, random S flags, words and masks, NaN and infinite
 * coordinates, zero / parallel directions, random image constants (0.0, 1.0, the axes table: indices outside 0..2 read
 * other frame slots), the result record in the caller's frame (the game's place), elsewhere, over S and 88E90's
 * arguments, over the point / delta vectors (the cast's own writes change what it reads), and the layouts the native
 * declines (esp unaligned, the record inside the 64 KB below the frame).
 * Each case runs the guest body and the hook from identical state and compares the whole arena, every xctx field and
 * the xv_preempt call count; a declined case runs the guest (as the game does) and must match too; with --verify it
 * also runs the verify mode (mode 1) and requires the guest result and no MISMATCH line. --bench N: game-like casts,
 * guest vs native ns/call and (Linux perf counters) user instructions/cycles per call. --replay <file> [reps]: casts
 * captured in the game (XV_NATIVE_1721B0_CAPTURE). */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#endif
#include <pthread.h>
#include "xv_x86rt.h"

uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#endif
int xv_phase_enabled;
void xv_phase_begin(void *s, void *c, unsigned id) { (void)s; (void)c; (void)id; }
void xv_phase_end(void *s) { (void)s; }
static int log_mismatch, log_all;
void xk_os_log(const char *fmt, ...)
{
    if (strstr(fmt, "MISMATCH")) log_mismatch++;
    if (log_all || getenv("NR_DEBUG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
}
uint64_t xk_os_monotonic_us(void) { return 0; }
static unsigned preempt_calls; static int slice = 37;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = slice; }
void xv_trap(xctx *c, uint32_t eip) { (void)c; fprintf(stderr, "trap %08X\n", eip); abort(); }
void x_guest_read_pages(void *dst, uint32_t a, size_t size)
{
    uint8_t *d = dst;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(d, X_G(a), n); d += n; a += (uint32_t)n; size -= n; }
}
void x_guest_write_pages(uint32_t a, const void *src, size_t size)
{
    const uint8_t *s = src;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(X_G(a), s, n); s += n; a += (uint32_t)n; size -= n; }
}
extern void f_00088E90(xctx *);
/* in a stage the hook's translation is the renamed body (tools/patch_native_1721b0_hooks.py); here the lifted body */
void f_00088E90_body(xctx *c) { f_00088E90(c); }
extern void xv_native_1721b0_force(int);
extern void xv_native_1721b0_detail(int);
extern int xv_native_1721b0_ray(xctx *);
extern void xv_native_1721b0_report(unsigned);

enum { ARENA = 8u << 20, IMG_LO = 0x1E0, IMG_PAGES = 0x20, TAG = 0x40000000u, TAG_PAGES = 1400, STACK = 0xD0000000u, STACK_PAGES = 24 };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
static uint32_t r32(uint32_t a) { uint32_t v; x_guest_read_pages(&v, a, 4); return v; }
static float nanf_(void) { uint32_t b = 0x7FC00000u | (rnd() & 0x3FFFFF); float f; memcpy(&f, &b, 4); return f; }

static void map_memory(void)
{
    g_xram = calloc(1, ARENA); g_xpt = malloc(sizeof(uint32_t) << 20);
    trash_off = ARENA - 4096; for (uint32_t i = 0; i < (1u << 20); ++i) g_xpt[i] = trash_off;
    next_free = 0;
    for (uint32_t i = 0; i < IMG_PAGES; ++i) g_xpt[IMG_LO + i] = next_free + (i << 12);     /* image constants (0x1EAF30, 0x1F0A68) */
    next_free += IMG_PAGES << 12;
    g_img_base = g_xram;                              /* unused by this subtree (no X_IMG access) */
    uint32_t *perm = malloc(sizeof(uint32_t) * TAG_PAGES); for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = next_free + (perm[i] << 12);   /* shuffled physical pages */
    next_free += TAG_PAGES << 12; free(perm);
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = next_free + ((STACK_PAGES - 1 - i) << 12);   /* reversed */
    next_free += STACK_PAGES << 12;
    if (next_free > trash_off) { fprintf(stderr, "arena too small\n"); exit(2); }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
}

static uint32_t tag_top;
static uint32_t tag_alloc(uint32_t bytes, int unaligned)
{
    uint32_t a = (tag_top + 3u) & ~3u; if (unaligned) a += 1 + rnd() % 3;
    tag_top = a + bytes; if (tag_top > TAG + (TAG_PAGES << 12) - 64) { fprintf(stderr, "tag space exhausted\n"); exit(2); }
    return a;
}
/* a record that straddles a page end now and then (tag pages are shuffled: the next guest page is elsewhere) */
static uint32_t rec_alloc(uint32_t bytes)
{
    if (rnd() % 9 == 0) { uint32_t end = (tag_top | 0xFFFu) + 1u; if (end - tag_top < 64) tag_top = end - (bytes / 2 + 1 + rnd() % 3); }
    return tag_alloc(bytes, rnd() % 11 == 0);
}

typedef struct { unsigned cases, native_ran, declined, timeouts, mismatches, verify_mismatch, verify_timeouts, nan_words, hits, chains, deep, aliased, overflow; } stats;
static int nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }

static uint32_t E_top;            /* esp at the entry of f_00088E90 */
static int bench_like;            /* game-like casts (a30 sound rays: ~16 nodes, ~0.25 leaf tests per cast) */
static int stack_only;            /* --threads: every word the cast writes on the stack pages (the game's layout) */
static float odd(float x, int flavor) { if (flavor == 2 && rnd() % 29 == 0) return rnd() % 3 ? nanf_() : rnd() % 2 ? INFINITY : -INFINITY; return x; }
typedef struct { float n[3], d; } plane;
static void random_normal(float *n, int flavor)
{
    if (flavor == 1 || rnd() % 5 == 0) { n[0] = n[1] = n[2] = 0; n[rnd() % 3] = rnd() & 1 ? 1.0f : -1.0f; return; }
    if (rnd() % 7 == 0) { float a = 0.70710677f; for (unsigned i = 0; i < 3; ++i) n[i] = rnd() & 1 ? a : -a; n[rnd() % 3] = 0; return; }
    float l;
    do { for (unsigned i = 0; i < 3; ++i) n[i] = frand(-1, 1); l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); } while (l < 0.05f);
    for (unsigned i = 0; i < 3; ++i) n[i] /= l;
}
/* the axes table the scene uses (the (u, v) of the projected plane per (axis, side)) */
static int16_t axes_u[6], axes_v[6];

static void scene(stats *st)
{
    memset(g_xram, 0, ARENA);
    tag_top = TAG + (rnd() % 64) * 4;
    const int flavor = bench_like ? 0 : rnd() % 4 == 0 ? 1 : rnd() % 8 == 0 ? 2 : 0;
    const int huge = !bench_like && flavor == 0 && rnd() % 12 == 0;    /* coordinates ~1e16: the sums' association shows */
    const int chain = !bench_like && rnd() % 12 == 0;                  /* planes across the segment: many leaves */
    const int deep = chain && rnd() % 12 == 0;                         /* > 1024 levels */
    const int clean = chain && (deep || rnd() % 2);                    /* no plane in the chain breaks it */
    /* image constants */
    for (unsigned a = 0; a < 6; ++a) {
        int16_t u = (int16_t)((a / 2 + 1) % 3), v = (int16_t)((a / 2 + 2) % 3);
        if (a & 1) { int16_t t = u; u = v; v = t; }
        if (!bench_like && rnd() % 37 == 0) u = (int16_t)((int)(rnd() % 11) - 4);
        if (!bench_like && rnd() % 37 == 0) v = (int16_t)((int)(rnd() % 11) - 4);
        axes_u[a] = u; axes_v[a] = v;
        w16(0x1EAF30 + 4 * a, (uint16_t)u); w16(0x1EAF32 + 4 * a, (uint16_t)v);
    }
    for (uint32_t a = 0x1EAF30 - 0x40; a < 0x1EAF30; a += 2) w16(a, (uint16_t)(int16_t)((int)(rnd() % 9) - 4));
    for (uint32_t a = 0x1EAF48; a < 0x1EAF48 + 0x100; a += 2) w16(a, (uint16_t)(int16_t)((int)(rnd() % 9) - 4));
    wf(0x1F0A68, bench_like || rnd() % 6 ? 0.0f : rnd() % 2 ? -0.0f : rnd() % 3 ? frand(-0.01f, 0.01f) : nanf_());
    wf(0x1F0A78, bench_like || rnd() % 6 ? 1.0f : rnd() % 3 ? frand(0.2f, 2.0f) : nanf_());
    /* the segment point + t * delta */
    float P[3] = { frand(-6, 6), frand(-6, 6), frand(-6, 6) }, D[3];
    const unsigned dk = bench_like ? 0 : rnd() % 14;
    if (dk == 1) D[0] = D[1] = D[2] = 0;
    else if (dk == 2 || flavor == 1) { D[0] = D[1] = D[2] = 0; D[rnd() % 3] = frand(-12, 12); }
    else for (unsigned k = 0; k < 3; ++k) D[k] = frand(-10, 10);
    if (flavor == 1) for (unsigned k = 0; k < 3; ++k) P[k] = (float)(int)P[k];
    if (huge) for (unsigned k = 0; k < 3; ++k) { if (rnd() % 3) P[k] *= 3e15f; if (rnd() % 2) D[k] *= 1e14f; }   /* mixed magnitudes */
    for (unsigned k = 0; k < 3; ++k) { P[k] = odd(P[k], flavor); D[k] = odd(D[k], flavor); }
    /* BSP */
    uint32_t bsp = tag_alloc(0x60, 0);
    int n3 = bench_like ? 60 : 3 + (int)(rnd() % 80), np = bench_like ? 40 : 4 + (int)(rnd() % 50), nl = bench_like ? 40 : 2 + (int)(rnd() % 60);
    if (chain) { n3 = deep ? 1100 + (int)(rnd() % 60) : 260 + (int)(rnd() % 80); np = n3; nl = n3 + 2; }
    const int ns = bench_like ? 40 : 4 + (int)(rnd() % 50), n2 = bench_like ? 60 : 2 + (int)(rnd() % 60);
    const int nv = ns * 6 + 8, ne = ns * 6 + 8, nr = nl * 3 + 8;
    uint32_t planes = tag_alloc((uint32_t)np * 16, rnd() % 13 == 0), nodes3 = tag_alloc((uint32_t)n3 * 12, rnd() % 13 == 0);
    uint32_t leaves = tag_alloc((uint32_t)nl * 8, 0), refs = tag_alloc((uint32_t)nr * 8, 0), nodes2 = tag_alloc((uint32_t)n2 * 20, rnd() % 13 == 0);
    uint32_t surfs = tag_alloc((uint32_t)ns * 12, 0), edges = tag_alloc((uint32_t)ne * 24, 0), verts = tag_alloc((uint32_t)nv * 16, rnd() % 17 == 0);
    w32(bsp + 4, nodes3); w32(bsp + 0x10, planes); w32(bsp + 0x1C, leaves); w32(bsp + 0x28, refs);
    w32(bsp + 0x34, nodes2); w32(bsp + 0x40, surfs); w32(bsp + 0x4C, edges); w32(bsp + 0x58, verts);
    for (uint32_t a = bsp + 0x5C; a < bsp + 0x60; a += 4) w32(a, rnd());
    plane *pl = malloc(sizeof(plane) * (size_t)np);
    float Dlen = sqrtf(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
    for (int i = 0; i < np; ++i) {
        if (chain && Dlen > 1e-3f && Dlen == Dlen && (clean || rnd() % 64)) {   /* across the segment at t = (i+0.5)/np */
            for (unsigned k = 0; k < 3; ++k) pl[i].n[k] = D[k] / Dlen;
            if (!clean && rnd() % 16 == 0) { float r[3]; random_normal(r, 0); for (unsigned k = 0; k < 3; ++k) pl[i].n[k] += 0.3f * r[k]; }
        } else random_normal(pl[i].n, flavor);
        const float t = chain ? ((float)i + frand(0.3f, 0.7f)) / (float)np : frand(-0.3f, 1.3f);
        float through = 0;
        for (unsigned k = 0; k < 3; ++k) through += pl[i].n[k] * (P[k] + t * D[k]);
        pl[i].d = (clean ? 1 : chain ? rnd() % 97 : rnd() % 6) ? through : frand(-10, 10);
        if (flavor == 1) pl[i].d = (float)(int)pl[i].d;
        if (!bench_like && rnd() % 41 == 0) pl[i].d = through + 0.0f * pl[i].n[0];   /* exactly through */
        for (unsigned k = 0; k < 3; ++k) wf(planes + 16u * (uint32_t)i + 4 * k, odd(pl[i].n[k], flavor));
        wf(planes + 16u * (uint32_t)i + 12, odd(pl[i].d, flavor));
    }
    /* BSP3D nodes: a tree (node i's children 2i+1 / 2i+2 when they exist, else leaves 0x80000000 | leaf or -1); a chain
     * (node i -> i+1 on one side, a leaf on the other: the segment crosses every plane) */
    int *leaf_parent = malloc(sizeof(int) * (size_t)nl);
    uint32_t *node_plane = malloc(sizeof(uint32_t) * (size_t)n3);
    int *node_parent = malloc(sizeof(int) * (size_t)n3);
    for (int i = 0; i < nl; ++i) leaf_parent[i] = -1;
    for (int i = 0; i < n3; ++i) node_parent[i] = -1;
    for (int i = 0; i < n3; ++i) {
        uint32_t N = nodes3 + 12u * (uint32_t)i, pi = chain ? (uint32_t)i % (uint32_t)np : rnd() % (uint32_t)np;
        if (!bench_like && !clean && rnd() % 97 == 0) pi = rnd() % 0x400000u;           /* a plane word beyond the array */
        node_plane[i] = pi;
        uint32_t ch[2];
        const int ahead = chain ? (clean || rnd() % 61 ? 1 : 0) : (int)(rnd() % 2);   /* chains: the next node on the far side (child 1 when n.D > 0) */
        for (int s = 0; s < 2; ++s) {
            unsigned r = rnd() % 10; int kid = chain ? (s == ahead ? i + 1 : n3) : 2 * i + 1 + s;
            if (kid < n3 && (chain || r < 7)) { ch[s] = (uint32_t)kid; node_parent[kid] = i; }
            else if (r < 9 || chain) {
                uint32_t L = chain ? (uint32_t)i % (uint32_t)nl : rnd() % (uint32_t)nl;
                if (chain && kid >= n3 && s == ahead) L = (uint32_t)(nl - 1);
                ch[s] = 0x80000000u | L; leaf_parent[L] = i;
            }
            else ch[s] = 0xFFFFFFFFu;
        }
        w32(N, pi); w32(N + 4, ch[0]); w32(N + 8, ch[1]);
    }
    /* leaves: the kind bit (byte 0 bit 0), the reference count and first reference */
    for (int i = 0; i < nl; ++i) {
        uint32_t L = leaves + 8u * (uint32_t)i, first = rnd() % (uint32_t)(nr - 6);
        int16_t cnt = (int16_t)(rnd() % 4); if (!bench_like && rnd() % 31 == 0) cnt = (int16_t)-(int16_t)(rnd() % 2);
        w16(L, (uint16_t)rnd()); w16(L + 2, (uint16_t)cnt); w32(L + 4, first);
    }
    /* references: mostly a plane of the leaf's ancestors (the crossed plane 889E0 compares with), either sign bit */
    for (int i = 0; i < nr; ++i) {
        uint32_t Rr = refs + 8u * (uint32_t)i;
        uint32_t pw = (rnd() % (uint32_t)np) | (rnd() & 1 ? 0x80000000u : 0);
        uint32_t root = rnd() % 2 ? rnd() % (uint32_t)n2 : rnd() % 9 ? 0x80000000u | (rnd() % (uint32_t)ns) : 0xFFFFFFFFu;
        w32(Rr, pw); w32(Rr + 4, root);
    }
    for (int L = 0; L < nl; ++L) {
        if (leaf_parent[L] < 0) continue;
        uint32_t first = r32(leaves + 8u * (uint32_t)L + 4);
        int16_t cnt; x_guest_read_pages(&cnt, leaves + 8u * (uint32_t)L + 2, 2);
        for (int r = 0; r < cnt && r < 6; ++r) {
            if (rnd() % 4 == 0) continue;
            int j = leaf_parent[L], up = rnd() % 3 ? 0 : (int)(rnd() % 4);     /* mostly the parent: the plane just crossed */
            while (up-- > 0 && node_parent[j] >= 0) j = node_parent[j];
            w32(refs + 8u * (first + (uint32_t)r), (node_plane[j] & 0x7FFFFFFFu) | (rnd() & 1 ? 0x80000000u : 0));
        }
    }
    /* BSP2D nodes: lines a*u + b*v = d through the region the segment projects to; children later nodes or surfaces */
    for (int i = 0; i < n2; ++i) {
        uint32_t N = nodes2 + 20u * (uint32_t)i;
        float a = frand(-1, 1), b = frand(-1, 1);
        if (rnd() % 4 == 0) { a = rnd() & 1 ? 1.0f : -1.0f; b = 0; }
        const float t = frand(0, 1); const unsigned u = rnd() % 3, v = (u + 1 + rnd() % 2) % 3;
        const float d = a * (P[u] + t * D[u]) + b * (P[v] + t * D[v]) + frand(-2, 2);
        wf(N, odd(a, flavor)); wf(N + 4, odd(b, flavor)); wf(N + 8, odd(flavor == 1 ? (float)(int)d : d, flavor));
        for (int s = 0; s < 2; ++s) {
            int kid = 2 * i + 1 + s;
            uint32_t ch = (kid < n2 && rnd() % 3) ? (uint32_t)kid : rnd() % 13 ? 0x80000000u | (rnd() % (uint32_t)ns) : 0xFFFFFFFFu;
            w32(N + 12 + 4 * (uint32_t)s, ch);
        }
    }
    /* surfaces: closed edge rings of 3..6 edges; the vertices around the segment's projection at some t (so the point
     * lands inside for one orientation and outside for the other), or beside it */
    int eused = 0;
    for (int s = 0; s < ns; ++s) {
        uint32_t S = surfs + 12u * (uint32_t)s;
        const int k = 3 + (int)(rnd() % 4);
        const uint32_t e0 = (uint32_t)eused, vfirst = (uint32_t)(s * 6);
        const float t = frand(-0.1f, 1.1f), rad = frand(0.3f, 6.0f);
        float C[3]; for (unsigned q = 0; q < 3; ++q) C[q] = P[q] + t * D[q] + (rnd() % 3 ? 0 : frand(-3, 3));
        const int ccw = rnd() % 2;
        for (int j = 0; j < k; ++j) {                      /* vertices on a circle in every pair of axes (any projection) */
            float a = 6.2831853f * ((float)j + frand(0.1f, 0.9f)) / (float)k; if (!ccw) a = -a;
            float V[3] = { C[0] + rad * cosf(a), C[1] + rad * sinf(a), C[2] + rad * (cosf(a) - sinf(a)) * 0.7f };
            if (flavor == 1) for (unsigned q = 0; q < 3; ++q) V[q] = (float)(int)V[q];
            for (unsigned q = 0; q < 3; ++q) wf(verts + 16u * (vfirst + (uint32_t)j) + 4 * q, odd(V[q], flavor));
            w32(verts + 16u * (vfirst + (uint32_t)j) + 12, rnd());
        }
        for (int j = 0; j < k; ++j) {                      /* edge j: vertex j -> j+1, this ring on side 1 or 0 */
            const uint32_t e = e0 + (uint32_t)j, en = e0 + (uint32_t)((j + 1) % k), Ed = edges + 24u * e;
            const uint32_t va = vfirst + (uint32_t)j, vb = vfirst + (uint32_t)((j + 1) % k);
            if (rnd() & 1) { w32(Ed, vb); w32(Ed + 4, va); w32(Ed + 0x14, (uint32_t)s); w32(Ed + 0xC, en); w32(Ed + 0x10, rnd() % (uint32_t)ns); w32(Ed + 8, rnd() % (uint32_t)ne); }
            else { w32(Ed, va); w32(Ed + 4, vb); w32(Ed + 0x10, (uint32_t)s); w32(Ed + 8, en); w32(Ed + 0x14, (uint32_t)((s + 1) % ns)); w32(Ed + 0xC, rnd() % (uint32_t)ne); }
            if (rnd() % 29 == 0) w32(Ed + 0x14, (uint32_t)s ^ 1u);   /* a mis-sided edge: the ring fix-up below closes it */
        }
        eused += k;
        w32(S, rnd()); w32(S + 4, e0 + (uint32_t)(rnd() % (uint32_t)k));
        w8(S + 8, (uint8_t)(rnd() % 3 ? rnd() & 0x0Au : rnd())); w8(S + 9, (uint8_t)(rnd() % 3 ? rnd() % 40 : rnd())); w16(S + 10, (uint16_t)rnd());
    }
    /* walk every ring the way 86E20 does and close it at the first repeat (a ring that never returns loops the guest) */
    for (int iter = 0; iter < 8; ++iter) {
        int changed = 0;
        for (int s = 0; s < ns; ++s) {
            uint32_t S = surfs + 12u * (uint32_t)s, e = r32(S + 4), first = e, seen[64]; int n = 0;
            for (;;) {
                uint32_t Ed = edges + 24u * e, r = r32(Ed + 0x14);
                int side = r == (uint32_t)s; uint32_t nx = r32(Ed + 8 + 4 * (uint32_t)side);
                if (nx == first) break;
                seen[n++] = e;
                int stop = nx >= (uint32_t)ne || n >= 60;
                for (int i = 0; i < n && !stop; ++i) if (seen[i] == nx) stop = 1;
                if (stop) { w32(Ed + 8 + 4 * (uint32_t)side, first); changed = 1; break; }
                e = nx;
            }
        }
        if (!changed) break;
    }
    free(pl); free(leaf_parent); free(node_plane); free(node_parent);
    /* the stack: E at a random 4-aligned place; the caller's frame above (the point and delta vectors, the record) */
    unsigned layout = bench_like ? 0 : rnd() % 40;               /* 1..13: the special layouts (a third of the cases) */
    if (deep && rnd() % 3 == 0) layout = 12;                      /* the record's t across a page end, with a translated subtree */
    if (stack_only) layout = 0;                                   /* the caller's frame (a record over S sends writes to the shared trash page) */
    E_top = STACK + 0x11000u + 4u * (rnd() % 0x800);
    for (uint32_t a = E_top - 0x400u; a < E_top + 0x400u; a += 4) w32(a, rnd());          /* stale frames */
    uint32_t rec = E_top + 0x40u + 4u * (rnd() % 0x100);                                     /* 1721B0's [esp+44h] area */
    if (layout == 1) rec = rec_alloc(0x418);
    if (layout == 2) rec = E_top - 0x28u + 4u * (rnd() % 12);                                /* over S and the arguments */
    if (layout == 3) rec = E_top - 0x10000u - 0x418u - 4u * (rnd() % 0x40);                  /* just below the checked region */
    if (layout == 4) rec = E_top - 0x3000u - 4u * (rnd() % 0x100);                           /* in the frames: declined */
    if (layout == 5) rec = E_top - 0x28u - 0x418u + 4u * (1 + rnd() % 0x10);                 /* ends inside the frame region: declined */
    uint32_t pa = E_top + 0x900u + 4u * (rnd() % 0x40), da = E_top + 0xA00u + 4u * (rnd() % 0x40);   /* the caller's frame, apart from the record */
    if (!bench_like && rnd() % 7 == 0) { pa = E_top + 0x100u + 4u * (rnd() % 0x100); da = E_top + 0x100u + 4u * (rnd() % 0x100); }   /* over it: declined */
    if (layout == 6) { pa = rec_alloc(12); da = rec_alloc(12); }
    if (layout == 7) { if (rnd() % 2) pa = rec + 4u * (rnd() % 8); else da = rec + 4u * (rnd() % 8); }   /* the record over the vectors */
    if (layout == 8) { if (rnd() % 2) pa = rec + 0x18u + 4u * (rnd() % 0x100); else da = rec + 0x18u + 4u * (rnd() % 0x100); }   /* the leaf list over them */
    if (layout == 9) { pa = E_top - 0x28u + 4u * (rnd() % 10); }                              /* the point in S */
    if (layout == 11) {                                        /* the vectors across a page end (split float loads) */
        pa = ((tag_alloc(0x1000, 0) | 0xFFFu) + 1u) - 1u - rnd() % 11u; da = ((tag_alloc(0x1000, 0) | 0xFFFu) + 1u) - 1u - rnd() % 11u;
    }
    if (layout == 12) rec = ((tag_alloc(0x2000, 0) | 0xFFFu) + 1u) - 1u - rnd() % 3u;       /* the record's t across a page end */
    if (layout == 13) rec = ((tag_alloc(0x2000, 0) | 0xFFFu) + 1u) - 0x14u - 1u - rnd() % 3u;   /* its leaf count across one */
    for (uint32_t a = rec; a < rec + 0x418u; a += 4) w32(a, rnd());                           /* a stale record */
    for (unsigned k = 0; k < 3; ++k) { wf(pa + 4 * k, P[k]); wf(da + 4 * k, D[k]); }
    /* the surface mask (86E20's a1): one bit per surface index byte */
    uint32_t mask = tag_alloc(64, 0);
    for (unsigned i = 0; i < 16; ++i) w32(mask + 4 * i, bench_like ? 0xFFFFFFFFu : rnd() % 3 ? rnd() | rnd() : rnd());
    float lim = bench_like || rnd() % 3 ? 3.4028235e38f : rnd() % 3 ? frand(0.1f, 1.5f) : rnd() % 2 ? -frand(0, 1) : rnd() % 2 ? 0.0f : nanf_();
    w32(E_top, 0x172295u); w32(E_top + 4, bench_like || rnd() % 3 ? 0x100u : rnd() % 64 | (rnd() & 0xFFFF0000u));
    w32(E_top + 8, mask); w32(E_top + 12, pa); w32(E_top + 16, da); wf(E_top + 20, lim);
    w32(E_top + 0x2000, bsp); w32(E_top + 0x2004, rec);     /* for init_ctx */
    if (layout == 10) E_top += 1 + rnd() % 3;               /* esp unaligned: declined */
    st->chains += chain; st->deep += deep; st->aliased += (layout >= 2 && layout <= 9 && layout != 4 && layout != 5) || (layout >= 11 && layout <= 13);
}

static void init_ctx(xctx *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    const uint32_t E = E_top & ~3u;
    c->r[4] = E_top; c->r[2] = r32(E + 0x2000); c->r[1] = r32(E + 0x2004);
    uint32_t fl = rnd();
    if (bench_like || rnd() % 4 == 0) fl = 0xC0E1u;            /* the looping-sound obstruction rays */
    else if (rnd() % 3) fl = (fl & ~0x1Fu) | (rnd() % 2 ? 0x03u : 0x01u) | (rnd() & 0x1Cu);
    c->r[0] = fl;
    c->fsp = rnd() & 7; c->fcw = 0x027F; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    for (unsigned r = 0; r < 8; ++r) for (unsigned l = 0; l < 4; ++l) c->xmm[r][l] = frand(-100, 100);
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = rnd() % 3 == 0 ? 8 : 32;
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->df = rnd() % 53 == 0;
    c->preempt = (int32_t)(rnd() % 400) - 20;
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) if (a->r[i] != b->r[i]) { snprintf(why, n, "r[%u] %X vs %X", i, a->r[i], b->r[i]); return 0; }
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_of_override) F(f_cf) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* two NaNs are equal (which NaN payload an operation propagates is the host compiler's operand order) */
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "xmm"); return 0; }
    if (memcmp(a->mm, b->mm, sizeof a->mm)) { snprintf(why, n, "mm"); return 0; }
#undef F
    return 1;
}

/* NR_PROF=<file>: a SIGPROF sampling profile of the timed native passes (one program counter per line, for addr2line) */
#include <sys/time.h>
#include <ucontext.h>
static volatile int prof_on; static uintptr_t *prof_pc; static unsigned prof_n, prof_cap = 1u << 22;
static void on_prof(int sig, siginfo_t *si, void *uc_)
{
    (void)sig; (void)si;
    if (!prof_on || prof_n >= prof_cap) return;
    ucontext_t *uc = uc_;
#if defined(__x86_64__)
    prof_pc[prof_n++] = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__arm__)
    prof_pc[prof_n++] = (uintptr_t)uc->uc_mcontext.arm_pc;
#endif
}
static void prof_start(void)
{
    if (!getenv("NR_PROF")) return;
    prof_pc = malloc(sizeof *prof_pc * prof_cap);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = { { 0, 97 }, { 0, 97 } }; setitimer(ITIMER_PROF, &it, NULL);
}
static void prof_stop(void)
{
    const char *prof = getenv("NR_PROF"); if (!prof) return;
    struct itimerval z; memset(&z, 0, sizeof z); setitimer(ITIMER_PROF, &z, NULL);
    FILE *pf = fopen(prof, "w");
    if (pf) { for (unsigned i = 0; i < prof_n; ++i) fprintf(pf, "%lx\n", (unsigned long)prof_pc[i]); fclose(pf); }
    printf("profile: %u samples to %s\n", prof_n, prof);
}
static int pe_fd[2] = { -1, -1 };
static void pe_open(void)
{
#if defined(__linux__)
    for (int i = 0; i < 2; ++i) {
        struct perf_event_attr a; memset(&a, 0, sizeof a); a.size = sizeof a; a.type = PERF_TYPE_HARDWARE;
        a.config = i ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS; a.exclude_kernel = 1; a.exclude_hv = 1;
        pe_fd[i] = (int)syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
    }
#endif
}
static void pe_read(uint64_t v[2]) { for (int i = 0; i < 2; ++i) { v[i] = 0; if (pe_fd[i] >= 0 && read(pe_fd[i], &v[i], 8) != 8) v[i] = 0; } }
static sigjmp_buf alarm_jmp;
static void on_alarm(int sig) { (void)sig; siglongjmp(alarm_jmp, 1); }
static void guard_stack(void)
{
    static uint8_t alt[1 << 16];
    stack_t ss; ss.ss_sp = alt; ss.ss_size = sizeof alt; ss.ss_flags = 0; sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_alarm; sa.sa_flags = SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
}
/* mode 0: the guest body; 1/2: the hook (declined: the guest, as the hooked f_00088E90 then runs its body).
 * Returns 0 on a timeout (env NR_ALARM seconds, default 6). */
static unsigned alarm_s = 6;
static int ran_native;
static int run(xctx *c, int mode)
{
    xv_native_1721b0_force(mode);
    if (sigsetjmp(alarm_jmp, 1)) { xv_native_1721b0_force(0); return 0; }
    alarm(alarm_s);
    ran_native = 0;
    if (mode) { if (xv_native_1721b0_ray(c)) ran_native = 1; else f_00088E90(c); }
    else f_00088E90(c);
    alarm(0);
    xv_native_1721b0_force(0);
    return 1;
}

/* --replay <file> [reps]: casts captured in the game (XV_NATIVE_1721B0_CAPTURE): the arena and page table once, then
 * per cast the entry state and the guest stack [esp - 64 KB, esp + 64 KB). Exactness: each cast guest then native
 * from the same state (xctx, the stack window, the result record, xv_preempt calls). Speed (reps > 0): a pass of all
 * casts through the guest, a pass through the native, reps times alternating; perf-counter user instructions/cycles. */
typedef struct { uint32_t r[8], fl[9], fsp, fsw, fcw, df; int32_t preempt; double st[8]; float xmm[8][4]; uint64_t mm[8]; } nrrec;
static void rec_to_ctx(const nrrec *q, xctx *c)
{
    memset(c, 0, sizeof *c); memcpy(c->r, q->r, sizeof q->r);
    c->f_kind = q->fl[0]; c->f_op1 = q->fl[1]; c->f_op2 = q->fl[2]; c->f_res = q->fl[3]; c->f_bits = q->fl[4];
    c->f_cf_override = q->fl[5]; c->f_cf = q->fl[6]; c->f_of_override = q->fl[7]; c->f_of = q->fl[8];
    c->fsp = q->fsp; c->fsw = (uint16_t)q->fsw; c->fcw = (uint16_t)q->fcw; c->df = q->df; c->preempt = q->preempt;
    memcpy(c->st, q->st, sizeof q->st); memcpy(c->xmm, q->xmm, sizeof q->xmm); memcpy(c->mm, q->mm, sizeof q->mm);
}
static int replay(const char *path, int reps)
{
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 2; }
    uint32_t hdr[4]; if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x3142374Eu || hdr[2] != (1u << 20)) { fprintf(stderr, "bad capture\n"); return 2; }
    const uint32_t arena = hdr[1], W = hdr[3];
    g_xram = malloc(arena + 4096u); g_xpt = malloc(4u << 20); g_img_base = g_xram;
    if (!g_xram || !g_xpt || fread(g_xpt, 4, 1u << 20, f) != (1u << 20) || fread(g_xram, 1, arena, f) != arena) { fprintf(stderr, "short capture\n"); return 2; }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
    unsigned n = 0, cap = 256; nrrec *recs = malloc(sizeof *recs * cap); uint8_t *wins = malloc((size_t)W * cap);
    for (;;) {
        if (n == cap) { cap *= 2; recs = realloc(recs, sizeof *recs * cap); wins = realloc(wins, (size_t)W * cap); }
        if (fread(&recs[n], sizeof *recs, 1, f) != 1 || fread(wins + (size_t)W * n, 1, W, f) != W) break;
        n++;
    }
    fclose(f);
    /* the result records outside the windows: saved from the arena as captured (restored per cast) */
    uint8_t *recimg = malloc((size_t)0x418 * (n ? n : 1));
    for (unsigned i = 0; i < n; ++i) x_guest_read_pages(recimg + (size_t)0x418 * i, recs[i].r[1], 0x418);
    printf("replay: %u casts, arena %u bytes\n", n, arena);
    uint8_t *gw = malloc(W), *nw = malloc(W), rg[0x418], rn[0x418];
    unsigned bad = 0, nan_words = 0, hits = 0, from1721 = 0, from1731 = 0;
    for (unsigned i = 0; i < n; ++i) {
        const uint32_t lo = recs[i].r[4] - W / 2;
        xctx cg, cn; rec_to_ctx(&recs[i], &cg); rec_to_ctx(&recs[i], &cn);
        x_guest_write_pages(recs[i].r[1], recimg + (size_t)0x418 * i, 0x418); x_guest_write_pages(lo, wins + (size_t)W * i, W); preempt_calls = 0;
        const uint32_t ret = r32(recs[i].r[4]); from1721 += ret == 0x172295u; from1731 += ret == 0x1732C7u;
        f_00088E90(&cg); unsigned gp = preempt_calls; x_guest_read_pages(gw, lo, W); x_guest_read_pages(rg, recs[i].r[1], 0x418);
        x_guest_write_pages(recs[i].r[1], recimg + (size_t)0x418 * i, 0x418); x_guest_write_pages(lo, wins + (size_t)W * i, W); preempt_calls = 0;
        xv_native_1721b0_force(2); if (!xv_native_1721b0_ray(&cn)) f_00088E90(&cn); xv_native_1721b0_force(0);
        x_guest_read_pages(nw, lo, W); x_guest_read_pages(rn, recs[i].r[1], 0x418);
        hits += (cg.r[0] & 0xFF) != 0;
        char why[200] = "";
        int ok = same_ctx(&cn, &cg, why, sizeof why) && preempt_calls == gp;
        if (ok && memcmp(rg, rn, sizeof rg)) { ok = 0; snprintf(why, sizeof why, "result record"); }
        if (ok && memcmp(gw, nw, W))
            for (uint32_t k = 0; k < W; ++k) if (gw[k] != nw[k]) {
                uint32_t a, b; memcpy(&a, gw + (k & ~3u), 4); memcpy(&b, nw + (k & ~3u), 4);
                if (nan32(a) && nan32(b)) { nan_words++; k |= 3u; continue; }
                ok = 0; snprintf(why, sizeof why, "stack %08X guest %02X native %02X", lo + k, gw[k], nw[k]); break;
            }
        if (!ok && ++bad <= 10) printf("cast %u MISMATCH: %s\n", i, why[0] ? why : "xv_preempt calls");
    }
    printf("replay: %u casts compared (%u from 1721B0, %u from 1731D0, %u hit), %u mismatches, %u NaN-payload words\n", n, from1721, from1731, hits, bad, nan_words);
    if (reps > 0) {
        pe_open(); prof_start();
        double ns[2] = { 0, 0 }; uint64_t pe[2][2] = { { 0, 0 }, { 0, 0 } }; struct timespec t0, t1;
        for (int r = 0; r < reps; ++r)
            for (int mode = 0; mode <= 2; mode += 2) {
                for (unsigned i = 0; i < n; ++i) {
                    const uint32_t lo = recs[i].r[4] - W / 2;
                    x_guest_write_pages(recs[i].r[1], recimg + (size_t)0x418 * i, 0x418); x_guest_write_pages(lo, wins + (size_t)W * i, W);
                    xctx c; rec_to_ctx(&recs[i], &c); c.preempt = 1 << 30;
                    xv_native_1721b0_force(mode);
                    uint64_t p0[2], p1[2]; pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                    prof_on = mode == 2 || getenv("NR_PROF_GUEST") != NULL;
                    if (mode) { if (!xv_native_1721b0_ray(&c)) f_00088E90(&c); } else f_00088E90(&c);
                    prof_on = 0;
                    clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                    ns[mode / 2] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    pe[mode / 2][0] += p1[0] - p0[0]; pe[mode / 2][1] += p1[1] - p0[1];
                }
                xv_native_1721b0_force(0);
            }
        prof_stop();
        const double calls = (double)n * reps;
        printf("replay speed: %u casts x %d: guest %.0f ns/call, native %.0f ns/call, ratio %.2fx; user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               n, reps, ns[0] / calls, ns[1] / calls, ns[0] / ns[1], pe[0][0] / calls, pe[1][0] / calls, pe[1][0] ? (double)pe[0][0] / pe[1][0] : 0,
               pe[0][1] / calls, pe[1][1] / calls, pe[1][1] ? (double)pe[0][1] / pe[1][1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
    }
    log_all = 1; xv_native_1721b0_report(0);
    return bad != 0;
}

/* --threads N [iters]: N host threads cast at the same time, each through its own page table whose stack pages are its
 * own physical copy (the same guest addresses, different host memory: the scene helper's render view in the game), in
 * the mode given by NR_MODE (2 native, default; 1 verify), iters casts each per scene; every result (registers, flags,
 * x87, the stack pages with the record) must equal the single-threaded translation's. Catches state shared between
 * threads (a journal, a level stack, a flag) and a translation that is not the calling thread's. */
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
typedef struct { uint32_t *pt; int iters; xctx c0, cref; const uint8_t *init, *ref; unsigned bad, runs; char why[200]; } nr_th;
static void *nr_thread(void *p)
{
    nr_th *a = p;
    xv_host_page_table = a->pt;
    uint8_t *cur = malloc((size_t)STACK_PAGES << 12);
    for (int k = 0; k < a->iters; ++k) {
        x_guest_write_pages(STACK, a->init, (size_t)STACK_PAGES << 12);
        xctx c = a->c0;
        if (!xv_native_1721b0_ray(&c)) f_00088E90(&c);
        x_guest_read_pages(cur, STACK, (size_t)STACK_PAGES << 12);
        char why[200] = "";
        int ok = same_ctx(&c, &a->cref, why, sizeof why);
        if (getenv("NR_TDEBUG") && !ok) {
            for (uint32_t i = 0; i < ((uint32_t)STACK_PAGES << 12); ++i) if (cur[i] != a->ref[i]) { printf("  first stack diff %08X (E%+d) native %02X guest %02X\n", STACK + i, (int)(STACK + i - a->c0.r[4]), cur[i], a->ref[i]); break; }
            printf("  eax %08X/%08X ecx %08X/%08X edx %08X/%08X esp %08X/%08X\n", c.r[0], a->cref.r[0], c.r[1], a->cref.r[1], c.r[2], a->cref.r[2], c.r[4], a->cref.r[4]);
        }
        if (ok && memcmp(cur, a->ref, (size_t)STACK_PAGES << 12)) {
            ok = 1;
            for (uint32_t i = 0; i < ((uint32_t)STACK_PAGES << 12); ++i) if (cur[i] != a->ref[i]) {
                uint32_t x, y; memcpy(&x, cur + (i & ~3u), 4); memcpy(&y, a->ref + (i & ~3u), 4);
                if (nan32(x) && nan32(y)) { i |= 3u; continue; }
                ok = 0; snprintf(why, sizeof why, "stack %08X native %02X guest %02X", STACK + i, cur[i], a->ref[i]); break;
            }
        }
        if (!ok && !a->bad++) snprintf(a->why, sizeof a->why, "%s", why);
        a->runs++;
    }
    free(cur);
    return NULL;
}
static int threads_test(unsigned scenes, int nth, int iters)
{
    const int mode = getenv("NR_MODE") ? atoi(getenv("NR_MODE")) : 2;
    stats st = {0}; stack_only = 1; slice = 1 << 30;
    if (next_free + ((uint32_t)nth * STACK_PAGES << 12) > trash_off) { fprintf(stderr, "arena too small for %d threads\n", nth); return 2; }
    uint32_t **pts = malloc(sizeof *pts * (size_t)nth);
    for (int t = 0; t < nth; ++t) {                                /* own tables: the stack pages on private physical pages */
        pts[t] = malloc(sizeof(uint32_t) << 20); memcpy(pts[t], g_xpt, sizeof(uint32_t) << 20);
        for (uint32_t i = 0; i < STACK_PAGES; ++i) pts[t][(STACK >> 12) + i] = next_free + (((uint32_t)t * STACK_PAGES + (STACK_PAGES - 1 - i)) << 12);
    }
    uint8_t *init = malloc((size_t)STACK_PAGES << 12), *ref = malloc((size_t)STACK_PAGES << 12);
    unsigned bad = 0, runs = 0, skipped = 0, native = 0, badscenes = 0;
    xv_native_1721b0_force(mode); xv_native_1721b0_detail(1);
    for (unsigned k = 0; k < scenes; ++k) {
        xv_host_page_table = g_xpt;
        scene(&st);
        xctx c0; init_ctx(&c0); c0.preempt = 1 << 30;
        if ((c0.r[4] & 3u) || c0.r[1] < STACK || c0.r[1] + 0x418u > STACK + ((uint32_t)STACK_PAGES << 12)) { skipped++; continue; }
        x_guest_read_pages(init, STACK, (size_t)STACK_PAGES << 12);
        xctx cref = c0;                                            /* the reference: the translation, single-threaded */
        xv_native_1721b0_force(0);
        if (!run(&cref, 0)) { skipped++; xv_native_1721b0_force(mode); continue; }
        xv_native_1721b0_force(mode);
        x_guest_read_pages(ref, STACK, (size_t)STACK_PAGES << 12);
        { xctx t = c0; x_guest_write_pages(STACK, init, (size_t)STACK_PAGES << 12); if (xv_native_1721b0_ray(&t)) native++; }   /* declined layouts run the guest */
        nr_th *a = calloc((size_t)nth, sizeof *a); pthread_t *th = malloc(sizeof *th * (size_t)nth);
        for (int t = 0; t < nth; ++t) { a[t].pt = pts[t]; a[t].iters = iters; a[t].c0 = c0; a[t].cref = cref; a[t].init = init; a[t].ref = ref; pthread_create(&th[t], NULL, nr_thread, &a[t]); }
        for (int t = 0; t < nth; ++t) {
            pthread_join(th[t], NULL); runs += a[t].runs;
            if (a[t].bad) { if (++badscenes <= 10) printf("scene %u thread %d: %u of %u casts differ (%s)\n", k, t, a[t].bad, a[t].runs, a[t].why); bad += a[t].bad; }
        }
        free(a); free(th);
    }
    xv_native_1721b0_force(0);
    log_all = 1; xv_native_1721b0_report(0); log_all = 0;
    printf("native-1721b0 threads: %u scenes (%u run natively, %u skipped), %d threads x %d casts each, mode %d: %u casts, %u mismatches\n",
           scenes, native, skipped, nth, iters, mode, runs, bad);
    return bad != 0;
}
#endif

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[3], "--replay")) return replay(argv[4], argc > 5 ? atoi(argv[5]) : 0);
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    if (argc > 4 && !strcmp(argv[3], "--threads")) {
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
        signal(SIGALRM, on_alarm); guard_stack(); map_memory();   /* a scene whose guest recursion never ends is skipped */
        return threads_test(cases, atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 50);
#else
        fprintf(stderr, "--threads needs the per-thread page table build (-DXV_THREAD_PAGE_TABLE=1)\n"); return 2;
#endif
    }
    const int verify = argc > 3 && !strcmp(argv[3], "--verify");
    const int bench = argc > 4 && !strcmp(argv[3], "--bench") ? atoi(argv[4]) : 0;
    stats s = {0};
    signal(SIGALRM, on_alarm);
    if (getenv("NR_ALARM")) alarm_s = (unsigned)atoi(getenv("NR_ALARM"));
    guard_stack();
    map_memory();
    if (bench) {
        bench_like = 1;
        double g_ns = 0, n_ns = 0; unsigned calls = 0; struct timespec t0, t1;
        uint64_t g_pe[2] = { 0, 0 }, n_pe[2] = { 0, 0 }; pe_open(); prof_start();
        uint8_t *snap = malloc(0x4000);
        for (unsigned k = 0; k < cases; ++k) {
            scene(&s);
            slice = 1 << 30;
            xctx c0; init_ctx(&c0);
            c0.preempt = 1 << 30; c0.df = 0;
            const uint32_t lo = c0.r[4] - 0x2000u;
            x_guest_read_pages(snap, lo, 0x4000);
            { xctx t = c0; if (!run(&t, 0)) { s.timeouts++; continue; } x_guest_write_pages(lo, snap, 0x4000); }
            xv_native_1721b0_detail(0);
            for (int r = 0; r < bench; ++r)
                for (int mode = 0; mode <= 2; mode += 2) {
                    x_guest_write_pages(lo, snap, 0x4000);
                    xctx c = c0; xv_native_1721b0_force(mode); xv_native_1721b0_detail(0);
                    uint64_t p0[2], p1[2]; pe_read(p0);
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                    prof_on = mode == 2 || getenv("NR_PROF_GUEST") != NULL;
                    if (mode) { if (!xv_native_1721b0_ray(&c)) f_00088E90(&c); } else f_00088E90(&c);
                    prof_on = 0;
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    pe_read(p1);
                    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    if (mode) { n_ns += ns; n_pe[0] += p1[0] - p0[0]; n_pe[1] += p1[1] - p0[1]; }
                    else { g_ns += ns; g_pe[0] += p1[0] - p0[0]; g_pe[1] += p1[1] - p0[1]; }
                }
            calls += (unsigned)bench;
        }
        xv_native_1721b0_force(0); prof_stop();
        if (s.timeouts) printf("native-1721b0 bench: %u scenes skipped (the guest never finished)\n", s.timeouts);
        double z = 0;
        for (int i = 0; i < 20000; ++i) { clock_gettime(CLOCK_MONOTONIC, &t0); clock_gettime(CLOCK_MONOTONIC, &t1); z += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec); }
        z /= 20000;
        printf("native-1721b0 bench: %u calls each: guest %.1f ns/call, native %.1f ns/call, ratio %.2fx (timer %.1f ns subtracted)\n",
               calls, g_ns / calls - z, n_ns / calls - z, (g_ns / calls - z) / (n_ns / calls - z), z);
        printf("native-1721b0 bench: user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               (double)g_pe[0] / calls, (double)n_pe[0] / calls, n_pe[0] ? (double)g_pe[0] / n_pe[0] : 0, (double)g_pe[1] / calls, (double)n_pe[1] / calls,
               n_pe[1] ? (double)g_pe[1] / n_pe[1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
        log_all = 1; xv_native_1721b0_report(0);
        return 0;
    }
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    for (unsigned k = 0; k < cases; ++k) {
        scene(&s);
        slice = 5 + (int)(rnd() % 90);
        xctx c0; init_ctx(&c0);
        if (getenv("NR_FROM") && k < (unsigned)atoi(getenv("NR_FROM"))) continue;
        if (getenv("NR_TRACE")) { fprintf(stderr, "case %u\n", k); fflush(stderr); }
        memcpy(before, g_xram, ARENA);
        xctx cg = c0; preempt_calls = 0;
        int gok = run(&cg, 0);
        unsigned guest_preempts = preempt_calls;
        memcpy(guest, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = 0; log_mismatch = 0;
        int nok = run(&cn, 2);
        unsigned native_preempts = preempt_calls;
        char why[200] = "";
        s.cases++;
        if (!gok) { s.timeouts++; continue; }
        if (ran_native) s.native_ran++; else s.declined++;
        if (getenv("NR_CASES")) { printf("case %u: chains so far %u, eax %08X lim %08X D %08X %08X %08X: ", k, s.chains, c0.r[0], r32((E_top & ~3u) + 20), r32(r32((E_top & ~3u) + 16)), r32(r32((E_top & ~3u) + 16) + 4), r32(r32((E_top & ~3u) + 16) + 8)); log_all = 1; xv_native_1721b0_report(0); log_all = 0; }
        {   /* the leaf list reached its capacity: the overflow slot was written */
            uint32_t rec = c0.r[1], cnt = 0;
            const uint32_t pa = g_xpt[(rec + 0x14u) >> 12] + ((rec + 0x14u) & 0xFFFu);
            if (pa + 4u <= ARENA) memcpy(&cnt, guest + pa, 4);
            if (cnt == 0x100u) s.overflow++;
            if ((cg.r[0] & 0xFF) != 0) s.hits++;
        }
        int ok = gok && nok && same_ctx(&cn, &cg, why, sizeof why) && native_preempts == guest_preempts;
        if (ok && memcmp(guest, g_xram, ARENA)) {
            for (uint32_t i = 0; i < ARENA; ++i) if (guest[i] != g_xram[i]) {
                uint32_t gw, nw; memcpy(&gw, guest + (i & ~3u), 4); memcpy(&nw, g_xram + (i & ~3u), 4);
                if (nan32(gw) && nan32(nw)) { s.nan_words++; i |= 3u; continue; }
                ok = 0; snprintf(why, sizeof why, "arena+%06X guest %02X native %02X", i, guest[i], g_xram[i]); break;
            }
        }
        if (!ok && !why[0]) snprintf(why, sizeof why, gok != nok ? "timeout guest %d native %d" : "xv_preempt calls native %u guest %u", gok ? native_preempts : !gok, gok ? guest_preempts : !nok);
        if (!ok) {
            if (++s.mismatches <= 10) {
                printf("case %u MISMATCH: %s\n", k, why);
                if (getenv("NR_DEBUG")) {
                    unsigned shown = 0;
                    for (uint32_t i = 0; i < ARENA && shown < 24; ++i) if (guest[i] != g_xram[i]) {
                        uint32_t va = 0xFFFFFFFFu;
                        for (uint32_t p = 0; p < (1u << 20); ++p) if (g_xpt[p] == (i & ~0xFFFu)) { va = (p << 12) | (i & 0xFFFu); break; }
                        printf("   va %08X (E%+d) before %02X guest %02X native %02X\n", va, (int)(va - E_top), before[i], guest[i], g_xram[i]); shown++;
                    }
                    printf("   E %08X eax %08X/%08X ecx %08X/%08X edx %08X/%08X fsw %04X/%04X preempts %u/%u\n", E_top, cg.r[0], cn.r[0], cg.r[1], cn.r[1], cg.r[2], cn.r[2], cg.fsw, cn.fsw, guest_preempts, native_preempts);
                }
            }
        }
        if (verify && gok) {
            memcpy(g_xram, before, ARENA);
            xctx cv = c0; preempt_calls = 0; log_mismatch = 0;
            int vok = run(&cv, 1);
            char vw[200] = "";
            int good = vok && !log_mismatch && same_ctx(&cv, &cg, vw, sizeof vw) && preempt_calls == guest_preempts && !memcmp(guest, g_xram, ARENA);
            if (!vok) s.verify_timeouts++;
            else if (!good) {
                const char *what = log_mismatch ? "log" : vw[0] ? vw : preempt_calls != guest_preempts ? "xv_preempt calls" : "arena";
                if (++s.verify_mismatch <= 10) printf("case %u VERIFY: log mismatches %d, %s\n", k, log_mismatch, what);
            }
        }
    }
    log_all = 1; xv_native_1721b0_report(0); log_all = 0;     /* the native's own counters over all native/verify runs */
    printf("native-1721b0 differential: %u cases (%u native, %u declined, %u hit, %u chains, %u deeper than 1024, %u aliased layouts, "
           "%u leaf-list overflows, %u guest timeouts skipped, %u NaN-payload words), %u mismatches%s",
           s.cases, s.native_ran, s.declined, s.hits, s.chains, s.deep, s.aliased, s.overflow, s.timeouts, s.nan_words, s.mismatches, verify ? "" : "\n");
    if (verify) printf(", verify-mode failures %u (%u verify runs past the alarm skipped)\n", s.verify_mismatch, s.verify_timeouts);
    return s.mismatches || s.verify_mismatch;
}
