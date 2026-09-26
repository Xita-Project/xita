/* Differential test: native f_00088110 BSP sphere query (recomp/kernel/xk_native_4b9d0.c) against the lifted guest
 * bodies of f_00088110 + f_00087EA0 + f_00087E10 + f_00086F50 + f_000B0CB0 (extracted from a stage's shards by
 * tools/test_native_4b9d0.py), on randomized queries in a synthetic guest arena: shuffled physical pages (records that
 * straddle page ends, split float loads), reversed stack pages, random BSP3D trees (chains that nest the ancestor
 * stack deeper than 88110's frame, both-side descents), leaves with BSP2D references whose plane words match the
 * ancestor stack on either side, BSP2D trees, surfaces with closed edge rings shared between surfaces, vertices around
 * the sphere (inside / on / outside, NaN and infinite coordinates), random "tested" bits and capacity words, more than
 * 0x100 leaves / surfaces / edges / vertices in reach (the list capacity), a random zero constant and axes table, the
 * result lists at the in-game place just above the arguments or elsewhere, and layouts the native declines (esp or
 * the lists unaligned, the lists inside the stack, the BSP header on the stack).
 * Each case runs the guest body (hook off) and the native from identical state and compares the whole arena, every
 * xctx field and the xv_preempt call count; a declined case must leave the state untouched; with --verify it also
 * runs the verify mode (mode 1) and requires the guest result and no MISMATCH line. --bench N: game-like queries,
 * guest vs native ns/call and (Linux perf counters) user instructions/cycles per call. */
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
    if (log_all || getenv("N4_DEBUG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
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
extern void f_00088110(xctx *);
static unsigned world_reference_calls;
#ifdef N4_FUSED_GUEST
/* --fused: the guest is the game's own fused query (a harness build's query_fusion.o and the collision control
 * objects: the older partial natives on, as XV_NATIVE_*_DEFAULT=1 builds them); the rest of its environment stubbed
 * as the owner sees it with the world-run admission declined */
#include <pthread.h>
static uint32_t n4_arena_size;
uint32_t xk_mem_arena_size(void) { return n4_arena_size; }
unsigned xv_object_hold_children_enabled;
void xv_object_math_report_check(void) {}
unsigned xv_object_motion_begin(xctx *c, unsigned k) { (void)c; (void)k; return 0; }
void xv_object_motion_end(unsigned *p) { (void)p; }
unsigned xv_object_world_run_admit(xctx *c) { (void)c; return 0; }
void xv_query_reuse_run(xctx *c) { (void)c; abort(); }
int xv_trace_funcs, xv_watch_n;
pthread_t xv_owner_pthread_self(void) { return pthread_self(); }
extern void query_fused_172c95_171f94(xctx *);
#define N4_GUEST(c) query_fused_172c95_171f94(c)
#else
/* the hook's guest: in the game the fused copy of the same subtree (recomp/query_fusion.c) */
void query_fused_172c95_171f94(xctx *c) { world_reference_calls++; f_00088110(c); }
#define N4_GUEST(c) f_00088110(c)
#endif
static void object_reference(xctx *c) { f_00088110(c); }
void xv_native_object_query_force(int);
void xv_native_4b9d0_object_query(xctx *, void (*)(xctx *));
extern void xv_native_4b9d0_force(int);
extern void xv_native_4b9d0_query(xctx *);
extern void xv_native_4b9d0_report(unsigned);

enum { ARENA = 8u << 20, IMG_LO = 0x1E0, IMG_PAGES = 0x20, TAG = 0x40000000u, TAG_PAGES = 1400, STACK = 0xD0000000u, STACK_PAGES = 24 };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
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

typedef struct { unsigned cases, native_ran, declined, dirty_like, timeouts, mismatches, verify_mismatch, verify_timeouts, nan_words, hits, deep, full[4]; } stats;
static int nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }

static uint32_t E_top;            /* esp at the entry of f_00088110 */
static int bench_like;            /* game-like queries (a10: ~28 BSP3D nodes, ~12 surfaces, ~48 ring steps) */
static float fcoord(float c, float spread, int flavor)
{
    if (flavor == 2 && rnd() % 41 == 0) return rnd() % 2 ? nanf_() : (rnd() % 2 ? INFINITY : -INFINITY);
    return c + frand(-spread, spread);
}
typedef struct { float n[3], d; } plane;
static void random_normal(float *n, int flavor)
{
    if (flavor == 1 || rnd() % 5 == 0) { n[0] = n[1] = n[2] = 0; n[rnd() % 3] = rnd() & 1 ? 1.0f : -1.0f; return; }
    if (rnd() % 7 == 0) { float a = 0.70710677f; for (unsigned i = 0; i < 3; ++i) n[i] = rnd() & 1 ? a : -a; n[rnd() % 3] = 0; return; }
    float l;
    do { for (unsigned i = 0; i < 3; ++i) n[i] = frand(-1, 1); l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); } while (l < 0.05f);
    for (unsigned i = 0; i < 3; ++i) n[i] /= l;
}

static void scene(stats *st)
{
    memset(g_xram, 0, ARENA);
    tag_top = TAG + (rnd() % 64) * 4;
    const int flavor = bench_like ? 0 : rnd() % 4 == 0 ? 1 : rnd() % 8 == 0 ? 2 : 0;
    const int big = !bench_like && rnd() % 10 == 0, deep = !bench_like && rnd() % 25 == 0, wild = deep && rnd() % 2;
    /* image constants */
    for (unsigned a = 0; a < 6; ++a) {
        uint16_t u = (uint16_t)((a / 2 + 1) % 3), v = (uint16_t)((a / 2 + 2) % 3);
        if (a & 1) { uint16_t t = u; u = v; v = t; }
        if (!bench_like && rnd() % 37 == 0) u = (uint16_t)(int16_t)((int)(rnd() % 9) - 3);
        if (!bench_like && rnd() % 37 == 0) v = (uint16_t)(int16_t)((int)(rnd() % 9) - 3);
        w16(0x1EAF30 + 4 * a, u); w16(0x1EAF32 + 4 * a, v);
    }
    float zero = bench_like || rnd() % 4 ? 0.0f : rnd() % 2 ? -0.0f : rnd() % 3 ? frand(-0.01f, 0.01f) : nanf_();
    wf(0x1F0A68, zero);
    /* the sphere */
    float ctr[3] = { frand(-4, 4), frand(-4, 4), frand(-4, 4) };
    float radius = bench_like ? frand(0.3f, 0.8f) : rnd() % 3 ? frand(0.2f, 2.5f) : rnd() % 2 ? frand(2.5f, 9) : rnd() % 3 ? 0.0f : rnd() % 2 ? frand(-1, 0) : nanf_();
    if (big) radius = rnd() % 3 ? frand(20, 60) : frand(200, 1000);   /* huge: every plane straddled, the lists fill up */
    if (wild) radius = frand(6, 12);
    /* BSP header */
    uint32_t bsp = tag_alloc(0x60, 0);
    int np = bench_like ? 24 : 4 + (int)(rnd() % 40), n3 = bench_like ? 60 : 3 + (int)(rnd() % 60), nl = bench_like ? 30 : 2 + (int)(rnd() % 40);
    int ns = bench_like ? 60 : 4 + (int)(rnd() % 50), n2 = bench_like ? 80 : 2 + (int)(rnd() % 60);
    if (big) { n3 = 200 + (int)(rnd() % 200); nl = 300 + (int)(rnd() % 100); ns = 300 + (int)(rnd() % 200); n2 = 300; }
    if (deep) { n3 = 150 + (int)(rnd() % 40); nl = 200; }
    int nv = ns * 3 + 8, ne = ns * 6 + 8, nr = nl * 3 + 4;
    uint32_t planes = tag_alloc((uint32_t)np * 16, rnd() % 13 == 0), nodes3 = tag_alloc((uint32_t)n3 * 12, rnd() % 13 == 0);
    uint32_t leaves = tag_alloc((uint32_t)nl * 8, 0), refs = tag_alloc((uint32_t)nr * 8, 0), nodes2 = tag_alloc((uint32_t)n2 * 20, rnd() % 13 == 0);
    uint32_t surfs = tag_alloc((uint32_t)ns * 12, 0), edges = tag_alloc((uint32_t)ne * 24, 0), verts = tag_alloc((uint32_t)nv * 16, rnd() % 17 == 0);
    w32(bsp + 4, nodes3); w32(bsp + 0x10, planes); w32(bsp + 0x1C, leaves); w32(bsp + 0x28, refs);
    w32(bsp + 0x34, nodes2); w32(bsp + 0x40, surfs); w32(bsp + 0x4C, edges); w32(bsp + 0x58, verts);
    plane *pl = malloc(sizeof(plane) * (size_t)np);
    for (int i = 0; i < np; ++i) {
        random_normal(pl[i].n, flavor);
        float through = pl[i].n[0] * ctr[0] + pl[i].n[1] * ctr[1] + pl[i].n[2] * ctr[2];
        pl[i].d = rnd() % 3 ? through + frand(-radius * 1.5f - 0.5f, radius * 1.5f + 0.5f) : frand(-10, 10);   /* mostly straddling */
        if (radius > 100) pl[i].d = through + frand(-5, 5);                /* huge: every plane straddled */
        if (flavor == 1) pl[i].d = (float)(int)pl[i].d;
        for (unsigned k = 0; k < 3; ++k) wf(planes + 16u * (uint32_t)i + 4 * k, pl[i].n[k]);
        wf(planes + 16u * (uint32_t)i + 12, flavor == 2 && rnd() % 23 == 0 ? nanf_() : pl[i].d);
    }
    /* BSP3D nodes: a tree (node i's children are 2i+1 and 2i+2 when they exist, or leaves 0x80000000 | leaf, or -1);
     * `deep` chains node i to i+1 on the front side, a leaf on the back side, planes through the center (both sides
     * taken at every level: the ancestor stack runs over 88110's frame); `wild` gives the chain below level 128 plane
     * words 0x3FFFFF00+k (their plane reads land on unmapped pages: distance 0, both sides), so the ancestor entries
     * that run over 88110's frame into the result lists make the surface count negative (0xBFFFFF00+k) and the
     * surface-list store lands in the frames of the chain above: a wild write the native's `dirty` path must follow */
    uint32_t *plane_of = malloc(sizeof(uint32_t) * (size_t)n3);
    int *leaf_at = malloc(sizeof(int) * (size_t)nl);   /* the node a leaf hangs from (child side in bit 30), -1 */
    for (int i = 0; i < nl; ++i) leaf_at[i] = -1;
    for (int i = 0; i < n3; ++i) {
        uint32_t N = nodes3 + 12u * (uint32_t)i, pi = rnd() % (uint32_t)np;
        if (wild && i >= 128) pi = 0x3FFFFF00u | (rnd() % 0x40u);
        plane_of[i] = pi;
        uint32_t ch[2];
        for (int s = 0; s < 2; ++s) {
            unsigned r = rnd() % 10; int kid = deep ? (s == 0 ? i + 1 : n3) : 2 * i + 1 + s;
            if (kid < n3 && (deep || r < 7 || (big && radius > 100))) ch[s] = (uint32_t)kid;   /* huge: a full tree */
            else if (r < 9 || deep) {
                uint32_t L = deep ? (uint32_t)i % (uint32_t)nl : rnd() % (uint32_t)nl;
                ch[s] = 0x80000000u | L; leaf_at[L] = i | (s << 30);
            }
            else ch[s] = 0xFFFFFFFFu;
        }
        if (deep && pi < (uint32_t)np) { pl[pi].d = pl[pi].n[0] * ctr[0] + pl[pi].n[1] * ctr[1] + pl[pi].n[2] * ctr[2]; wf(planes + 16u * pi + 12, pl[pi].d); }
        w32(N, pi); w32(N + 4, ch[0]); w32(N + 8, ch[1]);
    }
    /* leaves -> BSP2D references (plane word: a plane of the tree, either side bit; root: a node or a surface) */
    for (int i = 0; i < nl; ++i) {
        uint32_t L = leaves + 8u * (uint32_t)i, first = rnd() % (uint32_t)(nr - 3);
        int16_t cnt = (int16_t)(rnd() % 4); if (!bench_like && rnd() % 31 == 0) cnt = (int16_t)-(int16_t)(rnd() % 2);
        w16(L, (uint16_t)rnd()); w16(L + 2, (uint16_t)cnt); w32(L + 4, first);
    }
    for (int i = 0; i < nr; ++i) {
        uint32_t Rr = refs + 8u * (uint32_t)i;
        uint32_t pw = (rnd() % (uint32_t)np) | (rnd() & 1 ? 0x80000000u : 0);
        uint32_t root = rnd() % 4 ? rnd() % (uint32_t)n2 : 0x80000000u | (rnd() % (uint32_t)ns);
        w32(Rr, pw); w32(Rr + 4, root);
    }
    /* big / deep: most references name a plane that is on their leaf's ancestor stack (with its side bit), so the
     * BSP2D walks and surface tests run (the lists then reach their 0x100 capacity in the big scenes) */
    if (big || deep)
        for (int L = 0; L < nl; ++L) {
            if (leaf_at[L] < 0) continue;
            int node = leaf_at[L] & 0x3FFFFFFF, side = leaf_at[L] >> 30;
            uint32_t first, cnt16; x_guest_read_pages(&first, leaves + 8u * (uint32_t)L + 4, 4);
            int16_t cnt = (int16_t)(2 + rnd() % 2); w16(leaves + 8u * (uint32_t)L + 2, (uint16_t)cnt); (void)cnt16;
            for (int r = 0; r < cnt; ++r) {
                /* an ancestor: node j on the path to this leaf; its entry carries 0x80000000 when the path took its front child */
                int j = node, sj = side, up = (int)(rnd() % 6);
                while (up-- > 0 && j > 0) { int parent = deep ? j - 1 : (j - 1) / 2; sj = deep ? 0 : (j - 1) % 2; j = parent; }
                uint32_t word = (plane_of[j] & 0x7FFFFFFFu) | (sj == 0 ? 0x80000000u : 0);
                w32(refs + 8u * (first + (uint32_t)r), word);
                w32(refs + 8u * (first + (uint32_t)r) + 4, (big || wild) && rnd() % 3 ? 0x80000000u | (rnd() % (uint32_t)ns) : rnd() % (uint32_t)n2);
            }
        }
    free(plane_of); free(leaf_at);
    /* BSP2D nodes: lines a*u + b*v = d, children later nodes or surfaces */
    for (int i = 0; i < n2; ++i) {
        uint32_t N = nodes2 + 20u * (uint32_t)i;
        float a = frand(-1, 1), b = frand(-1, 1);
        if (rnd() % 4 == 0) { a = rnd() & 1 ? 1.0f : -1.0f; b = 0; }
        wf(N, a); wf(N + 4, b); wf(N + 8, frand(-3, 3));
        for (int s = 0; s < 2; ++s) {                        /* a tree: children 2i+1, 2i+2 or surfaces */
            int kid = 2 * i + 1 + s;
            uint32_t ch = (kid < n2 && rnd() % 3) ? (uint32_t)kid : 0x80000000u | (rnd() % (uint32_t)ns);
            w32(N + 12 + 4 * (uint32_t)s, ch);
        }
    }
    /* vertices around the sphere */
    for (int i = 0; i < nv; ++i) {
        uint32_t V = verts + 16u * (uint32_t)i;
        float sp = rnd() % 3 ? radius * 1.3f + 0.2f : 6.0f;
        if (!(sp == sp) || sp < 0.2f) sp = 1.0f;
        for (unsigned k = 0; k < 3; ++k) wf(V + 4 * k, fcoord(ctr[k], sp, flavor));
        w32(V + 12, rnd());
    }
    /* surfaces: closed edge rings (3..6 edges); an edge's right-hand surface (+0x14) is the ring's when it walks the
     * ring on side 1; some edges are shared by two rings (one side each) */
    int eused = 0;
    int *side0_free = malloc(sizeof(int) * (size_t)ne); int nfree = 0;
    for (int s = 0; s < ns; ++s) {
        uint32_t S = surfs + 12u * (uint32_t)s;
        int k = 3 + (int)(rnd() % 4); if (big) k = 3;
        int ring[8];
        for (int j = 0; j < k; ++j) {
            if (nfree && rnd() % 3 == 0) { int t = (int)(rnd() % (uint32_t)nfree); ring[j] = -1 - side0_free[t]; side0_free[t] = side0_free[--nfree]; }
            else ring[j] = eused++;                          /* ne = 6 * ns + 8: never exhausted */
        }
        uint32_t vfirst = rnd() % (uint32_t)(nv - 8);
        for (int j = 0; j < k; ++j) {
            int e = ring[j] >= 0 ? ring[j] : -1 - ring[j], en = ring[(j + 1) % k] >= 0 ? ring[(j + 1) % k] : -1 - ring[(j + 1) % k];
            uint32_t Ed = edges + 24u * (uint32_t)e;
            if (ring[j] >= 0) {                              /* a new edge: this ring on side 1 or side 0 */
                int side = (int)(rnd() & 1);
                uint32_t va = vfirst + (uint32_t)j, vb = vfirst + (uint32_t)((j + 1) % k);
                if (side) { w32(Ed, vb); w32(Ed + 4, va); w32(Ed + 0x14, (uint32_t)s); w32(Ed + 0xC, (uint32_t)en); w32(Ed + 0x10, rnd() % (uint32_t)ns); w32(Ed + 8, rnd() % (uint32_t)ne); }
                else { w32(Ed, va); w32(Ed + 4, vb); w32(Ed + 0x10, (uint32_t)s); w32(Ed + 8, (uint32_t)en); w32(Ed + 0x14, (uint32_t)((s + 1 + (int)(rnd() % 3)) % ns)); w32(Ed + 0xC, rnd() % (uint32_t)ne);
                       if (nfree < ne) side0_free[nfree++] = e; }
                if (rnd() % 29 == 0) w32(Ed + 0x14, (uint32_t)s ^ 1u);
            } else {                                         /* shared: walk it on side 1 (its +0x14 becomes this ring) */
                w32(Ed + 0x14, (uint32_t)s); w32(Ed + 0xC, (uint32_t)en);
            }
        }
        uint32_t e0 = (uint32_t)(ring[0] >= 0 ? ring[0] : -1 - ring[0]);
        w32(S, rnd()); w32(S + 4, e0); w8(S + 8, (uint8_t)(rnd() % 4 == 0 ? 8 | rnd() : rnd() & ~8u)); w8(S + 9, (uint8_t)(rnd() % 48)); w16(S + 10, (uint16_t)rnd());
    }
    free(side0_free);
    /* a mis-sided edge (+0x14 changed) or a cut can leave a ring open: walk every ring the way the guest does and
     * close it at the first repeat, until a whole pass changes nothing (a ring that still never returns to its first
     * edge makes the guest loop: the case is skipped past the alarm) */
    for (int iter = 0; iter < 8; ++iter) {
        int changed = 0;
        for (int s = 0; s < ns; ++s) {
            uint32_t S = surfs + 12u * (uint32_t)s, e; x_guest_read_pages(&e, S + 4, 4);
            uint32_t first = e, seen[64]; int n = 0;
            for (;;) {
                uint32_t Ed = edges + 24u * e, r, nx; x_guest_read_pages(&r, Ed + 0x14, 4);
                int side = r == (uint32_t)s; x_guest_read_pages(&nx, Ed + 8 + 4 * (uint32_t)side, 4);
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
    /* the tested-surface bits and the capacity word */
    uint32_t bits = tag_alloc(64, 0);
    for (unsigned i = 0; i < 16; ++i) w32(bits + 4 * i, bench_like || big || wild ? 0xFFFFFFFFu : rnd() | rnd());
    /* the stack: E at a random 4-aligned place; the center and radius arguments; the result lists in-game just above */
    int layout = bench_like ? 0 : rnd() % 16;              /* 10: unaligned esp; 11: lists on the stack; 2: lists below esp */
    E_top = STACK + 0x10000u + 4u * (rnd() % 0x800);
    if (layout == 10) E_top += 1 + rnd() % 3;
    uint32_t res = E_top + 0x20u;                          /* 171F10's [esp+1Ch] in the game */
    if (layout == 1 || layout == 6) res = E_top + 12u;
    if (layout == 2) res = tag_alloc(0x1010, 0);
    if (layout == 3 || layout == 8) res = E_top + 0x3000u + 4u * (rnd() % 0x100);
    if (layout == 11) res = E_top - 0x3000u - 4u * (rnd() % 0x100);
    if (layout == 12) res = E_top - 4u * (rnd() % 0x80);    /* inside 88110's frame: declined */
    if (wild) res = E_top + 0x20u;
    uint32_t ctra = rnd() % 3 ? E_top + 0x1100u + 4 * (rnd() % 16) : rec_alloc(12);
    for (unsigned k = 0; k < 3; ++k) wf(ctra + 4 * k, ctr[k]);
    w32(E_top, 0x171F99u); w32(E_top + 4, ctra); wf(E_top + 8, radius);
    for (uint32_t a = res; a < res + 0x1010; a += 4) w32(a, rnd());   /* stale lists */
    w32(E_top + 0x2000, bsp); w32(E_top + 0x2004, res); w32(E_top + 0x2008, bits);   /* for init_ctx */
    if (big) st->hits++;
    if (deep) st->deep++;
    if (wild) st->dirty_like++;
    free(pl);
}

static void init_ctx(xctx *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    uint32_t bsp, res, bits;
    x_guest_read_pages(&bsp, E_top + 0x2000, 4); x_guest_read_pages(&res, E_top + 0x2004, 4); x_guest_read_pages(&bits, E_top + 0x2008, 4);
    c->r[4] = E_top; c->r[0] = bsp; c->r[6] = res; c->r[2] = bits;
    c->r[1] = (rnd() & 0xFFFF0000u) | (bench_like ? 0x100u : rnd() % 3 ? 0x100u : rnd() % 64);
    c->fsp = rnd() & 7; c->fcw = 0x027F; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    for (unsigned r = 0; r < 8; ++r) for (unsigned l = 0; l < 4; ++l) c->xmm[r][l] = frand(-100, 100);
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = 32;
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->df = rnd() % 53 == 0;
    c->preempt = (int32_t)(rnd() % 400) - 20;
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_of_override) F(f_cf) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* two NaNs are equal (which NaN payload an operation propagates is the host compiler's operand order) */
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    for (unsigned r = 0; r < 8; ++r) for (unsigned l = 0; l < 4; ++l)
        if (memcmp(&a->xmm[r][l], &b->xmm[r][l], 4) && !(isnan(a->xmm[r][l]) && isnan(b->xmm[r][l]))) { snprintf(why, n, "xmm%u[%u]", r, l); return 0; }
    if (memcmp(a->mm, b->mm, sizeof a->mm)) { snprintf(why, n, "mm"); return 0; }
#undef F
    return 1;
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
/* a wild scene can send the guest into unbounded recursion (a corrupted frame pops a node index that points back up the
 * tree): the host stack overflows; like a timeout, the case is skipped (the handler runs on its own stack) */
static void guard_stack(void)
{
    static uint8_t alt[1 << 16];
    stack_t ss; ss.ss_sp = alt; ss.ss_size = sizeof alt; ss.ss_flags = 0; sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_alarm; sa.sa_flags = SA_ONSTACK | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
}
/* mode 0: the guest body; 1/2: the hook (verify / native). Returns 0 on a timeout (env N4_ALARM seconds, default 6). */
static unsigned alarm_s = 6;
static int run(xctx *c, int mode)
{
    xv_native_4b9d0_force(mode);
    if (sigsetjmp(alarm_jmp, 1)) { xv_native_4b9d0_force(0); return 0; }
    alarm(alarm_s);
    if (getenv("N4_OBJECT")) {
        unsigned world_before = world_reference_calls;
        xv_native_object_query_force(mode);
        xv_native_4b9d0_object_query(c, object_reference);
        if (world_reference_calls != world_before) abort();
    } else if (mode) xv_native_4b9d0_query(c); else f_00088110(c);
    alarm(0);
    xv_native_4b9d0_force(0);
    return 1;
}

/* --replay <file> [reps]: queries captured in the game (XV_NATIVE_4B9D0_CAPTURE): the arena and page table once, then
 * per query the entry state and the guest stack [esp - 64 KB, esp + 64 KB). Exactness: each query guest then native
 * from the same state (xctx, the stack window, xv_preempt calls). Speed (reps > 0): a pass of all queries through the
 * guest, a pass through the native, reps times alternating; perf-counter user instructions and cycles. */
typedef struct { uint32_t r[8], fl[9], fsp, fsw, fcw, df; int32_t preempt; double st[8]; float xmm[8][4]; uint64_t mm[8]; } n4rec;
/* N4_PROF=<file>: a SIGPROF sampling profile of the timed native passes (one program counter per line, for addr2line) */
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
static void rec_to_ctx(const n4rec *q, xctx *c)
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
    uint32_t hdr[4]; if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x3444344Eu || hdr[2] != (1u << 20)) { fprintf(stderr, "bad capture\n"); return 2; }
    const uint32_t arena = hdr[1], W = hdr[3];
#ifdef N4_FUSED_GUEST
    n4_arena_size = arena;
#endif
    g_xram = malloc(arena + 4096u); g_xpt = malloc(4u << 20); g_img_base = g_xram;
    if (!g_xram || !g_xpt || fread(g_xpt, 4, 1u << 20, f) != (1u << 20) || fread(g_xram, 1, arena, f) != arena) { fprintf(stderr, "short capture\n"); return 2; }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
    unsigned n = 0, cap = 256; n4rec *recs = malloc(sizeof *recs * cap); uint8_t *wins = malloc((size_t)W * cap);
    for (;;) {
        if (n == cap) { cap *= 2; recs = realloc(recs, sizeof *recs * cap); wins = realloc(wins, (size_t)W * cap); }
        if (fread(&recs[n], sizeof *recs, 1, f) != 1 || fread(wins + (size_t)W * n, 1, W, f) != W) break;
        n++;
    }
    fclose(f);
    printf("replay: %u queries, arena %u bytes\n", n, arena);
    uint8_t *gw = malloc(W), *nw = malloc(W);
    unsigned bad = 0, nan_words = 0;
    for (unsigned i = 0; i < n; ++i) {
        const uint32_t lo = recs[i].r[4] - W / 2;
        xctx cg, cn; rec_to_ctx(&recs[i], &cg); rec_to_ctx(&recs[i], &cn);
        x_guest_write_pages(lo, wins + (size_t)W * i, W); preempt_calls = 0;
        N4_GUEST(&cg); unsigned gp = preempt_calls; x_guest_read_pages(gw, lo, W);
        x_guest_write_pages(lo, wins + (size_t)W * i, W); preempt_calls = 0;
        xv_native_4b9d0_force(2); xv_native_4b9d0_query(&cn); xv_native_4b9d0_force(0);
        x_guest_read_pages(nw, lo, W);
        char why[200] = "";
        int ok = same_ctx(&cn, &cg, why, sizeof why) && preempt_calls == gp;
        if (ok && memcmp(gw, nw, W))
            for (uint32_t k = 0; k < W; ++k) if (gw[k] != nw[k]) {
                uint32_t a, b; memcpy(&a, gw + (k & ~3u), 4); memcpy(&b, nw + (k & ~3u), 4);
                if (nan32(a) && nan32(b)) { nan_words++; k |= 3u; continue; }
                ok = 0; snprintf(why, sizeof why, "stack %08X guest %02X native %02X", lo + k, gw[k], nw[k]); break;
            }
        if (!ok && ++bad <= 10) printf("query %u MISMATCH: %s\n", i, why[0] ? why : "xv_preempt calls");
    }
    printf("replay: %u queries compared, %u mismatches, %u NaN-payload words\n", n, bad, nan_words);
    if (reps > 0) {
        const char *prof = getenv("N4_PROF");
        if (prof) {
            prof_pc = malloc(sizeof *prof_pc * prof_cap);
            struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
            sigaction(SIGPROF, &sa, NULL);
            struct itimerval it = { { 0, 97 }, { 0, 97 } }; setitimer(ITIMER_PROF, &it, NULL);
        }
        pe_open();
        double ns[2] = { 0, 0 }; uint64_t pe[2][2] = { { 0, 0 }, { 0, 0 } }; struct timespec t0, t1;
        for (int r = 0; r < reps; ++r)
            for (int mode = 0; mode <= 2; mode += 2) {
                for (unsigned i = 0; i < n; ++i) {
                    const uint32_t lo = recs[i].r[4] - W / 2;
                    x_guest_write_pages(lo, wins + (size_t)W * i, W);
                    xctx c; rec_to_ctx(&recs[i], &c); c.preempt = 1 << 30;
                    xv_native_4b9d0_force(mode);
                    uint64_t p0[2], p1[2]; pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                    prof_on = mode == 2;
                    if (mode) xv_native_4b9d0_query(&c); else N4_GUEST(&c);
                    prof_on = 0;
                    clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                    ns[mode / 2] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    pe[mode / 2][0] += p1[0] - p0[0]; pe[mode / 2][1] += p1[1] - p0[1];
                }
                xv_native_4b9d0_force(0);
            }
        if (prof) {
            struct itimerval z; memset(&z, 0, sizeof z); setitimer(ITIMER_PROF, &z, NULL);
            FILE *pf = fopen(prof, "w");
            if (pf) { for (unsigned i = 0; i < prof_n; ++i) fprintf(pf, "%lx\n", (unsigned long)prof_pc[i]); fclose(pf); }
            printf("profile: %u samples to %s\n", prof_n, prof);
        }
        const double calls = (double)n * reps;
        printf("replay speed: %u queries x %d: guest %.0f ns/call, native %.0f ns/call, ratio %.2fx; user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               n, reps, ns[0] / calls, ns[1] / calls, ns[0] / ns[1], pe[0][0] / calls, pe[1][0] / calls, pe[1][0] ? (double)pe[0][0] / pe[1][0] : 0,
               pe[0][1] / calls, pe[1][1] / calls, pe[1][1] ? (double)pe[0][1] / pe[1][1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
    }
    log_all = 1; xv_native_4b9d0_report(0);
    return bad != 0;
}

/* ==== N4_PART=features: the solver's feature test (native f_000864C0 subtree vs the lifted bodies of f_000864C0 +
 * f_00085D10 + f_00085A00 + f_00085720 + f_00011120 + f_000111A0). Random feature sets around a random move: spheres,
 * capsules and plane prisms placed across the path (hits, grazes, misses, starting inside), zero / parallel / NaN
 * directions, degenerate normals and axes, prisms with 0..12 vertices (beyond the record), axis words and side bytes
 * outside the table (the table and the frame read at other indices), negative counts, random image constants; the
 * layouts: the game's (start and dir in the caller's frame above, the record elsewhere), frames straddling a page end
 * of the reversed stack, start / dir / features inside the frames (the guest's stores change what it reads), the
 * record over start / dir / the features (the literal tail), and the declined ones (esp unaligned, the record in the
 * frames). ------------------------------------------------------------------------------------------------------ */
extern void f_000864C0(xctx *);
extern int xv_native_4b9d0_features(xctx *);
static int part5;
static uint32_t E5;
static void wv3(uint32_t a, const float *v) { for (unsigned k = 0; k < 3; ++k) wf(a + 4 * k, v[k]); }
static float odd(float x, int flavor) { if (flavor == 2 && rnd() % 23 == 0) return rnd() % 3 ? nanf_() : rnd() % 2 ? INFINITY : -INFINITY; return x; }
static void scene5(stats *st)
{
    memset(g_xram, 0, ARENA);
    tag_top = TAG + (rnd() % 64) * 4;
    const int flavor = bench_like ? 0 : rnd() % 4 == 0 ? 1 : rnd() % 8 == 0 ? 2 : 0;
    const unsigned layout = bench_like ? 0 : rnd() % 16;
    /* image constants: the axes table, 0.0, 1.0, eps (double), the facing threshold */
    for (unsigned a = 0; a < 6; ++a) {
        uint16_t u = (uint16_t)((a / 2 + 1) % 3), v = (uint16_t)((a / 2 + 2) % 3);
        if (a & 1) { uint16_t t = u; u = v; v = t; }
        if (!bench_like && rnd() % 41 == 0) u = (uint16_t)(int16_t)((int)(rnd() % 13) - 5);
        if (!bench_like && rnd() % 41 == 0) v = (uint16_t)(int16_t)((int)(rnd() % 13) - 5);
        w16(0x1EAF30 + 4 * a, u); w16(0x1EAF32 + 4 * a, v);
    }
    for (uint32_t a = 0x1EAF30 - 0x40; a < 0x1EAF30; a += 2) w16(a, (uint16_t)(int16_t)((int)(rnd() % 9) - 4));   /* below the table */
    for (uint32_t a = 0x1EAF48; a < 0x1EAF48 + 0x100; a += 2) w16(a, (uint16_t)(int16_t)((int)(rnd() % 9) - 4));  /* above it */
    wf(0x1F0A68, bench_like || rnd() % 5 ? 0.0f : rnd() % 2 ? -0.0f : rnd() % 3 ? frand(-0.01f, 0.01f) : nanf_());
    wf(0x1F0A78, bench_like || rnd() % 4 ? 1.0f : rnd() % 5 ? frand(0.3f, 2.0f) : nanf_());
    { double eps = bench_like || rnd() % 5 ? 0.0001 : rnd() % 2 ? 0.0 : rnd() % 2 ? 1e-12 : 0.75; x_guest_write_pages(0x1F0AF8, &eps, 8); }
    wf(0x1F0C24, bench_like || rnd() % 3 ? -0.0001f : rnd() % 2 ? 0.0f : frand(-0.5f, 0.5f));
    /* the move */
    float start[3] = { frand(-4, 4), frand(-4, 4), frand(-4, 4) }, dir[3];
    const unsigned dk = bench_like ? 0 : rnd() % 12;
    if (dk == 1) dir[0] = dir[1] = dir[2] = 0;
    else if (dk == 2 || flavor == 1) { dir[0] = dir[1] = dir[2] = 0; dir[rnd() % 3] = frand(-2, 2); }
    else for (unsigned k = 0; k < 3; ++k) dir[k] = frand(-1.5f, 1.5f);
    if (flavor == 1) for (unsigned k = 0; k < 3; ++k) start[k] = (float)(int)start[k];
    for (unsigned k = 0; k < 3; ++k) { start[k] = odd(start[k], flavor); dir[k] = odd(dir[k], flavor); }
    /* the stack: 864C0's entry esp; now and then its frames straddle a page end (the stack pages are reversed) */
    E5 = STACK + 0x8000u + 4u * (rnd() % 0x800);
    if (rnd() % 5 == 0) E5 = STACK + 0xA000u - 0x10u + 4u * (rnd() % 0x28);
    for (uint32_t a = E5 - 0x200u; a < E5 + 0x200u; a += 4) w32(a, rnd());   /* stale frames */
    /* the features */
    int n0 = (int)(rnd() % 6), n1 = (int)(rnd() % 6), n2 = (int)(rnd() % 7);
    if (!bench_like) {
        if (rnd() % 9 == 0) n0 = (int)(rnd() % 20); if (rnd() % 9 == 0) n1 = (int)(rnd() % 20); if (rnd() % 9 == 0) n2 = (int)(rnd() % 20);
        if (rnd() % 17 == 0) n0 = -(int)(rnd() % 3) - 1; if (rnd() % 17 == 0) n1 = -1 - (int)(rnd() % 3); if (rnd() % 17 == 0) n2 = -1;
    }
    uint32_t feat = tag_alloc(0x4408u + 0x68u * (uint32_t)(n2 > 0 ? n2 : 0) + 0x80u, 0);
    if (layout == 14) feat = E5 - 0x60u - 0x1Cu * (rnd() % 3) - 8u;   /* spheres over the frames (and the counts in them) */
    w16(feat, (uint16_t)n0); w16(feat + 2, (uint16_t)n1); w16(feat + 4, (uint16_t)n2);
    for (int i = 0; i < n0; ++i) {                                   /* spheres: +0xC center, +0x18 radius */
        const uint32_t r = feat + 8u + 0x1Cu * (uint32_t)i;
        if (layout == 14 && r + 0x1C > E5 - 0x90u && r < E5 + 0x14u && rnd() % 2) continue;   /* keep some frame bytes */
        for (unsigned k = 0; k < 3; ++k) w32(r + 4 * k, rnd());
        const float t = frand(-0.3f, 1.3f), rad = rnd() % 11 ? frand(0.1f, 2.0f) : rnd() % 2 ? 0.0f : -frand(0, 1);
        float c[3];
        for (unsigned k = 0; k < 3; ++k) c[k] = odd(start[k] + t * dir[k] + frand(-1.5f, 1.5f) * (rnd() % 3 ? rad : 3.0f), flavor);
        if (rnd() % 9 == 0) for (unsigned k = 0; k < 3; ++k) c[k] = start[k] + frand(-0.2f, 0.2f);   /* starts inside */
        wv3(r + 0xC, c); wf(r + 0x18, odd(rad, flavor));
    }
    for (int i = 0; i < n1; ++i) {                                   /* capsules: +0xC point, +0x18 axis, +0x24 radius */
        const uint32_t r = feat + 0x1C08u + 0x28u * (uint32_t)i;
        for (unsigned k = 0; k < 3; ++k) w32(r + 4 * k, rnd());
        const float t = frand(-0.3f, 1.3f), rad = rnd() % 11 ? frand(0.1f, 1.5f) : 0.0f;
        float p[3], ax[3];
        for (unsigned k = 0; k < 3; ++k) { ax[k] = frand(-2.5f, 2.5f); p[k] = start[k] + t * dir[k] + frand(-1.2f, 1.2f) - 0.5f * ax[k] * frand(0, 1); }
        const unsigned ak = rnd() % 10;
        if (ak == 0) ax[0] = ax[1] = ax[2] = 0;                                     /* A == 0 */
        else if (ak == 1) for (unsigned k = 0; k < 3; ++k) ax[k] = dir[k] * frand(-2, 2);   /* parallel: A == 0 up to rounding */
        else if (ak == 2 || flavor == 1) { ax[0] = ax[1] = ax[2] = 0; ax[rnd() % 3] = frand(0.5f, 3); }
        for (unsigned k = 0; k < 3; ++k) { p[k] = odd(p[k], flavor); ax[k] = odd(ax[k], flavor); }
        wv3(r + 0xC, p); wv3(r + 0x18, ax); wf(r + 0x24, odd(rad, flavor));
    }
    for (int i = 0; i < n2; ++i) {                                   /* plane prisms */
        const uint32_t r = feat + 0x4408u + 0x68u * (uint32_t)i;
        for (unsigned k = 0; k < 3; ++k) w32(r + 4 * k, rnd());
        float nrm[3]; random_normal(nrm, flavor);
        if (rnd() % 23 == 0) nrm[0] = nrm[1] = nrm[2] = 0;
        if (rnd() % 7 == 0)                                          /* parallel to the move (nd == 0) where dir has a zero */
            for (unsigned j = 0; j < 3; ++j) if (dir[j] == 0.0f) { nrm[0] = nrm[1] = nrm[2] = 0; nrm[j] = rnd() % 2 ? 1.0f : -1.0f; break; }
        const float t = frand(-0.3f, 1.3f);
        float P[3]; for (unsigned k = 0; k < 3; ++k) P[k] = start[k] + t * dir[k];
        float d = nrm[0] * P[0] + nrm[1] * P[1] + nrm[2] * P[2];
        if (rnd() % 6 == 0) d = nrm[0] * start[0] + nrm[1] * start[1] + nrm[2] * start[2] - frand(-0.5f, 0.5f);   /* start near / inside */
        if (flavor == 1) d = (float)(int)d;
        const float thick = rnd() % 5 ? frand(0.05f, 1.5f) : rnd() % 2 ? 0.0f : -frand(0, 1);
        unsigned ax = fabsf(nrm[0]) >= fabsf(nrm[1]) && fabsf(nrm[0]) >= fabsf(nrm[2]) ? 0 : fabsf(nrm[1]) >= fabsf(nrm[2]) ? 1 : 2;
        uint8_t side = nrm[ax] < 0;
        int16_t axw = (int16_t)ax;
        if (!bench_like && rnd() % 19 == 0) axw = (int16_t)((int)(rnd() % 9) - 4);
        if (!bench_like && rnd() % 97 == 0) axw = (int16_t)(rnd() % 0x2000);
        if (!bench_like && rnd() % 19 == 0) side = (uint8_t)(rnd() % 2 ? rnd() % 4 : 255);
        int nv = 3 + (int)(rnd() % 6);
        if (!bench_like && rnd() % 9 == 0) nv = (int)(rnd() % 13) - 1;
        wv3(r + 0xC, nrm); wf(r + 0x18, odd(d, flavor)); wf(r + 0x1C, odd(thick, flavor));
        w16(r + 0x20, (uint16_t)axw); w8(r + 0x22, side); w8(r + 0x23, (uint8_t)rnd()); w32(r + 0x24, (uint32_t)nv);
        /* the polygon on the projected axes (the table's standard (u, v) for the axis and side), around the path */
        const unsigned u = (ax + 1 + side % 2) % 3, v = (ax + 2 - side % 2) % 3;
        const float cu = P[u] + frand(-0.8f, 0.8f), cv = P[v] + frand(-0.8f, 0.8f), rr = frand(0.2f, 2.5f);
        const int ccw = rnd() % 2;
        const int nw = nv > 12 ? 12 : nv;
        for (int j = 0; j < nw; ++j) {
            float a = 6.2831853f * ((float)j + frand(0.1f, 0.9f)) / (float)(nw > 0 ? nw : 1);
            if (!ccw) a = -a;
            float pu = cu + rr * cosf(a), pv = cv + rr * sinf(a);
            if (flavor == 1) { pu = (float)(int)pu; pv = (float)(int)pv; }
            wf(r + 0x28 + 8u * (uint32_t)j, odd(pu, flavor)); wf(r + 0x2C + 8u * (uint32_t)j, odd(pv, flavor));
        }
    }
    /* start, dir and the result record */
    uint32_t sa = E5 + 0x68u, da = E5 + 0x50u, res = tag_alloc(0x2C, 0);
    if (rnd() % 3 == 0) res = E5 + 0x100u + 4u * (rnd() % 0x40);
    if (layout == 12) { if (rnd() % 2) sa = E5 - 0x90u + 4u * (rnd() % 0x22); else da = E5 - 0x90u + 4u * (rnd() % 0x22); }
    if (layout == 15) { sa = rec_alloc(12); da = rec_alloc(12); }
    if (layout == 13) res = rnd() % 3 == 0 ? sa - 4u * (rnd() % 8) : rnd() % 2 ? da - 4u * (rnd() % 8) : feat + 4u * (rnd() % 16);
    if (layout == 11) res = E5 - 4u * (rnd() % 0x30);                  /* in the frames: declined */
    wv3(sa, start); wv3(da, dir);
    for (uint32_t a = res; a < res + 0x2C; a += 4) if (!(layout == 13)) w32(a, rnd());
    w32(E5, 0x170CD6u); w32(E5 + 4, feat); w32(E5 + 8, sa); w32(E5 + 12, da); w32(E5 + 16, res);
    if (layout == 10) E5 += 1u + rnd() % 3;                            /* esp unaligned: declined */
    st->hits += n2 > 0; st->deep += layout == 12 || layout == 13 || layout == 14; st->dirty_like += flavor == 2;
}
static void init_ctx5(xctx *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = E5;
    c->fsp = rnd() & 7; c->fcw = 0x027F; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    for (unsigned r = 0; r < 8; ++r) for (unsigned l = 0; l < 4; ++l) c->xmm[r][l] = frand(-100, 100);
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = rnd() % 3 == 0 ? 16 : 32;
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->preempt = (int32_t)(rnd() % 400) - 20;
}
/* mode 0: the guest body; 1/2: the hook (declined: the guest, as the fused solver code runs it) */
static int run5(xctx *c, int mode)
{
    xv_native_4b9d0_force(mode);
    if (sigsetjmp(alarm_jmp, 1)) { xv_native_4b9d0_force(0); return 0; }
    alarm(alarm_s);
    if (!mode || !xv_native_4b9d0_features(c)) f_000864C0(c);
    alarm(0);
    xv_native_4b9d0_force(0);
    return 1;
}

/* --replay-features <file> [reps]: feature-test calls captured in the game (XV_NATIVE_4B9D0_CAPTURE_FEATURES) */
static int replay5(const char *path, int reps)
{
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 2; }
    uint32_t hdr[4]; if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x3544354Eu || hdr[2] != (1u << 20)) { fprintf(stderr, "bad capture\n"); return 2; }
    const uint32_t arena = hdr[1], W = hdr[3];
    g_xram = malloc(arena + 4096u); g_xpt = malloc(4u << 20); g_img_base = g_xram;
    if (!g_xram || !g_xpt || fread(g_xpt, 4, 1u << 20, f) != (1u << 20) || fread(g_xram, 1, arena, f) != arena) { fprintf(stderr, "short capture\n"); return 2; }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
    unsigned n = 0, cap = 256; n4rec *recs = malloc(sizeof *recs * cap); uint8_t *wins = malloc((size_t)W * cap);
    uint32_t *fa = malloc(4 * cap), *fl = malloc(4 * cap); uint8_t **fb = malloc(sizeof *fb * cap);
    for (;;) {
        if (n == cap) { cap *= 2; recs = realloc(recs, sizeof *recs * cap); wins = realloc(wins, (size_t)W * cap); fa = realloc(fa, 4 * cap); fl = realloc(fl, 4 * cap); fb = realloc(fb, sizeof *fb * cap); }
        if (fread(&recs[n], sizeof *recs, 1, f) != 1 || fread(wins + (size_t)W * n, 1, W, f) != W) break;
        if (fread(&fa[n], 4, 1, f) != 1 || fread(&fl[n], 4, 1, f) != 1) break;
        fb[n] = malloc(fl[n]); if (fread(fb[n], 1, fl[n], f) != fl[n]) break;
        n++;
    }
    fclose(f);
    printf("replay: %u feature-test calls, arena %u bytes\n", n, arena);
    uint8_t *gw = malloc(W), *nw = malloc(W);
    unsigned bad = 0, nan_words = 0, hits = 0;
    for (unsigned i = 0; i < n; ++i) {
        const uint32_t lo = recs[i].r[4] - W / 2;
        uint32_t res; memcpy(&res, wins + (size_t)W * i + W / 2 + 0x10, 4);
        xctx cg, cn; rec_to_ctx(&recs[i], &cg); rec_to_ctx(&recs[i], &cn);
        uint8_t rg[0x2C], rn[0x2C];
        x_guest_write_pages(lo, wins + (size_t)W * i, W); x_guest_write_pages(fa[i], fb[i], fl[i]); preempt_calls = 0;
        f_000864C0(&cg); unsigned gp = preempt_calls; x_guest_read_pages(gw, lo, W); x_guest_read_pages(rg, res, 0x2C);
        x_guest_write_pages(lo, wins + (size_t)W * i, W); x_guest_write_pages(fa[i], fb[i], fl[i]); preempt_calls = 0;
        xv_native_4b9d0_force(2); if (!xv_native_4b9d0_features(&cn)) f_000864C0(&cn); xv_native_4b9d0_force(0);
        x_guest_read_pages(nw, lo, W); x_guest_read_pages(rn, res, 0x2C);
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
        if (!ok && ++bad <= 10) printf("call %u MISMATCH: %s\n", i, why[0] ? why : "xv_preempt calls");
    }
    printf("replay: %u calls compared (%u hits), %u mismatches, %u NaN-payload words\n", n, hits, bad, nan_words);
    if (reps > 0) {
        pe_open();
        double ns[2] = { 0, 0 }; uint64_t pe[2][2] = { { 0, 0 }, { 0, 0 } }; struct timespec t0, t1;
        for (int r = 0; r < reps; ++r)
            for (int mode = 0; mode <= 2; mode += 2) {
                for (unsigned i = 0; i < n; ++i) {
                    const uint32_t lo = recs[i].r[4] - W / 2;
                    x_guest_write_pages(lo, wins + (size_t)W * i, W); x_guest_write_pages(fa[i], fb[i], fl[i]);
                    xctx c; rec_to_ctx(&recs[i], &c); c.preempt = 1 << 30;
                    xv_native_4b9d0_force(mode);
                    uint64_t p0[2], p1[2]; pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                    if (mode) { if (!xv_native_4b9d0_features(&c)) f_000864C0(&c); } else f_000864C0(&c);
                    clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                    ns[mode / 2] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    pe[mode / 2][0] += p1[0] - p0[0]; pe[mode / 2][1] += p1[1] - p0[1];
                }
                xv_native_4b9d0_force(0);
            }
        const double calls = (double)n * reps;
        printf("replay speed: %u calls x %d: guest %.0f ns/call, native %.0f ns/call, ratio %.2fx; user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               n, reps, ns[0] / calls, ns[1] / calls, ns[0] / ns[1], pe[0][0] / calls, pe[1][0] / calls, pe[1][0] ? (double)pe[0][0] / pe[1][0] : 0,
               pe[0][1] / calls, pe[1][1] / calls, pe[1][1] ? (double)pe[0][1] / pe[1][1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
    }
    log_all = 1; xv_native_4b9d0_report(0);
    return bad != 0;
}

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[3], "--replay-features")) return replay5(argv[4], argc > 5 ? atoi(argv[5]) : 0);
    { const char *p = getenv("N4_PART"); part5 = p && !strcmp(p, "features"); }
    if (argc > 3 && !strcmp(argv[3], "--replay")) return replay(argv[4], argc > 5 ? atoi(argv[5]) : 0);
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    const int verify = argc > 3 && !strcmp(argv[3], "--verify");
    const int bench = argc > 4 && !strcmp(argv[3], "--bench") ? atoi(argv[4]) : 0;
    stats s = {0};
    signal(SIGALRM, on_alarm);
    if (getenv("N4_ALARM")) alarm_s = (unsigned)atoi(getenv("N4_ALARM"));
    guard_stack();
    map_memory();
    if (bench) {
        /* game-like queries repeated on their own state (each repetition clears and refills the lists like the game)
         * guest and native alternating; timer and counter-read costs subtracted */
        bench_like = 1;
        double g_ns = 0, n_ns = 0; unsigned calls = 0; struct timespec t0, t1;
        uint64_t g_pe[2] = { 0, 0 }, n_pe[2] = { 0, 0 }; pe_open();
        for (unsigned k = 0; k < cases; ++k) {
            if (part5) scene5(&s); else scene(&s);
            slice = 1 << 30;
            xctx c0; if (part5) init_ctx5(&c0); else init_ctx(&c0);
            c0.preempt = 1 << 30; c0.df = 0;
            for (int r = 0; r < bench; ++r)
                for (int mode = 0; mode <= 2; mode += 2) {
                    xctx c = c0; xv_native_4b9d0_force(mode);
                    uint64_t p0[2], p1[2]; pe_read(p0);
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                    if (part5) { if (!mode || !xv_native_4b9d0_features(&c)) f_000864C0(&c); }
                    else if (mode) xv_native_4b9d0_query(&c); else f_00088110(&c);
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    pe_read(p1);
                    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    if (mode) { n_ns += ns; n_pe[0] += p1[0] - p0[0]; n_pe[1] += p1[1] - p0[1]; }
                    else { g_ns += ns; g_pe[0] += p1[0] - p0[0]; g_pe[1] += p1[1] - p0[1]; }
                }
            calls += (unsigned)bench;
        }
        xv_native_4b9d0_force(0);
        double z = 0;
        for (int i = 0; i < 20000; ++i) { clock_gettime(CLOCK_MONOTONIC, &t0); clock_gettime(CLOCK_MONOTONIC, &t1); z += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec); }
        z /= 20000;
        printf("native-4b9d0 bench: %u calls each: guest %.1f ns/call, native %.1f ns/call, ratio %.2fx (timer %.1f ns subtracted)\n",
               calls, g_ns / calls - z, n_ns / calls - z, (g_ns / calls - z) / (n_ns / calls - z), z);
        printf("native-4b9d0 bench: user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               (double)g_pe[0] / calls, (double)n_pe[0] / calls, n_pe[0] ? (double)g_pe[0] / n_pe[0] : 0, (double)g_pe[1] / calls, (double)n_pe[1] / calls,
               n_pe[1] ? (double)g_pe[1] / n_pe[1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
        log_all = 1; xv_native_4b9d0_report(0);
        return 0;
    }
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    for (unsigned k = 0; k < cases; ++k) {
        if (part5) scene5(&s); else scene(&s);
        slice = 5 + (int)(rnd() % 90);
        xctx c0; if (part5) init_ctx5(&c0); else init_ctx(&c0);
        if (getenv("N4_FROM") && k < (unsigned)atoi(getenv("N4_FROM"))) continue;
        if (getenv("N4_TRACE")) { fprintf(stderr, "case %u\n", k); fflush(stderr); }
        memcpy(before, g_xram, ARENA);
        xctx cg = c0; preempt_calls = 0;
        int gok = part5 ? run5(&cg, 0) : run(&cg, 0);
        unsigned guest_preempts = preempt_calls;
        memcpy(guest, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = 0; log_mismatch = 0;
        int nok = part5 ? run5(&cn, 2) : run(&cn, 2);
        unsigned native_preempts = preempt_calls;
        char why[200] = "";
        s.cases++;
        if (!gok) { s.timeouts++; continue; }
        if (!part5) {                              /* the result lists that reached their capacity (0x100 entries) */
            uint32_t res; x_guest_read_pages(&res, E_top + 0x2004, 4);
            for (unsigned l = 0; l < 4; ++l) {
                uint32_t a = res + 0x404u * l, n = 0;
                const uint32_t pa = g_xpt[a >> 12] + (a & 0xFFFu);
                if (pa + 4u <= ARENA) memcpy(&n, guest + pa, 4);
                if (n >= 0x100u && n < 0x80000000u) s.full[l]++;
            }
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
                if (getenv("N4_DEBUG")) {
                    unsigned shown = 0;
                    for (uint32_t i = 0; i < ARENA && shown < 24; ++i) if (guest[i] != g_xram[i]) {
                        uint32_t va = 0xFFFFFFFFu;
                        for (uint32_t p = 0; p < (1u << 20); ++p) if (g_xpt[p] == (i & ~0xFFFu)) { va = (p << 12) | (i & 0xFFFu); break; }
                        printf("   va %08X (E%+d) before %02X guest %02X native %02X\n", va, (int)(va - (part5 ? E5 : E_top)), before[i], guest[i], g_xram[i]); shown++;
                    }
                    printf("   E %08X eax %08X/%08X ecx %08X/%08X edx %08X/%08X fsw %04X/%04X preempts %u/%u\n", part5 ? E5 : E_top, cg.r[0], cn.r[0], cg.r[1], cn.r[1], cg.r[2], cn.r[2], cg.fsw, cn.fsw, guest_preempts, native_preempts);
                }
            }
        }
        if (verify && gok) {
            memcpy(g_xram, before, ARENA);
            xctx cv = c0; preempt_calls = 0; log_mismatch = 0;
            int vok = part5 ? run5(&cv, 1) : run(&cv, 1);
            char vw[200] = "";
            int good = vok && !log_mismatch && same_ctx(&cv, &cg, vw, sizeof vw) && preempt_calls == guest_preempts && !memcmp(guest, g_xram, ARENA);
            if (!vok) s.verify_timeouts++;
            else if (!good) {
                const char *what = log_mismatch ? "log" : vw[0] ? vw : preempt_calls != guest_preempts ? "xv_preempt calls" : "arena";
                if (++s.verify_mismatch <= 10) printf("case %u VERIFY: log mismatches %d, %s\n", k, log_mismatch, what);
            }
        }
    }
    log_all = 1; xv_native_4b9d0_report(0); log_all = 0;     /* the native's own counters over all native/verify runs */
    if (part5)
        printf("native-4b9d0 features differential: %u cases (%u with prisms, %u aliased layouts, %u NaN/inf scenes, %u guest timeouts skipped, %u NaN-payload words), %u mismatches%s",
               s.cases, s.hits, s.deep, s.dirty_like, s.timeouts, s.nan_words, s.mismatches, verify ? "" : "\n");
    else
    printf("native-4b9d0 differential: %u cases (%u big, %u deep, %u wild, %u guest timeouts skipped, %u NaN-payload words; "
           "full lists: surfaces %u edges %u vertices %u leaves %u), %u mismatches%s",
           s.cases, s.hits, s.deep, s.dirty_like, s.timeouts, s.nan_words, s.full[0], s.full[1], s.full[2], s.full[3], s.mismatches, verify ? "" : "\n");
    if (verify) printf(", verify-mode failures %u (%u verify runs past the alarm skipped)\n", s.verify_mismatch, s.verify_timeouts);
    return s.mismatches || s.verify_mismatch;
}
