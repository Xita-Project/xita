/* Differential test: native f_00052E10 (recomp/kernel/xk_native_visibility.c) against the lifted guest bodies of
 * f_00052E10 + f_0005C300 (extracted from a stage's shards by tools/test_native_visibility.py), on randomized BSP
 * visibility scenes in a synthetic guest arena: shuffled physical pages, random visible-entry lists and frustums,
 * shared/duplicate/unaligned surface lists, count caps, preset bitmaps, equal-to-epsilon planes, NaN and negative
 * counts, pvs/current-cluster switches, stray (out-of-bitmap) surface indices, a small back-edge slice.
 * Each case runs the guest body (hook off) and the native (hook on) from identical state and compares the whole
 * arena, every xctx field except the unobservable f_cf/f_of cells, and the xv_preempt call count. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "xv_x86rt.h"

uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#endif
int xv_phase_enabled;
void xv_phase_begin(void *s, void *c, unsigned id) { (void)s; (void)c; (void)id; }
void xv_phase_end(void *s) { (void)s; }
int xv_math_bounds(xctx *c) { (void)c; return 0; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
uint64_t xk_os_monotonic_us(void) { return 0; }
static unsigned preempt_calls; static int slice = 37;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = slice; }
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
extern void f_00052E10(xctx *);
extern int xv_native_visibility(xctx *);

enum { ARENA = 24u << 20, IMAGE_PAGES = 0x400, TAG = 0x40000000u, TAG_PAGES = 512, STACK = 0xD0000000u, STACK_PAGES = 4 };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
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
static void frustum(uint32_t f, int flavor)
{
    for (unsigned p = 0; p < 4; ++p) {
        float nx = frand(-1, 1), ny = frand(-1, 1), nz = frand(-1, 1), d = frand(-60, 60);
        if (flavor == 1) { nx = ny = nz = 0; if (p & 1) nx = 1; else ny = -1; d = (float)(int)frand(-20, 20); }   /* axis planes: ties */
        if (flavor == 2 && (rnd() & 7) == 0) { if (rnd() & 1) nx = nanf_(); else d = nanf_(); }
        if (flavor == 3) {        /* one live plane of powers of two: 2^60 products cancel, the summation order decides the sign */
            static unsigned live; if (p == 0) live = rnd() % 4;
            if (p != live) { nx = ny = nz = 0; d = 1; }
            else {
                float v[3]; for (unsigned i = 0; i < 3; ++i) v[i] = (rnd() & 1 ? -1.0f : 1.0f) * (rnd() & 1 ? 1073741824.0f : 1.0f);
                nx = v[0]; ny = v[1]; nz = v[2]; d = (rnd() & 1 ? -0.5f : 0.5f) * (float)(1 + rnd() % 2);
            }
        }
        wf(f + 0x78 + 16 * p, nx); wf(f + 0x7C + 16 * p, ny); wf(f + 0x80 + 16 * p, nz); wf(f + 0x84 + 16 * p, d);
    }
    float lo[3], hi[3];
    for (unsigned i = 0; i < 3; ++i) { float c = frand(-50, 50), h = frand(0, 120); lo[i] = c - h; hi[i] = c + h; if (flavor == 1) { lo[i] = (float)(int)lo[i]; hi[i] = (float)(int)hi[i]; }
                                       if (flavor == 3) { lo[i] = -8589934592.0f; hi[i] = 8589934592.0f; } }
    if (flavor == 2 && (rnd() & 3) == 0) lo[rnd() % 3] = nanf_();
    for (unsigned i = 0; i < 3; ++i) { wf(f + 0x128 + 8 * i, lo[i]); wf(f + 0x12C + 8 * i, hi[i]); }
}

typedef struct { unsigned cases, guest_calls, native_calls, visible_cases, capped, oob_cases, mismatches; } stats;

static void scene(void)
{
    memset(g_xram, 0, ARENA);                          /* the trash page too */
    tag_top = TAG + (rnd() % 64) * 4;
    int flavor = rnd() % 4 == 0 ? 1 : rnd() % 8 == 0 ? 2 : rnd() % 5 == 0 ? 3 : 0;
    uint32_t surfaces = 1 + rnd() % 20000, clusters_n = 1 + rnd() % 40;
    uint32_t root = tag_alloc(0x140, 0), clusters = tag_alloc(clusters_n * 0x68, 0);
    w32(root + 0xF8, surfaces); w32(root + 0x134, clusters_n); w32(root + 0x138, clusters);
    uint32_t shared_list = 0, shared_n = 0;
    for (uint32_t k = 0; k < clusters_n; ++k) {
        int32_t subs = (int32_t)(rnd() % 9); if (rnd() % 23 == 0) subs = -(int32_t)(rnd() % 3);
        uint32_t arr = tag_alloc((uint32_t)(subs > 0 ? subs : 1) * 0x24, rnd() % 29 == 0);
        w32(clusters + k * 0x68 + 0x34, (uint32_t)subs); w32(clusters + k * 0x68 + 0x38, arr);
        for (int32_t j = 0; j < subs; ++j) {
            uint32_t s = arr + (uint32_t)j * 0x24;
            for (unsigned i = 0; i < 3; ++i) {
                float c = frand(-80, 80), h = frand(0.5f, 25); if (flavor == 1) { c = (float)(int)c; h = (float)(int)h; }
                if (flavor == 3) { c = (rnd() & 1 ? -1.0f : 1.0f) * (rnd() & 1 ? 1073741824.0f : 1.5f); h = 0; }   /* a point: all corners agree */
                wf(s + 8 * i, c - h); wf(s + 4 + 8 * i, c + h);
            }
            if (flavor == 2 && rnd() % 5 == 0) wf(s + 4 * (rnd() % 6), nanf_());
            int32_t n = (int32_t)(rnd() % 80); if (rnd() % 17 == 0) n = -(int32_t)(rnd() % 4);
            uint32_t list;
            if (shared_list && rnd() % 4 == 0) { list = shared_list; if (n > (int32_t)shared_n) n = (int32_t)shared_n; }
            else {
                list = tag_alloc((uint32_t)(n > 0 ? n : 1) * 4, rnd() % 31 == 0);
                uint32_t base = rnd() % surfaces;
                for (int32_t q = 0; q < n; ++q) {
                    uint32_t idx = rnd() % 3 ? (base + rnd() % 64) % surfaces : rnd() % surfaces;
                    if (rnd() % 997 == 0) idx = 0x400000u + rnd() % 32;            /* lands on the count word */
                    else if (rnd() % 1499 == 0) idx = (uint32_t)-(int32_t)(32 * (1 + rnd() % 64));   /* the entry table / view count */
                    w32(list + 4u * (uint32_t)q, idx);
                }
                if (n > 0) { shared_list = list; shared_n = (uint32_t)n; }
            }
            w32(s + 0x18, (uint32_t)n); w32(s + 0x1C, list);
        }
    }
    int32_t views = (int32_t)(rnd() % 129); if (rnd() % 19 == 0) views = -(int32_t)(rnd() % 3); if (rnd() % 7 == 0) views = (int32_t)(rnd() % 4);
    w16(0x30BE0C, (uint16_t)views);
    for (int32_t i = 0; i < (views > 0 ? views : 0); ++i) {
        uint32_t e = 0x2FEE0C + (uint32_t)i * 0x1A0;
        int16_t idx = (int16_t)(rnd() % clusters_n); if (rnd() % 97 == 0) idx = (int16_t)-(int16_t)(1 + rnd() % 2);
        w16(e, (uint16_t)idx); frustum(e + 0x14, flavor);
    }
    frustum(0x2FEBE4, flavor);
    *(uint8_t *)(g_xram + 0x39CC15) = rnd() % 5 == 0;
    w32(0x2FEDC4, rnd() % 5 == 0 ? 0xFFFFFFFFu : rnd() % clusters_n);
    wf(0x1F0A68, rnd() % 4 ? 0.0f : rnd() % 2 ? frand(-2, 2) : (rnd() % 8 == 0 ? nanf_() : -0.0f));
    uint16_t count = rnd() % 3 ? 0 : (uint16_t)(0x4000 - rnd() % 300); if (rnd() % 29 == 0) count = (uint16_t)(0x4000 + rnd() % 4); if (rnd() % 41 == 0) count = 0x8000;
    w16(0x38BE10, count);
    uint32_t words = (surfaces + 31) / 32;
    if (rnd() % 4 == 0) for (uint32_t i = 0; i < words; ++i) w32(0x30BE10 + 4 * i, rnd() & rnd() & rnd());
    w32(STACK + 0x2000 + 4, root);
}

static void init_ctx(xctx *c)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = STACK + 0x2000; w32(c->r[4], 0x53AF7u);
    c->fsp = rnd() & 7; c->fcw = 0x027F; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = 32; c->f_cf = rnd() & 1; c->f_of = rnd() & 1;
    c->preempt = (int32_t)(rnd() % 200) - 20;
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_of_override)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* Dead x87 slots: two NaNs are equal. Which operand's NaN payload an addition propagates is the host compiler's
     * choice (GCC commutes a+b freely; the guest body built -O0 and -O2 already differs), never guest-visible. */
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    map_memory();
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    stats s = {0};
    for (unsigned k = 0; k < cases; ++k) {
        scene(); slice = 5 + rnd() % 60;
        xctx c0; init_ctx(&c0);
        memcpy(before, g_xram, ARENA);
        xctx cg = c0; preempt_calls = 0;
        setenv("XV_NATIVE_VISIBILITY", "0", 1);
        f_00052E10(&cg);                 /* hook reads the env once: the first case fixes the mode below */
        unsigned guest_preempts = preempt_calls;
        memcpy(guest, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = 0;
        extern void xv_native_visibility_force(int);
        xv_native_visibility_force(2);
        f_00052E10(&cn);
        xv_native_visibility_force(0);
        unsigned native_preempts = preempt_calls;
        char why[160] = "";
        int ok = same_ctx(&cn, &cg, why, sizeof why) && native_preempts == guest_preempts;
        if (ok && memcmp(guest, g_xram, ARENA)) {
            ok = 0; for (uint32_t i = 0; i < ARENA; ++i) if (guest[i] != g_xram[i]) { snprintf(why, sizeof why, "arena+%06X guest %02X native %02X", i, guest[i], g_xram[i]); break; }
        }
        if (!ok && !why[0]) snprintf(why, sizeof why, "xv_preempt calls native %u guest %u", native_preempts, guest_preempts);
        s.cases++;
        uint16_t cnt; memcpy(&cnt, guest + 0x38BE10, 2); if ((int16_t)cnt >= 0x4000) s.capped++;
        if (cg.r[0]) s.visible_cases++;
        if (!ok) { if (++s.mismatches <= 10) printf("case %u MISMATCH: %s\n", k, why); }
    }
    printf("native-visibility differential: %u cases, %u reached the cap, %u mismatches\n", s.cases, s.capped, s.mismatches);
    return s.mismatches != 0;
}
