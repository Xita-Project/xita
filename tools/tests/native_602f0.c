/* Differential test: the native f_000602F0 (recomp/kernel/xk_native_606b0.c) against the lifted guest bodies of
 * f_000602F0 and its callees (f_000B1260, f_00011120, f_00061270, the CRT floor f_00019E7B with f_0001EC1F /
 * f_0001EABA / f_0001EAF7, f_0005FE80; extracted from a stage by tools/test_native_606b0.py) on randomized BSP
 * lens-flare marker clusters in a synthetic guest arena.
 * Each case runs f_000602F0 three times from the same state: guest (hook off), native (mode 2) and verify (mode 1:
 * native, restore, guest; the guest result is kept and the native logs any difference). Compared: the whole arena,
 * every xctx field, the xv_preempt and flare-barrier call counts.
 * Scenes: shuffled physical tag pages, reversed stack pages, random dead stack, random x87 slots/top (stale high
 * bits)/status/lazy flags, control words (rounding modes; precision exception unmasked -> declined untouched), 0..40
 * markers per cluster (and 0), int8 directions with zeros, ties and axis-aligned vectors (every B1260 branch, the
 * 11120 zero-length path), positions in front of / behind the camera, alpha 0, the list near and at its 1024 limit,
 * flares disabled ([2FC6C0]), unaligned esp (declined), stack windows across pages, and aliasing: definitions and
 * markers inside the stack window, a cluster word straddling into a stack page. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "xv_x86rt.h"

uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#endif
int xv_phase_enabled;
void xv_phase_begin(void *s, void *c, unsigned id) { (void)s; (void)c; (void)id; }
void xv_phase_end(void *s) { (void)s; }
static unsigned log_mismatch;
void xk_os_log(const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (strstr(b, "MISMATCH")) { if (++log_mismatch <= 5) fputs(b, stdout); }
    if (strstr(b, " frames: calls ")) fputs(b, stdout);
}
uint64_t xk_os_monotonic_us(void) { return 0; }
void xv_scene_phase_begin(uint32_t a) { (void)a; }
void xv_scene_phase_end(uint32_t a) { (void)a; }
static unsigned preempt_calls, barrier_calls; static int slice = 37;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = slice; }
void xv_flare_barrier(unsigned r) { (void)r; barrier_calls++; }
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
static int unexpected; static const char *unexpected_what;
void xv_trap(xctx *c, uint32_t eip) { (void)c; (void)eip; unexpected = 1; unexpected_what = "trap"; }
void xv_unimpl(xctx *c, uint32_t eip, const char *what) { (void)c; (void)eip; unexpected = 1; unexpected_what = what; }
void xv_call(xctx *c, uint32_t target) { (void)target; unexpected = 1; unexpected_what = "xv_call"; c->r[4] += 4; }
/* the runtime's string instructions (xv_x86rt.c): element-wise, every element through its own guest address */
void x_str_movs(xctx *c, unsigned sz, int mode)
{
    do {
        if (mode != X_STR_ONCE && !c->r[1]) break;
        uint32_t v = 0; x_guest_read_pages(&v, c->r[6], sz); x_guest_write_pages(c->r[7], &v, sz);
        c->r[6] += c->df ? -sz : sz; c->r[7] += c->df ? -sz : sz;
        if (mode != X_STR_ONCE) c->r[1]--;
    } while (mode != X_STR_ONCE);
}
void x_str_stos(xctx *c, unsigned sz, int mode)
{
    do {
        if (mode != X_STR_ONCE && !c->r[1]) break;
        uint32_t v = c->r[0]; x_guest_write_pages(c->r[7], &v, sz);
        c->r[7] += c->df ? -sz : sz;
        if (mode != X_STR_ONCE) c->r[1]--;
    } while (mode != X_STR_ONCE);
}
void f_0001E9C7(xctx *c) { unexpected = 1; unexpected_what = "f_0001E9C7 (CRT floor error path)"; c->r[4] += 4; }
void f_0001EA1A(xctx *c) { unexpected = 1; unexpected_what = "f_0001EA1A (CRT floor exception path)"; c->r[4] += 4; }

extern void f_000602F0(xctx *);
extern int xv_native_602f0(xctx *);
extern void xv_native_602f0_force(int);
extern void xv_native_606b0_report(unsigned);

enum { ARENA = 8u << 20, IMAGE_PAGES = 0x400, TAG = 0x40000000u, TAG_PAGES = 512, STACK = 0xD0000000u, STACK_PAGES = 4 };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }

static void map_memory(void)
{
    g_xram = calloc(1, ARENA); g_xpt = malloc(sizeof(uint32_t) << 20);
    trash_off = ARENA - 4096; for (uint32_t i = 0; i < (1u << 20); ++i) g_xpt[i] = trash_off;
    for (uint32_t i = 0; i < IMAGE_PAGES; ++i) g_xpt[i] = i << 12;
    g_img_base = g_xram; next_free = IMAGE_PAGES << 12;
    uint32_t perm[TAG_PAGES]; for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = next_free + (perm[i] << 12);
    next_free += TAG_PAGES << 12;
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = next_free + ((STACK_PAGES - 1 - i) << 12);
    next_free += STACK_PAGES << 12;
    if (next_free > trash_off) { fprintf(stderr, "arena too small\n"); exit(2); }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
}
static uint32_t tag_top;
static uint32_t tag_alloc(uint32_t bytes, uint32_t align)
{
    uint32_t a = (tag_top + align - 1) & ~(align - 1);
    tag_top = a + bytes; if (tag_top > TAG + (TAG_PAGES << 12) - 64) { fprintf(stderr, "tag space exhausted\n"); exit(2); }
    return a;
}
static uint32_t tag_page_below_stack(void)
{
    for (uint32_t i = 0; i < TAG_PAGES; ++i) if (g_xpt[(TAG >> 12) + i] == ((IMAGE_PAGES + TAG_PAGES - 1) << 12)) return TAG + (i << 12);
    return 0;
}
static void image_constants(void)
{
    static const struct { uint32_t a, v; } k[] = {
        { 0x1F0A68, 0x00000000 }, { 0x1F0A70, 0x00000000 }, { 0x1F0A74, 0x3FF00000 }, { 0x1F0A78, 0x3F800000 },
        { 0x1F0ABC, 0xBF800000 }, { 0x1F0AF8, 0xE0000000 }, { 0x1F0AFC, 0x3F1A36E2 }, { 0x1F0B14, 0x3C010204 },
        { 0x1F0C30, 0x43FFC000 }, { 0x1F0C34, 0x447FE000 }, { 0x1F2840, 0x0000173F },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) w32(k[i].a, k[i].v);
    if (rnd() % 4 == 0) w32(0x1F2840, 0x0000033Fu | ((rnd() & 3u) << 10) | (rnd() & 0x1000u));   /* the floor's control word, any RC */
}
typedef struct { int nmarkers, alias, straddle, declines; } scen;

static void scene(xctx *c, scen *sc)
{
    memset(g_xram, 0, ARENA);
    image_constants();
    tag_top = TAG + (rnd() % 64) * 4;
    const int straddle = sc->straddle = rnd() % 13 == 0 && tag_page_below_stack() >= TAG + 0x8000u;
    for (uint32_t a = STACK; a < STACK + STACK_PAGES * 4096u; a += 4) w32(a, rnd());
    uint32_t E0 = (STACK + 0x200 + (rnd() % (STACK_PAGES * 4096u - 0x300))) & ~3u;
    if (straddle) E0 = STACK + 0x3000u + 0xB0u;                   /* the window starts at guest stack page 3 = physical page 0 */
    else if (rnd() % 11 == 0) E0 = STACK + 0x1000u + 4u * (rnd() % 44);   /* the window across a page boundary */
    sc->declines = 0;
    if (rnd() % 61 == 0) { E0 |= 1 + rnd() % 3; sc->declines = 1; }
    const uint32_t S = E0 - 0x5Cu;
    /* camera and view */
    w16(0x2FC6C0, rnd() % 31 == 0 ? 1 : 0); w8(0x2FEB8A, (uint8_t)rnd());
    for (unsigned i = 0; i < 3; ++i) wf(0x2FC6C8 + 4 * i, frand(-30, 30));
    { float fx = frand(-1, 1), fy = frand(-1, 1), fz = frand(-1, 1), n = sqrtf(fx * fx + fy * fy + fz * fz) + 1e-3f;
      wf(0x2FC6D4, fx / n); wf(0x2FC6D8, fy / n); wf(0x2FC6DC, fz / n); }
    /* the flare list */
    uint32_t count = rnd() % 200; if (rnd() % 9 == 0) count = 1000 + rnd() % 30; if (rnd() % 23 == 0) count = 1024;
    w32(0x2E34E0, count); w8(0x2E34E4, (uint8_t)(rnd() % 2));
    for (uint32_t a = 0x2C76D0; a < 0x2C76D0 + 40u * 1024u; a += 4) w32(a, rnd());
    /* tag instances with flare definitions ([def+1Ch]: the near distance) */
    const uint32_t tags = tag_alloc(32u * 16u, 4); w32(0x39CE24, tags);
    for (unsigned t = 0; t < 16; ++t) {
        uint32_t def = tag_alloc(0x40, 4);
        if (sc->alias == 1 && t == 0) def = S - 0x1Cu + 4u * (rnd() % 20);   /* [def+1Ch] inside the stack window */
        else { for (uint32_t o = 0; o < 0x40; o += 4) w32(def + o, rnd()); wf(def + 0x1C, rnd() % 5 == 0 ? 0.0f : rnd() % 4 == 0 ? -frand(0, 5) : frand(0, 40)); }
        w32(tags + 32u * t + 0x14, def);
    }
    /* the BSP: clusters (0x68), markers (16), lens flares (16) */
    const uint32_t bsp = tag_alloc(0x140, 4); w32(0x39BE58, bsp);
    for (uint32_t o = 0; o < 0x140; o += 4) w32(bsp + o, rnd());
    const unsigned nclusters = 8;
    uint32_t clusters = tag_alloc(0x68u * nclusters, rnd() % 13 == 0 ? 2 : 4);
    int32_t idx = (int32_t)(rnd() % nclusters);
    if (straddle) { clusters = tag_page_below_stack() + 0xFFFu - 0x42u - 0x68u * (uint32_t)idx; }   /* [cluster+42h] word at FFFh */
    w32(bsp + 0x138, clusters);
    const unsigned nmarkers = 64, nflares = 8;
    const uint32_t markers = tag_alloc(16u * nmarkers, 4), lflares = tag_alloc(16u * nflares, 4);
    w32(bsp + 0x12C, sc->alias == 2 ? S + 0x1Cu - 16u * (rnd() % 3) - 12u : markers);   /* aliasing: markers read from the stack window */
    w32(bsp + 0x120, lflares);
    for (unsigned f = 0; f < nflares; ++f) { for (uint32_t o = 0; o < 16; o += 4) w32(lflares + 16 * f + o, rnd()); w32(lflares + 16 * f + 0xC, (rnd() & 0xFFFF0000u) | (rnd() % 16)); }
    for (unsigned k = 0; k < nmarkers; ++k) {
        uint32_t mk = markers + 16u * k;
        for (unsigned i = 0; i < 3; ++i) wf(mk + 4 * i, frand(-60, 60));
        int8_t d[3];
        switch (rnd() % 8) {
        case 0: d[0] = d[1] = d[2] = 0; break;                                            /* zero direction */
        case 1: { int8_t v = (int8_t)((int)(rnd() % 255) - 127); d[0] = d[1] = d[2] = v; if (rnd() & 1) d[rnd() % 3] = (int8_t)-v; } break;   /* ties */
        case 2: d[0] = d[1] = d[2] = 0; d[rnd() % 3] = (int8_t)(rnd() & 1 ? 127 : -127); break;   /* axis-aligned */
        default: for (unsigned i = 0; i < 3; ++i) d[i] = (int8_t)((int)(rnd() % 255) - 127);
        }
        w8(mk + 0xC, (uint8_t)d[0]); w8(mk + 0xD, (uint8_t)d[1]); w8(mk + 0xE, (uint8_t)d[2]);
        w8(mk + 0xF, (uint8_t)(rnd() % nflares));
    }
    int32_t n = (int32_t)(rnd() % 41); if (rnd() % 17 == 0) n = 0;
    sc->nmarkers = n;
    for (unsigned k = 0; k < nclusters; ++k) {
        uint32_t cl = clusters + 0x68u * k;
        if (straddle && (int32_t)k == idx) continue;
        for (uint32_t o = 0; o < 0x68; o += 2) w16(cl + o, (uint16_t)rnd());
        w16(cl + 0x40, (uint16_t)(rnd() % (nmarkers - 40)));
        w16(cl + 0x42, (uint16_t)n);
    }
    if (straddle) {                     /* the count word: low byte in the tag page, high byte from the host-adjacent page =
                                         * the window's first byte; a page-split read would take the next guest page's byte */
        uint32_t cl = clusters + 0x68u * (uint32_t)idx;
        w16(cl + 0x40, (uint16_t)(rnd() % 16));
        w8(cl + 0x42, (uint8_t)n); w8(STACK + 0x3000u, 0); w8(cl + 0x43, 1);
    }
    /* the call */
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[1] = (c->r[1] & 0xFFFF0000u) | (uint32_t)idx;
    c->r[4] = E0; w32(E0 & ~3u, 0x92A00u);
    c->fsp = rnd() & 7; if (rnd() % 7 == 0) c->fsp |= 8u << (rnd() % 3);
    static const uint16_t cws[] = { 0x027F, 0x027F, 0x027F, 0x037F, 0x067F, 0x0A7F, 0x0E7F, 0x025F };
    c->fcw = cws[rnd() % 8]; if (!(c->fcw & 0x20)) sc->declines = 1;
    c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = (rnd() & 1) ? 32 : ((rnd() & 1) ? 16 : 8);
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->df = 0;
    c->preempt = (int32_t)(rnd() % 200) - 20;
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_cf) F(f_of_override) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* x87 slots: two NaNs are equal (aliased-garbage markers can give NaN positions; which operand's NaN a two-NaN
     * operation keeps is the host compiler's operand order - the guest built -O0 and -O2 already disagrees) */
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}
static int same_arena(const uint8_t *a, const uint8_t *b, char *why, size_t n)
{
    if (!memcmp(a, b, ARENA)) return 1;
    for (uint32_t i = 0; i < ARENA; i += 4) {
        uint32_t x, y; memcpy(&x, a + i, 4); memcpy(&y, b + i, 4);
        if (x != y) { snprintf(why, n, "arena+%06X %08X vs %08X", i, x, y); return 0; }
    }
    return 1;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 2000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    const int bench = argc > 3 && !strcmp(argv[3], "bench");
    map_memory();
    {   /* the native's libm-free x87 rounding against libm (every mode; halves, zeros, signs, large, NaN/inf) */
        extern double xv_native_606b0_round(uint16_t, double);
        unsigned bad = 0, n = 0;
        for (unsigned k = 0; k < 400000; ++k) {
            double v;
            switch (k % 8) {
            case 0: v = (double)((int32_t)rnd() % 4096) * 0.5; break;                      /* halves and integers */
            case 1: v = (double)((int32_t)rnd() % 2048) + (k & 16 ? 0.5 : -0.5); break;
            case 2: v = ((double)(int32_t)rnd()) / (double)(1u + rnd() % 1000); break;
            case 3: v = ldexp((double)(int32_t)rnd(), -(int)(rnd() % 60)); break;          /* tiny fractions, -0.0 via 0 */
            case 4: v = (k & 32) ? -0.0 : 0.0; if (k & 64) v = (k & 32) ? -1e-300 : 1e-300; break;
            case 5: v = ldexp((double)(int32_t)rnd(), (int)(rnd() % 40)); break;           /* up to and past 2^31 */
            case 6: v = (k & 32) ? 2147483647.5 : -2147483648.5; if (k & 64) v = (k & 32) ? 2147483648.0 : -2147483648.0; break;
            default: v = (k & 32) ? NAN : ((k & 64) ? INFINITY : -INFINITY);
            }
            for (unsigned rc = 0; rc < 4; ++rc) {
                const uint16_t fcw = (uint16_t)(0x027Fu | (rc << 10));
                const double a = xv_native_606b0_round(fcw, v), b = x87_round(&(xctx){ .fcw = fcw }, v);
                ++n;
                if (memcmp(&a, &b, 8) && !(isnan(a) && isnan(b))) { if (++bad <= 5) printf("round rc %u %.17g: native %.17g libm %.17g\n", rc, v, a, b); }
            }
        }
        printf("rounding self-test: %u values x modes, %u differences\n", n, bad);
        if (bad) return 1;
    }
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    unsigned mismatches = 0, unexp = 0, markers = 0, verify_bad = 0, declined = 0, decline_changed = 0;
    for (unsigned k = 0; k < cases; ++k) {
        xctx c0; scen sc = { 0, (int)(rnd() % 9), 0, 0 }; scene(&c0, &sc); slice = 5 + rnd() % 60;
        memcpy(before, g_xram, ARENA);
        char why[240] = "";
        /* guest */
        xctx cg = c0; preempt_calls = barrier_calls = 0; unexpected = 0;
        xv_native_602f0_force(0); f_000602F0(&cg);
        const unsigned g_pre = preempt_calls, g_bar = barrier_calls, g_unexp = unexpected;
        memcpy(guest, g_xram, ARENA);
        /* native: a decline must leave everything untouched */
        memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = barrier_calls = 0; unexpected = 0;
        xv_native_602f0_force(2);
        const int handled = xv_native_602f0(&cn);
        xv_native_602f0_force(0);
        int ok = 1;
        if (!handled) {
            declined++;
            if (memcmp(g_xram, before, ARENA) || memcmp(&cn, &c0, sizeof cn)) { decline_changed++; ok = 0; snprintf(why, sizeof why, "decline changed state"); }
            if (!sc.declines) { ok = 0; snprintf(why, sizeof why, "unexpected decline"); }
        } else {
            ok = same_ctx(&cn, &cg, why, sizeof why);
            if (ok && (preempt_calls != g_pre || barrier_calls != g_bar)) { ok = 0; snprintf(why, sizeof why, "xv_preempt %u vs %u, barrier %u vs %u", preempt_calls, g_pre, barrier_calls, g_bar); }
            if (ok && !same_arena(g_xram, guest, why, sizeof why)) ok = 0;
            if (sc.declines) { ok = 0; snprintf(why, sizeof why, "ran where it must decline"); }
        }
        if (g_unexp && handled) { unexp++; ok = 0; snprintf(why, sizeof why, "unexpected guest path: %s", unexpected_what); }   /* (a declined
                                                                   * call may reach the CRT exception path: precision exception unmasked) */
        if (!ok && ++mismatches <= 10) printf("case %u (%d markers, alias %d, straddle %d) native MISMATCH: %s\n", k, sc.nmarkers, sc.alias, sc.straddle, why);
        /* verify */
        memcpy(g_xram, before, ARENA);
        xctx cv = c0; preempt_calls = barrier_calls = 0; unexpected = 0; log_mismatch = 0;
        xv_native_602f0_force(1);
        if (!xv_native_602f0(&cv)) f_000602F0(&cv);
        xv_native_602f0_force(0);
        int vok = same_ctx(&cv, &cg, why, sizeof why) && preempt_calls == g_pre && same_arena(g_xram, guest, why, sizeof why) && !log_mismatch;
        if (!vok) { verify_bad++; if (++mismatches <= 10) printf("case %u (%d markers) VERIFY MISMATCH (logged %u): %s\n", k, sc.nmarkers, log_mismatch, why); }
        if (sc.nmarkers > 0) markers += (unsigned)sc.nmarkers;
    }
    printf("native-602f0 differential: %u cases, %u markers, %u declined (%u changed state), %u guest-unexpected, %u verify-mode failures, %u mismatches\n",
           cases, markers, declined, decline_changed, unexp, verify_bad, mismatches);
    xv_native_606b0_report(cases);
    if (bench) {
        xctx c0; scen sc;
        do { sc = (scen){ 0, 0, 0, 0 }; scene(&c0, &sc); } while (sc.nmarkers < 30 || sc.declines || sc.straddle || (*(uint16_t *)X_G(0x2FC6C0) & 0xFFFF));
        memcpy(before, g_xram, ARENA);
        struct timespec t0, t1; double tg = 0, tn = 0; const unsigned reps = 20000;
        for (int pass = 0; pass < 2; ++pass) {
            xv_native_602f0_force(pass ? 2 : 0);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (unsigned i = 0; i < reps; ++i) { xctx cc = c0; w32(0x2E34E0, 100); if (!xv_native_602f0(&cc)) f_000602F0(&cc); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / reps;
            if (pass) tn = us; else tg = us;
        }
        xv_native_602f0_force(0);
        printf("bench: %d markers, us/call guest %.2f native %.2f (%.2fx)\n", sc.nmarkers, tg, tn, tg / tn);
    }
    return mismatches != 0;
}
