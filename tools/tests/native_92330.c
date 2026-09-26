/* Differential test: native f_00056670 (recomp/kernel/xk_native_92330.c) against the lifted guest bodies of
 * f_00056670 + f_00052240 + f_00051E90 + f_00011840 + f_000B77C0 + f_000A9330 (extracted from a stage's shards by
 * tools/test_native_92330.py), on randomized light cluster queries in a synthetic guest arena: shuffled physical
 * pages, random BSP cluster/portal graphs with convex portal polygons in random planes, axis-aligned planes and
 * ties between normal components, degenerate (repeated) polygon vertices, empty polygons, a 129/130-vertex portal
 * (the projected polygon runs over the frame: the native's `dirty` path), the stack placed inside the visited-stamp
 * range with portals aimed at live stack slots (stamp aliasing), NaN/negative/zero radii and eps, cluster 0xFFFF,
 * full and fragmented datum arrays (hint past the end, salt wrap, element sizes that are not multiples of 4),
 * pre-visited stamps, both direction flags and a small back-edge slice.
 * Each case runs the guest body (hook off) and the native (mode 2) from identical state and compares the whole
 * arena, every xctx field (f_cf/f_of only while their override is set) and the xv_preempt call count; with --verify
 * it also runs the verify mode (mode 1) and requires the guest result and no MISMATCH line. */
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
    if (log_all || getenv("N92_DEBUG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
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
/* Same semantics as recomp/xv_x86rt.c (both the guest body and the native call it). */
void x_str_stos(xctx *c, unsigned sz, int mode)
{
    uint32_t v = c->r[0];
    do {
        if (mode != X_STR_ONCE && !c->r[1]) break;
        if (sz == 4) { uint32_t w = v; x_guest_write(c->r[7], &w, 4); }
        else if (sz == 2) { uint16_t w = (uint16_t)v; x_guest_write(c->r[7], &w, 2); }
        else { uint8_t w = (uint8_t)v; x_guest_write(c->r[7], &w, 1); }
        c->r[7] += c->df ? (uint32_t)-(int32_t)sz : sz;
        if (mode != X_STR_ONCE) c->r[1]--;
    } while (mode != X_STR_ONCE);
}
extern void f_00056670(xctx *);
#ifdef XV_NATIVE_52240_TEST
extern void f_00052240(xctx *);
extern void xv_native_52240_test(xctx *);
extern void xv_native_52240_test_counts(uint64_t out[3]);
#endif
extern void xv_native_92330_force(int);

enum { ARENA = 24u << 20, IMAGE_PAGES = 0x400, TAG = 0x40000000u, TAG_PAGES = 768, STACK = 0xD0000000u, STACK_PAGES = 8,
       ALIAS_STACK = 0x2C0000u };
static uint32_t trash_off, next_free, stack_base;
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
    for (uint32_t i = 0; i < IMAGE_PAGES; ++i) g_xpt[i] = i << 12;       /* image: identity, so X_IMG == X_G */
    g_img_base = g_xram; next_free = IMAGE_PAGES << 12;
    uint32_t perm[TAG_PAGES]; for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = next_free + (perm[i] << 12);   /* shuffled physical pages */
    next_free += TAG_PAGES << 12;
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = next_free + ((STACK_PAGES - 1 - i) << 12);
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

typedef struct { float n[3], d; } plane;
static void random_plane(plane *p, int flavor)
{
    if (flavor == 1 || rnd() % 5 == 0) {                        /* axis-aligned, either sign */
        memset(p->n, 0, sizeof p->n); p->n[rnd() % 3] = rnd() & 1 ? 1.0f : -1.0f;
    } else if (rnd() % 7 == 0) {                                 /* ties between |components| */
        float a = rnd() & 1 ? 0.70710677f : 0.57735026f;
        for (unsigned i = 0; i < 3; ++i) p->n[i] = (rnd() & 1 ? a : -a) * (a > 0.6f && i == rnd() % 3 ? 0.0f : 1.0f);
    } else {
        float l;
        do { for (unsigned i = 0; i < 3; ++i) p->n[i] = frand(-1, 1); l = sqrtf(p->n[0] * p->n[0] + p->n[1] * p->n[1] + p->n[2] * p->n[2]); } while (l < 0.05f);
        for (unsigned i = 0; i < 3; ++i) p->n[i] /= l;
    }
    p->d = frand(-40, 40); if (flavor == 1) p->d = (float)(int)p->d;
    if (flavor == 2 && rnd() % 9 == 0) p->n[rnd() % 3] = nanf_();
}

typedef struct { unsigned cases, native_hit, flood_cases, linked, dirty_cases, alias_cases, timeouts, mismatches, verify_mismatch, verify_timeouts, nan_words; } stats;
static int nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }

static uint32_t E_top;
static int game_like;          /* --bench-game: a10-like queries (few clusters, ~1 full portal test, ~25-slot datum scans) */
static uint32_t mut_addr[8], mut_len[8]; static unsigned mut_n;   /* what a query changes besides the stack and the image */
static void scene(stats *st, int *alias)
{
    memset(g_xram, 0, ARENA);
    tag_top = TAG + (rnd() % 64) * 4;
    const int flavor = game_like ? 0 : rnd() % 4 == 0 ? 1 : rnd() % 8 == 0 ? 2 : 0;
    *alias = !game_like && rnd() % 23 == 0;
    const int wrap = !game_like && !*alias && rnd() % 40 == 0, big = !game_like && rnd() % 8 == 0;
    /* stack: normally its own pages; the alias flavor puts it inside the visited-stamp range of the image, the wrap
     * flavor straddles address 0 (the list pointer carries out of bit 31 in the link loop) */
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[0xFFFF8u + i] = trash_off;
    if (wrap) for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[0xFFFF8u + i] = g_xpt[(STACK >> 12) + i];
    stack_base = *alias ? ALIAS_STACK : STACK;
    E_top = wrap ? 0x80u - 4u * (1u + rnd() % 6) : stack_base + 0x6000u + 4u * (rnd() % 64);
    /* image globals */
    for (unsigned a = 0; a < 6; ++a) {                             /* dominant-axis projection table */
        uint16_t u = (uint16_t)((a / 2 + 1) % 3), v = (uint16_t)((a / 2 + 2) % 3);
        if (a & 1) { uint16_t t = u; u = v; v = t; }
        if (rnd() % 29 == 0) { u = (uint16_t)(int16_t)((int)(rnd() % 12) - 3); }
        w16(0x1EAF30 + 4 * a, u); w16(0x1EAF32 + 4 * a, v);
    }
    float eps = rnd() % 3 ? 0.0f : rnd() % 2 ? frand(0, 0.01f) : rnd() % 4 ? -0.0f : rnd() % 2 ? frand(-1, 0) : nanf_();
    wf(0x1F0A68, eps);
    uint32_t epoch = rnd(); w32(0x2D2FAC, epoch); w8(0x2D2FA9, (uint8_t)rnd());
    /* BSP */
    uint32_t bsp = tag_alloc(0x200, 0); w32(0x39BE58, bsp);
    int32_t ncl = 1 + (int32_t)(rnd() % 40), npl = 1 + (int32_t)(rnd() % 50), npo = (int32_t)(rnd() % 90);
    if (big) { ncl = 70 + (int32_t)(rnd() % 80); npo = 2 * ncl + (int32_t)(rnd() % (uint32_t)ncl); }   /* > 64 clusters in reach */
    if (game_like) { ncl = 3 + (int32_t)(rnd() % 4); npo = 2 * ncl; }
    uint32_t clusters = tag_alloc((uint32_t)ncl * 0x68, rnd() % 13 == 0), planes = tag_alloc((uint32_t)npl * 16, rnd() % 11 == 0);
    uint32_t planes2 = tag_alloc((uint32_t)npl * 16, 0), portals = tag_alloc((uint32_t)(npo ? npo : 1) * 64, rnd() % 17 == 0);
    w32(bsp + 0x134, (uint32_t)ncl); w32(bsp + 0x138, clusters); w32(bsp + 0x158, portals);
    uint32_t pst = tag_alloc(0x20, 0); w32(pst + 0x10, planes); w32(bsp + 0xB4, pst);
    uint32_t b3d = tag_alloc(0x20, 0); w32(0x39BE50, b3d); w32(b3d + 0x10, rnd() % 8 ? planes : planes2);
    plane *pl = malloc(sizeof(plane) * (size_t)npl);
    for (int32_t i = 0; i < npl; ++i) {
        random_plane(&pl[i], flavor);
        for (unsigned k = 0; k < 3; ++k) { wf(planes + 16u * (uint32_t)i + 4u * k, pl[i].n[k]); wf(planes2 + 16u * (uint32_t)i + 4u * k, pl[i].n[(k + 1) % 3]); }
        wf(planes + 16u * (uint32_t)i + 12, pl[i].d); wf(planes2 + 16u * (uint32_t)i + 12, -pl[i].d);
    }
    /* stamps: random, some already equal to the next epoch (visited), some equal to the current one */
    for (int32_t i = -4; i < ncl + 4; ++i) {
        uint32_t v = rnd() % 5 == 0 ? epoch + 1u : rnd() % 7 == 0 ? epoch : rnd();
        if (i != -1) w32(0x2D2FB0 + 4u * (uint32_t)i, v);
    }
    /* portals: a polygon in its plane, front/back clusters, bounding sphere */
    int16_t *front = malloc(sizeof(int16_t) * (size_t)(npo + 1)), *back = malloc(sizeof(int16_t) * (size_t)(npo + 1));
    for (int32_t p = 0; p < npo; ++p) {
        uint32_t P = portals + 64u * (uint32_t)p;
        front[p] = (int16_t)(rnd() % (uint32_t)ncl); back[p] = (int16_t)(rnd() % (uint32_t)ncl);
        if (big || game_like) { front[p] = (int16_t)(p % ncl); back[p] = (int16_t)((p % ncl + 1 + (int32_t)(rnd() % 3)) % ncl); }
        if (rnd() % 31 == 0) back[p] = -1;
        w16(P, (uint16_t)front[p]); w16(P + 2, (uint16_t)back[p]);
        uint32_t pi = rnd() % (uint32_t)npl; w32(P + 4, pi);
        const plane *q = &pl[pi];
        float c[3] = { frand(-60, 60), frand(-60, 60), frand(-60, 60) };
        float dist = q->n[0] * c[0] + q->n[1] * c[1] + q->n[2] * c[2] - q->d;
        for (unsigned k = 0; k < 3; ++k) c[k] -= dist * q->n[k];      /* onto the plane */
        float t1[3], t2[3];
        { float a[3] = { 1, 0, 0 }; if (fabsf(q->n[0]) > 0.9f) { a[0] = 0; a[1] = 1; }
          t1[0] = q->n[1] * a[2] - q->n[2] * a[1]; t1[1] = q->n[2] * a[0] - q->n[0] * a[2]; t1[2] = q->n[0] * a[1] - q->n[1] * a[0];
          float l = sqrtf(t1[0] * t1[0] + t1[1] * t1[1] + t1[2] * t1[2]); if (!(l > 0)) l = 1; for (unsigned k = 0; k < 3; ++k) t1[k] /= l;
          t2[0] = q->n[1] * t1[2] - q->n[2] * t1[1]; t2[1] = q->n[2] * t1[0] - q->n[0] * t1[2]; t2[2] = q->n[0] * t1[1] - q->n[1] * t1[0]; }
        int32_t nv = (int32_t)(rnd() % 9); if (rnd() % 41 == 0) nv = (int32_t)(rnd() % 3); if (rnd() % 97 == 0) nv = -(int32_t)(rnd() % 3);
        if (rnd() % 211 == 0) nv = 129 + (int32_t)(rnd() % 6);        /* runs over the 0x400-byte polygon buffer into the frames above */
        if (game_like) nv = 4 + (int32_t)(rnd() % 3);
        float r = frand(0.5f, 30), sgn = rnd() & 1 ? 1.0f : -1.0f;
        uint32_t verts = tag_alloc((uint32_t)(nv > 0 ? nv : 1) * 12, rnd() % 19 == 0);
        for (int32_t v = 0; v < nv; ++v) {
            float ang = sgn * 6.2831853f * (float)v / (float)(nv > 0 ? nv : 1), rr = r * frand(0.6f, 1.0f);
            if (rnd() % 23 == 0 && v > 0) { for (unsigned k = 0; k < 3; ++k) { float x; x_guest_read_pages(&x, verts + 12u * (uint32_t)(v - 1) + 4u * k, 4); wf(verts + 12u * (uint32_t)v + 4u * k, x); } continue; }
            for (unsigned k = 0; k < 3; ++k) wf(verts + 12u * (uint32_t)v + 4u * k, c[k] + rr * (cosf(ang) * t1[k] + sinf(ang) * t2[k]));
        }
        w32(P + 0x34, (uint32_t)nv); w32(P + 0x38, verts);
        for (unsigned k = 0; k < 3; ++k) wf(P + 8 + 4 * k, c[k] + (rnd() % 9 == 0 ? frand(-20, 20) : 0));
        wf(P + 0x14, rnd() % 9 == 0 ? frand(0, 5) : r * frand(1.0f, 1.5f));
        if (flavor == 2 && rnd() % 13 == 0) wf(P + 8 + 4 * (rnd() % 4), nanf_());
    }
    /* clusters: the portals that touch them (+ random extras) */
    for (int32_t k = 0; k < ncl; ++k) {
        uint32_t C = clusters + 0x68u * (uint32_t)k;
        int16_t list[160]; int32_t n = 0;
        for (int32_t p = 0; p < npo && n < 150; ++p) if (front[p] == k || back[p] == k) list[n++] = (int16_t)p;
        if (npo && rnd() % 5 == 0) list[n++] = (int16_t)(rnd() % (uint32_t)npo);
        for (int32_t i = n - 1; i > 0; --i) { int32_t j = (int32_t)(rnd() % (uint32_t)(i + 1)); int16_t t = list[i]; list[i] = list[j]; list[j] = t; }
        uint32_t arr = tag_alloc((uint32_t)(n > 0 ? n : 1) * 2, rnd() % 29 == 0);
        for (int32_t i = 0; i < n; ++i) w16(arr + 2u * (uint32_t)i, (uint16_t)list[i]);
        w32(C + 0x5C, rnd() % 61 == 0 ? (uint32_t)-(int32_t)(rnd() % 3) : (uint32_t)n); w32(C + 0x60, arr);
    }
    if (*alias) st->alias_cases++;
    /* reference structure (0x2FC670-like): [0] per-cluster heads, [4] cluster->light refs, [8] light->cluster refs */
    uint32_t ref = rnd() % 4 ? 0x2FC670u : tag_alloc(16, 0);
    uint32_t heads = tag_alloc((uint32_t)(ncl + 8) * 4, 0) + 16;
    mut_n = 0; mut_addr[mut_n] = heads - 16; mut_len[mut_n++] = (uint32_t)(ncl + 8) * 4;
    for (int32_t i = -4; i < ncl + 4; ++i) w32(heads + 4u * (uint32_t)i, rnd() % 3 ? 0xFFFFFFFFu : rnd());
    w32(ref, heads);
    for (unsigned k = 1; k <= 2; ++k) {
        uint32_t A = tag_alloc(0x38, rnd() % 23 == 0);
        int16_t max = (int16_t)(1 + rnd() % 80), size = (int16_t)(rnd() % 4 ? 12 : 8 + rnd() % 9);
        if (game_like) { max = 1024; size = 12; }
        uint32_t base = tag_alloc((uint32_t)(max + 4) * (uint32_t)(size > 12 ? size : 12) + 16, rnd() % 31 == 0);
        w16(A + 0x20, (uint16_t)max); w16(A + 0x22, (uint16_t)size); w32(A + 0x34, base);
        mut_addr[mut_n] = A; mut_len[mut_n++] = 0x38; mut_addr[mut_n] = base; mut_len[mut_n++] = (uint32_t)(max + 4) * (uint32_t)(size > 12 ? size : 12) + 16;
        int16_t hint = (int16_t)(rnd() % (uint32_t)(max + 3)); if (rnd() % 37 == 0) hint = (int16_t)-(int16_t)(rnd() % 3);
        w16(A + 0x2C, (uint16_t)hint); w16(A + 0x2E, (uint16_t)(rnd() % (uint32_t)(max + 2))); w16(A + 0x30, (uint16_t)rnd());
        w16(A + 0x32, rnd() % 11 == 0 ? 0xFFFFu : (uint16_t)(0x8000u | rnd()));
        int fill = rnd() % 4;                                          /* 0 sparse .. 3 nearly full */
        if (game_like) w16(A + 0x2C, getenv("N92_NOSCAN") ? (uint16_t)max : 0);
        for (int32_t i = 0; i < max + 4; ++i) {
            uint16_t salt = (rnd() % 4 < (unsigned)fill) ? (uint16_t)(0x8000u | rnd()) : 0;
            if (game_like) salt = rnd() % 25 ? (uint16_t)(0x8000u | rnd()) : 0;   /* ~25-slot scans (a10: ~26 per datum) */
            for (int32_t b = 0; b < (size > 12 ? size : 12); b += 2) w16(base + (uint32_t)(i * (size > 12 ? size : 12) + b), (uint16_t)rnd());
            w16(base + (uint32_t)(i * size), salt);
        }
        w32(ref + 4u * k, A);
    }
    /* light: head, position, radius, location */
    uint32_t head = tag_alloc(8, 0); w32(head, rnd() % 2 ? 0xFFFFFFFFu : rnd());
    mut_addr[mut_n] = head; mut_len[mut_n++] = 8;
    uint32_t pos = tag_alloc(12, rnd() % 7 == 0);
    for (unsigned k = 0; k < 3; ++k) wf(pos + 4 * k, flavor == 2 && rnd() % 29 == 0 ? nanf_() : frand(-60, 60));
    float radius = rnd() % 9 == 0 ? eps : rnd() % 11 == 0 ? frand(-3, 0) : rnd() % 3 ? frand(0, 40) : frand(0, 200);
    if (game_like) radius = getenv("N92_NOFLOOD") ? -1.0f : getenv("N92_FULL") ? frand(60, 90) : frand(8, 30);
    if (big || (*alias && rnd() % 2)) radius = frand(300, 600);
    if (flavor == 2 && rnd() % 7 == 0) radius = nanf_();
    uint32_t loc = tag_alloc(8, rnd() % 5 == 0);
    uint16_t start = rnd() % 13 == 0 ? 0xFFFFu : (uint16_t)(rnd() % (uint32_t)ncl);
    if (game_like) start = (uint16_t)(rnd() % (uint32_t)ncl);
    w16(loc + 4, start);
    if (*alias && npo && start != 0xFFFFu) {       /* aim portals of the start cluster at live flood-frame words */
        uint32_t C = clusters + 0x68u * start, n = r32(C + 0x5C), arr = r32(C + 0x60);
        static const uint8_t slot[] = { 4, 8, 12 };                  /* pos, bsp, cluster of the frame (S-4/-8/-C) */
        for (uint32_t t = 0; t < n && t < 64 && (int32_t)n > 0; ++t) if (rnd() % 2) {
            uint16_t pidx; x_guest_read_pages(&pidx, arr + 2 * t, 2);
            uint32_t P = portals + 64u * pidx, target = E_top - 0x98u - 0x2Cu * (rnd() % 2) - slot[rnd() % 3];
            if (rnd() % 3 == 0) target = E_top - 0x98u + 4u * (1 + rnd() % 3);   /* radius / loop index / found (S+4/+8/+C) */
            int32_t idx = (int32_t)(target - 0x2D2FB0u) / 4;
            w16(P, (uint16_t)(int16_t)idx); w16(P + 2, start);
            wf(P + 0x14, 2000.0f);                                    /* the sphere tests pass */
        }
    }
    w32(E_top, 0x925B0u); w32(E_top + 4, rnd() & 0xFFFFu); w32(E_top + 8, head); w32(E_top + 12, pos); wf(E_top + 16, radius);
    w32(E_top + 0x100, loc); w32(E_top + 0x104, ref);                  /* scratch for init_ctx */
    free(pl); free(front); free(back);
}

static void init_ctx(xctx *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = E_top; c->r[0] = r32(E_top + 0x100); c->r[7] = r32(E_top + 0x104);
    c->fsp = rnd() & 7; c->fcw = 0x027F; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = 32; c->f_cf = rnd() & 1; c->f_of = rnd() & 1;
    c->df = rnd() % 53 == 0;
    c->preempt = (int32_t)(rnd() % 200) - 20;
#ifdef XV_NATIVE_52240_TEST
    /* Reuse randomized BSPs, but enter the flood directly with its own ABI. */
    uint16_t cluster; x_guest_read_pages(&cluster, c->r[0] + 4, 2);
    if (cluster == 0xFFFFu) cluster = 0;
    c->r[1] = (c->r[1] & 0xFFFF0000u) | cluster;
    c->r[2] = r32(E_top + 12);
    uint32_t radius = r32(E_top + 16), list = tag_alloc(128, 0);
    static const uint32_t capacities[] = {0, 1, 2, 64, 0xFFFFFFFFu};
    w32(E_top + 4, radius); w32(E_top + 8, capacities[rnd() % 5]);
    w32(E_top + 12, list);
#endif
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_of_override)
    if (b->f_cf_override) F(f_cf)
    if (b->f_of_override) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* Scratch x87 slots: two NaNs are equal (which NaN payload an operation propagates is the host compiler's choice). */
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}

/* --bench-game: optional user-space instruction and cycle counts (perf_event_open; 0 when unavailable) */
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
/* Runs f_00056670 in `mode`; returns 0 on a timeout (a chaotic aliasing case that never terminates). */
static int run(xctx *c, int mode)
{
    xv_native_92330_force(mode);
    if (sigsetjmp(alarm_jmp, 1)) { xv_native_92330_force(0); return 0; }
    alarm(4);
#ifdef XV_NATIVE_52240_TEST
    if (mode) xv_native_52240_test(c); else f_00052240(c);
#else
    f_00056670(c);
#endif
    alarm(0);
    xv_native_92330_force(0);
    return 1;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    const int verify = argc > 3 && !strcmp(argv[3], "--verify");
    stats s = {0};
    signal(SIGALRM, on_alarm);
    map_memory();
    const int bench = argc > 4 && !strcmp(argv[3], "--bench") ? atoi(argv[4]) : 0;
    const int bench_game = argc > 4 && !strcmp(argv[3], "--bench-game") ? atoi(argv[4]) : 0;
    if (bench_game) {
        /* Speed on a10-like queries: every repetition restores the datum arrays and list heads (outside the timed
         * call), so each timed call allocates and links like the game; guest and native alternate per repetition. */
        game_like = 1;
        double g_ns = 0, n_ns = 0; unsigned calls = 0, floods = 0; struct timespec t0, t1;
        uint64_t g_pe[2] = { 0, 0 }, n_pe[2] = { 0, 0 }; pe_open();
        uint8_t *snap = malloc(ARENA);
        for (unsigned k = 0; k < cases; ++k) {
            int alias; scene(&s, &alias); slice = 1 << 30;
            xctx c0; init_ctx(&c0); c0.preempt = 1 << 30; c0.df = 0;
            uint32_t off = 0;
            for (unsigned i = 0; i < mut_n; ++i) { x_guest_read_pages(snap + off, mut_addr[i], mut_len[i]); off += mut_len[i]; }
            uint32_t e0 = r32(0x2D2FAC);
            for (int r = 0; r < bench_game; ++r)
                for (int mode = getenv("N92_ONLY_NATIVE") ? 2 : 0; mode <= 2; mode += 2) {
                    for (unsigned i = 0, o = 0; i < mut_n; o += mut_len[i], ++i) x_guest_write_pages(mut_addr[i], snap + o, mut_len[i]);
                    xctx c = c0; xv_native_92330_force(mode);
                    uint64_t p0[2], p1[2]; pe_read(p0);
                    clock_gettime(CLOCK_MONOTONIC, &t0);
                    f_00056670(&c);
                    clock_gettime(CLOCK_MONOTONIC, &t1);
                    pe_read(p1);
                    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    if (mode) { n_ns += ns; n_pe[0] += p1[0] - p0[0]; n_pe[1] += p1[1] - p0[1]; }
                    else { g_ns += ns; g_pe[0] += p1[0] - p0[0]; g_pe[1] += p1[1] - p0[1]; }
                }
            calls += (unsigned)bench_game; floods += r32(0x2D2FAC) != e0;
        }
        xv_native_92330_force(0);
        double z = 0;                                         /* the timer's own cost, subtracted */
        for (int i = 0; i < 20000; ++i) { clock_gettime(CLOCK_MONOTONIC, &t0); clock_gettime(CLOCK_MONOTONIC, &t1); z += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec); }
        z /= 20000;
        printf("native-92330 game-like bench: %u calls each (%u scenes flooded): guest %.1f ns/call, native %.1f ns/call, ratio %.2fx (timer %.1f ns subtracted)\n",
               calls, floods, g_ns / calls - z, n_ns / calls - z, (g_ns / calls - z) / (n_ns / calls - z), z);
        uint64_t e0[2], e1[2]; pe_read(e0); pe_read(e1);            /* the counter read's own cost */
        printf("native-92330 game-like bench: user instructions/call guest %.0f native %.0f (%.2fx), cycles/call guest %.0f native %.0f (%.2fx)%s\n",
               (double)g_pe[0] / calls, (double)n_pe[0] / calls, n_pe[0] ? (double)g_pe[0] / n_pe[0] : 0, (double)g_pe[1] / calls, (double)n_pe[1] / calls,
               n_pe[1] ? (double)g_pe[1] / n_pe[1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
        log_all = 1; extern void xv_native_92330_report(unsigned); xv_native_92330_report(0);
        return 0;
    }
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    if (bench) {
        /* Speed: the same query repeated on its own state (datum arrays made full, so every repetition does the same
         * flood and link work: the epoch advances and the stamps of the previous run are stale), guest then native. */
        struct timespec t0, t1; double g_ns = 0, n_ns = 0; unsigned calls = 0;
        for (unsigned k = 0; k < cases; ++k) {
            int alias; scene(&s, &alias); slice = 1 << 30;
            if (alias) continue;
            xctx c0; init_ctx(&c0); c0.preempt = 1 << 30; c0.df = 0;
            uint32_t ref = c0.r[7];
            for (unsigned a = 1; a <= 2; ++a) { uint32_t A = r32(ref + 4 * a); uint16_t max; x_guest_read_pages(&max, A + 0x20, 2); w16(A + 0x2C, max); }
            xctx c = c0; xv_native_92330_force(0); f_00056670(&c);   /* warm */
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int r = 0; r < bench; ++r) { c = c0; f_00056670(&c); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            g_ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
            xv_native_92330_force(2); c = c0; f_00056670(&c);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int r = 0; r < bench; ++r) { c = c0; f_00056670(&c); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            xv_native_92330_force(0);
            n_ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
            calls += (unsigned)bench;
        }
        printf("native-92330 bench: %u calls each: guest %.1f ns/call, native %.1f ns/call, ratio %.2fx\n", calls, g_ns / calls, n_ns / calls, g_ns / n_ns);
        return 0;
    }
    for (unsigned k = 0; k < cases; ++k) {
        int alias; scene(&s, &alias); slice = 5 + (int)(rnd() % 60);
        xctx c0; init_ctx(&c0);
        if (getenv("N92_FROM") && k < (unsigned)atoi(getenv("N92_FROM"))) continue;   /* debugging: replay from case N */
        memcpy(before, g_xram, ARENA);
        xctx cg = c0; preempt_calls = 0;
        int gok = run(&cg, 0);
        unsigned guest_preempts = preempt_calls;
        if (getenv("N92_GUEST_HASH")) {                    /* guest-only: one FNV hash of the arena per case */
            uint64_t h = 1469598103934665603ull;
            for (uint32_t i = 0; i < ARENA; ++i) { h ^= g_xram[i]; h *= 1099511628211ull; }
            printf("case %u %016llx\n", k, (unsigned long long)h);
            continue;
        }
        memcpy(guest, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = 0;
        int nok = run(&cn, 2);
        unsigned native_preempts = preempt_calls;
        char why[200] = "";
        s.cases++;
        if (!gok) { s.timeouts++; continue; }
        int ok = gok && nok && same_ctx(&cn, &cg, why, sizeof why) && native_preempts == guest_preempts;
        if (ok && memcmp(guest, g_xram, ARENA)) {
            /* a float NaN in both at a differing aligned word: the payload is the host compiler's operand order */
            for (uint32_t i = 0; i < ARENA; ++i) if (guest[i] != g_xram[i]) {
                uint32_t gw, nw; memcpy(&gw, guest + (i & ~3u), 4); memcpy(&nw, g_xram + (i & ~3u), 4);
                if (nan32(gw) && nan32(nw)) { s.nan_words++; i |= 3u; continue; }
                ok = 0; snprintf(why, sizeof why, "arena+%06X guest %02X native %02X", i, guest[i], g_xram[i]); break;
            }
        }
        if (!ok && !why[0]) snprintf(why, sizeof why, gok != nok ? "timeout guest %d native %d" : "xv_preempt calls native %u guest %u", gok ? native_preempts : !gok, gok ? guest_preempts : !nok);
        if (cg.r[0] == 0 && cg.f_kind == XK_SUB) s.linked++;
        uint16_t cnt; memcpy(&cnt, guest + 0x2D2FAC, 2);
        uint32_t e0, e1; memcpy(&e0, before + 0x2D2FAC, 4); memcpy(&e1, guest + 0x2D2FAC, 4); if (e0 != e1) s.flood_cases++;
        if (!ok) {
            if (++s.mismatches <= 10) {
                printf("case %u MISMATCH: %s\n", k, why);
                if (getenv("N92_DEBUG")) {                  /* guest addresses of the differing bytes */
                    unsigned shown = 0;
                    for (uint32_t i = 0; i < ARENA && shown < 24; ++i) if (guest[i] != g_xram[i]) {
                        uint32_t va = 0xFFFFFFFFu;
                        for (uint32_t p = 0; p < (1u << 20); ++p) if (g_xpt[p] == (i & ~0xFFFu)) { va = (p << 12) | (i & 0xFFFu); break; }
                        printf("   va %08X (E%+d) before %02X guest %02X native %02X\n", va, (int)(va - E_top), before[i], guest[i], g_xram[i]); shown++;
                    }
                    printf("   E %08X eax %08X/%08X ecx %08X/%08X edx %08X/%08X fsw %04X/%04X\n", E_top, cg.r[0], cn.r[0], cg.r[1], cn.r[1], cg.r[2], cn.r[2], cg.fsw, cn.fsw);
                }
            }
        }
        if (verify && gok) {
            memcpy(g_xram, before, ARENA);
            xctx cv = c0; preempt_calls = 0; log_mismatch = 0;
            int vok = run(&cv, 1);
            char vw[200] = "";
            int good = vok && !log_mismatch && same_ctx(&cv, &cg, vw, sizeof vw) && preempt_calls == guest_preempts && !memcmp(guest, g_xram, ARENA);
            if (!vok) s.verify_timeouts++;                   /* native + guest past the 4 s alarm (a chaotic overrun case) */
            else if (!good) {
                const char *what = log_mismatch ? "log" : vw[0] ? vw : preempt_calls != guest_preempts ? "xv_preempt calls" : "arena";
                if (++s.verify_mismatch <= 10) printf("case %u VERIFY: log mismatches %d, %s\n", k, log_mismatch, what);
            }
        }
    }
    extern void xv_native_92330_report(unsigned);
    log_all = 1; xv_native_92330_report(0); log_all = 0;     /* the native's own counters over all native/verify runs */
    printf("native-92330 differential: %u cases (%u flooded, %u linked, %u stack-alias scenes, %u guest timeouts skipped, %u NaN-payload words), %u mismatches%s",
           s.cases, s.flood_cases, s.linked, s.alias_cases, s.timeouts, s.nan_words, s.mismatches, verify ? "" : "\n");
    if (verify) printf(", verify-mode failures %u (%u verify runs past the alarm skipped)\n", s.verify_mismatch, s.verify_timeouts);
#ifdef XV_NATIVE_52240_TEST
    uint64_t counts[3]; xv_native_52240_test_counts(counts);
    printf("direct flood coverage: %llu calls, %llu flood entries, %llu portal tests\n",
           (unsigned long long)counts[0], (unsigned long long)counts[1], (unsigned long long)counts[2]);
    if (cases >= 300 && (counts[0] < cases || counts[1] <= counts[0] || !counts[2])) return 2;
#endif
    return s.mismatches || s.verify_mismatch;
}
