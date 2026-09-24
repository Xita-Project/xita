/* Differential test: the native per-flare region of f_000606B0 (recomp/kernel/xk_native_606b0.c) against the lifted
 * guest bodies (f_000606B0 with its hooks, f_00060E90, f_0005FE30, f_00060000, f_00011120, f_00061560, f_00011B60;
 * extracted from a stage by tools/test_native_606b0.py) on randomized flare lists in a synthetic guest arena.
 * The draw-path callees with D3D work (64070, 173F20, 1260F0, 64480, 61FE0, 7F210, 63E80, 71D00) are stubs with the
 * real stack conventions that log every call (registers, st0, stack arguments) into guest memory and return
 * deterministic values (64480 sometimes rejects the reflection), so the heavy path runs between native regions and
 * re-enters them at L_00060AC0 / L_00060DA6.
 * Each case runs the whole f_000606B0 three times from the same state: guest (hooks off), native (mode 2) and verify
 * (mode 1: native then guest per region, compared at the exit probes, guest kept). Compared: the whole arena, every
 * xctx field, the xv_preempt and flare-barrier call counts; verify must equal the guest and log no MISMATCH.
 * Scenes: shuffled physical tag pages, reversed stack pages, random dead stack, random x87 slots/top (stale high
 * bits)/status and lazy flags, 0..48 flares with every rejection (stage, area, alpha, reflection count, brightness,
 * distance fade <= 0 / NaN / inf), all rotation modes (1..4, 0 and out of range), brightness bytes on both paths,
 * 0..8 reflections with fade indices inside and outside 0..3, NaN/inf/zero data, and aliasing: flare rows and data
 * inside the stack window, unaligned records whose single-translation reads straddle into a stack page. */
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
static unsigned log_mismatch, log_nan;
void xk_os_log(const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (strstr(b, "MISMATCH")) { if (++log_mismatch <= 5) fputs(b, stdout); }
    if (strstr(b, "NAN-ONLY")) log_nan++;
    if (strstr(b, " frames: calls ")) fputs(b, stdout);                  /* the native's own counters (end of the run) */
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
void x_str_movs(xctx *c, unsigned sz, int mode) { (void)c; (void)sz; (void)mode; unexpected = 1; unexpected_what = "movs"; }
void x_str_stos(xctx *c, unsigned sz, int mode) { (void)c; (void)sz; (void)mode; unexpected = 1; unexpected_what = "stos"; }

extern void f_000606B0(xctx *);
extern void xv_native_606b0_force(int);
extern void xv_native_606b0_report(unsigned);

enum { ARENA = 8u << 20, IMAGE_PAGES = 0x400, TAG = 0x40000000u, TAG_PAGES = 512, STACK = 0xD0000000u, STACK_PAGES = 4,
       LOG_BASE = 0x3E0000u, LOG_TOP = 0x3DFFFCu };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
static uint32_t r32(uint32_t a) { uint32_t v; x_guest_read_pages(&v, a, 4); return v; }
static float nanf_(void) { uint32_t b = 0x7FC00000u | (rnd() & 0x3FFFFF); if (rnd() & 1) b |= 0x80000000u; float f; memcpy(&f, &b, 4); return f; }

/* ---- the draw-path stubs: log (id, registers, st0, stack args) and return deterministic values ---- */
static void stub_log(xctx *c, uint32_t id, unsigned nargs)
{
    uint32_t n = r32(LOG_TOP), a = LOG_BASE + (n % 900u) * 72u;
    w32(LOG_TOP, n + 1);
    w32(a, id); for (unsigned i = 0; i < 8; ++i) w32(a + 4 + 4 * i, c->r[i]);
    uint64_t st0; memcpy(&st0, &c->st[c->fsp & 7u], 8); w32(a + 36, (uint32_t)st0); w32(a + 40, (uint32_t)(st0 >> 32));
    w32(a + 44, c->fsp); w32(a + 48, c->fsw);
    for (unsigned i = 0; i < nargs && i < 4; ++i) w32(a + 52 + 4 * i, r32(c->r[4] + 4 + 4 * i));
}
void f_00064070(xctx *c) { stub_log(c, 0x64070, 1); c->r[0] = 0x6407u ^ r32(LOG_TOP); c->r[4] += 8; }
void f_00173F20(xctx *c) { stub_log(c, 0x173F20, 2); x87_push(c, (double)(r32(LOG_TOP) % 7u) * 0.25); c->r[0] = 0x173F; c->r[4] += 12; }
void f_001260F0(xctx *c)
{
    stub_log(c, 0x1260F0, 2);
    uint32_t out = r32(c->r[4] + 4), k = r32(LOG_TOP);
    for (unsigned i = 0; i < 4; ++i) wf(out + 4 * i, (float)((k + i) % 5u) * 0.25f);
    c->r[0] = out; c->r[4] += 12;
}
void f_00064480(xctx *c) { stub_log(c, 0x64480, 2); c->r[0] = (r32(LOG_TOP) % 3u) == 0; c->r[1] = 0x6448; c->r[2] = 0x6449; c->r[4] += 4; }
void f_00061FE0(xctx *c) { stub_log(c, 0x61FE0, 1); c->r[0] = r32(c->r[4] + 4); c->r[4] += 8; }
void f_0007F210(xctx *c) { stub_log(c, 0x7F210, 0); c->r[0] = 0x7F21; c->r[1] = 0x7F22; c->r[2] = 0x7F23; c->r[4] += 4; }
void f_00063E80(xctx *c) { stub_log(c, 0x63E80, 4); c->r[0] = 0x63E8; c->r[1] = 0x63E9; c->r[2] = 0x63EA; c->r[4] += 0x14; }
void f_00071D00(xctx *c) { stub_log(c, 0x71D00, 0); c->r[0] = 0x71D0; c->r[1] = 0x71D1; c->r[2] = 0x71D2; c->r[4] += 4; }
void f_000602F0(xctx *c) { unexpected = 1; unexpected_what = "f_000602F0 (not part of this test)"; c->r[4] += 4; }

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
static uint32_t tag_alloc(uint32_t bytes, uint32_t align)
{
    uint32_t a = (tag_top + align - 1) & ~(align - 1);
    tag_top = a + bytes; if (tag_top > TAG + (TAG_PAGES << 12) - 64) { fprintf(stderr, "tag space exhausted\n"); exit(2); }
    return a;
}
/* the guest page whose physical page is the one right below the stack's first physical page */
static uint32_t tag_page_below_stack(void)
{
    for (uint32_t i = 0; i < TAG_PAGES; ++i) if (g_xpt[(TAG >> 12) + i] == ((IMAGE_PAGES + TAG_PAGES - 1) << 12)) return TAG + (i << 12);
    return 0;
}
static void image_constants(void)
{
    static const struct { uint32_t a, v; } k[] = {
        { 0x1F0A68, 0x00000000 }, { 0x1F0A70, 0x00000000 }, { 0x1F0A74, 0x3FF00000 }, { 0x1F0A78, 0x3F800000 },
        { 0x1F0A80, 0x42652EE1 }, { 0x1F0A98, 0x3B808081 }, { 0x1F0AA0, 0x3F000000 }, { 0x1F0ACC, 0x3A802008 },
        { 0x1F0AD0, 0x35000000 }, { 0x1F0AD4, 0x3A001002 }, { 0x1F0AD8, 0x35800000 }, { 0x1F0AEC, 0x437F0000 },
        { 0x1F0AF8, 0xE0000000 }, { 0x1F0AFC, 0x3F1A36E2 }, { 0x1F0B40, 0x3C8EFA35 }, { 0x1F0C2C, 0x3E22F983 },
        { 0x2FC918, 0x00000000 },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) w32(k[i].a, k[i].v);
}
static float anyf(int flavor, float lo, float hi)
{
    switch (rnd() % 23) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return flavor ? nanf_() : lo;
    case 3: return flavor ? (rnd() & 1 ? INFINITY : -INFINITY) : hi;
    case 4: return frand(-1e-30f, 1e-30f);
    default: return frand(lo, hi);
    }
}
typedef struct { int flavor, nflares, alias, straddle; } scen;

static void scene(xctx *c, scen *sc)
{
    memset(g_xram, 0, ARENA);
    image_constants();
    tag_top = TAG + (rnd() % 64) * 4;
    const int flavor = sc->flavor = rnd() % 6 == 0;                  /* NaN / inf data */
    const int straddle = sc->straddle = rnd() % 13 == 0 && tag_page_below_stack() >= TAG + 0x8000u;   /* a record straddling into a stack page */
    /* the stack: random bytes; E (the body's esp) 8-aligned with the frame and the callee frames mapped */
    for (uint32_t a = STACK; a < STACK + STACK_PAGES * 4096u; a += 4) w32(a, rnd());
    uint32_t E = (STACK + 0x100 + (rnd() % (STACK_PAGES * 4096u - 0x300))) & ~7u;
    if (straddle) E = STACK + 0x3040u;                               /* the window starts at guest stack page 3 = physical stack page 0 */
    else if (rnd() % 11 == 0) E = STACK + 0x1000u - 0x40u - 8u * (rnd() % 12);   /* the window straddles a page boundary */
    const uint32_t ESP0 = E + 0xCCu + (rnd() & 4);                   /* push ebp; and esp,-8 -> E + 0xC8; sub esp,0B8h; 4 pushes -> E */
    /* camera and view */
    const uint16_t stage = (uint16_t)(rnd() % 3);
    w16(0x2FC6C0, rnd() % 29 == 0 ? 1 : 0); w16(0x2FC6C2, stage);
    for (unsigned i = 0; i < 3; ++i) wf(0x2FC6C8 + 4 * i, frand(-30, 30));
    { float fx = frand(-1, 1), fy = frand(-1, 1), fz = frand(-1, 1), n = sqrtf(fx * fx + fy * fy + fz * fz) + 1e-3f;
      wf(0x2FC6D4, fx / n); wf(0x2FC6D8, fy / n); wf(0x2FC6DC, fz / n); }
    for (unsigned i = 0; i < 9; ++i) wf(0x2FC764 + 4 * i, anyf(flavor, -1, 1));
    /* brightness arrays (object path 0x27FFB0 + (index << 2) + stage, surface path 0x2BFFD2 + 34 * i + 4 * j + stage) */
    for (uint32_t a = 0x27FFB0; a < 0x280FB0; a += 4) w32(a, rnd());
    for (uint32_t k = 1; k < 4; ++k) for (uint32_t a = 0x27FFB0 + (k << 18); a < 0x280FB0 + (k << 18); a += 4) w32(a, rnd());
    for (uint32_t a = 0x2BFFD0; a < 0x2C0FD0; a += 4) w32(a, rnd());
    /* flare definitions with their reflections */
    uint32_t defs[6]; const unsigned ndefs = 1 + rnd() % 6;
    for (unsigned d = 0; d < ndefs; ++d) {
        uint32_t def = tag_alloc(0xD0, rnd() % 17 == 0 ? 2 : 4);
        /* straddle: [def+C8h] (the reflection array pointer, X_M32) at page offset FFEh of the tag page whose physical
         * page lies right below the stack's: its high half is read from the host-adjacent page = the first two bytes of
         * the stack window (a page-split read would take the next guest page's bytes instead) */
        if (straddle && d == 0) def = tag_page_below_stack() + 0xF36u;
        defs[d] = def;
        for (uint32_t o = 0; o < 0xD0; o += 4) w32(def + o, rnd());
        wf(def + 8, anyf(flavor, 0.5f, 60)); wf(def + 0xC, anyf(flavor, 0, 10));
        wf(def + 0x18, anyf(flavor, 1, 80)); wf(def + 0x1C, rnd() % 4 == 0 ? -frand(0, 5) : anyf(flavor, 0, 20));
        if (rnd() % 9 == 0) { if (rnd() & 1) w32(def + 0x18, r32(def + 0x1C)); else wf(def + 0x18, 0.0f); }   /* zero range: x/0 */
        const uint16_t modes[] = { 1, 2, 3, 4, 1, 2, 3, 4, 0, 5, 0xFFFF };
        w16(def + 0x80, rnd() % 13 == 0 ? (uint16_t)rnd() : modes[rnd() % 11]);
        wf(def + 0x84, anyf(flavor, -2, 2));
        wf(def + 0x10, rnd() % 5 == 0 ? 50.0f : frand(0, 60)); w8(def + 0x30, (uint8_t)rnd());
        int32_t nrefl = (int32_t)(rnd() % 9); if (rnd() % 17 == 0) nrefl = -(int32_t)(rnd() % 3);
        w32(def + 0xC4, (uint32_t)nrefl);
        uint32_t refl = tag_alloc(0x80u * (nrefl > 0 ? (uint32_t)nrefl : 1u) + 8, rnd() % 19 == 0 ? 1 : 4);
        if (sc->alias == 1 && rnd() % 3 == 0) refl = E - 0x40u + 4u * (rnd() % 16);        /* reflections inside the stack window */
        w32(def + 0xC8, refl);
        if (straddle && d == 0) {
            w16(STACK + 0x3000u, (uint16_t)(refl >> 16));               /* window bytes 0..1 (frame offset -40h: never written) */
            w16(def + 0xCA, (uint16_t)((refl >> 16) ^ 0x10));           /* the next guest page: a different (valid) tag page */
        }
        for (int32_t r = 0; r < nrefl; ++r) {
            uint32_t q = refl + 0x80u * (uint32_t)r;
            if (q - (E - 0x40u) < 0x108u + 0x80u) continue;                             /* keep the aliased window as it is */
            for (uint32_t o = 0; o < 0x80; o += 4) w32(q + o, rnd());
            w16(q, (uint16_t)rnd()); w16(q + 4, (uint16_t)rnd());
            wf(q + 0x1C, anyf(flavor, 0, 2)); wf(q + 0x20, anyf(flavor, -1, 1));
            wf(q + 0x28, anyf(flavor, -1, 2)); wf(q + 0x2C, anyf(flavor, -1, 2));
            wf(q + 0x34, anyf(flavor, -0.5f, 1.5f)); wf(q + 0x38, anyf(flavor, -0.5f, 1.5f));
            int16_t idx = (int16_t)(rnd() % 4); if (rnd() % 11 == 0) idx = (int16_t)((int)(rnd() % 40) - 20); if (rnd() % 97 == 0) idx = (int16_t)rnd();
            w16(q + 0x3C, (uint16_t)idx);
            for (unsigned k = 0; k < 4; ++k) wf(q + 0x40 + 4 * k, rnd() % 3 == 0 ? 0.0f : anyf(flavor, 0, 1));
            wf(q + 0x50, anyf(flavor, 0, 1)); wf(q + 0x60, anyf(flavor, 0, 1));
            w8(q + 0x70, (uint8_t)rnd()); w16(q + 0x72, (uint16_t)(rnd() % 4)); wf(q + 0x74, anyf(flavor, 0.1f, 4)); wf(q + 0x78, anyf(flavor, 0, 4));
        }
    }
    /* the flare list */
    int32_t n = (int32_t)(rnd() % 49); if (rnd() % 23 == 0) n = 0; if (rnd() % 37 == 0) n = -(int32_t)(rnd() % 3);
    sc->nflares = n;
    const int other_view = rnd() % 10 == 0;                           /* every row of another view: no x87 op in the region */
    w32(0x2E34E0, (uint32_t)n);
    for (int32_t i = 0; i < (n > 0 ? n : 0); ++i) {
        uint32_t row = 0x2C76D0u + 40u * (uint32_t)i;
        for (uint32_t o = 0; o < 40; o += 4) w32(row + o, rnd());
        w32(row, defs[rnd() % ndefs]);
        if (sc->alias == 2 && rnd() % 5 == 0) w32(row, E - 0xB4u);   /* a definition overlapping the stack window: [def+C4h] = the
                                                                       * never-written frame dword [E+10h], [def+80h/84h] in the callee
                                                                       * frames, [def+C8h] = [E+14h] (the reflection brightness) */
        for (unsigned k = 0; k < 3; ++k) wf(row + 4 + 4 * k, anyf(flavor, -60, 60));
        w32(row + 0x10, rnd()); w32(row + 0x14, rnd());
        w8(row + 0x1B, rnd() % 6 == 0 ? 0 : (uint8_t)rnd());
        if (rnd() & 1) { w16(row + 0x1E, (uint16_t)(0x8000u | (rnd() % 4))); w16(row + 0x20, (uint16_t)(rnd() % 0x3E0)); }   /* object path: 0x27FFB0 + index * 4 */
        else { w16(row + 0x1E, (uint16_t)(rnd() % 0x70)); w16(row + 0x20, (uint16_t)(rnd() % 8)); }
        w8(row + 0x22, (uint8_t)((other_view ? stage + 1u + rnd() % 2 : rnd() % 5 == 0 ? rnd() % 3 : stage) | (rnd() & 0x80)));
        w8(row + 0x23, (uint8_t)rnd());
        w32(row + 0x24, rnd() % 6 == 0 ? (uint32_t)-(int32_t)(rnd() % 3) : rnd() % 5000);
    }
    if (sc->alias == 2) w32(E + 0x10u, rnd() % 5);
    /* the call: esp -> return address; ebp etc. random */
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = ESP0; w32(ESP0, 0x5DC0Cu);
    c->fsp = rnd() & 7; if (rnd() % 7 == 0) c->fsp |= 8u << (rnd() % 3);   /* stale high bits: pushes/pops mask them */
    static const uint16_t cws[] = { 0x027F, 0x027F, 0x027F, 0x037F, 0x067F, 0x0A7F, 0x0E7F, 0x025F };
    c->fcw = cws[rnd() % 8]; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = (rnd() & 1) ? 32 : ((rnd() & 1) ? 16 : 8);
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->df = 0;
    c->preempt = (int32_t)(rnd() % 200) - 20;
    w32(LOG_TOP, 0);
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_cf) F(f_of_override) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}
static int nan32(uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu); }
static int same_arena(const uint8_t *a, const uint8_t *b, int nan_ok, char *why, size_t n)
{
    if (!memcmp(a, b, ARENA)) return 1;
    for (uint32_t i = 0; i < ARENA; i += 4) {
        uint32_t x, y; memcpy(&x, a + i, 4); memcpy(&y, b + i, 4);
        if (x == y || (nan_ok && nan32(x) && nan32(y))) continue;
        snprintf(why, n, "arena+%06X %08X vs %08X", i, x, y); return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 2000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    const int bench = argc > 3 && !strcmp(argv[3], "bench");
    map_memory();
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    unsigned mismatches = 0, unexp = 0, flares = 0, verify_bad = 0, draws = 0, nan_regions = 0, nan_propagated = 0;
    for (unsigned k = 0; k < cases; ++k) {
        xctx c0; scen sc = { 0, 0, (int)(rnd() % 9), 0 }; scene(&c0, &sc); slice = 5 + rnd() % 60;
        memcpy(before, g_xram, ARENA);
        char why[240] = "";
        /* guest */
        xctx cg = c0; preempt_calls = barrier_calls = 0; unexpected = 0;
        xv_native_606b0_force(0); f_000606B0(&cg);
        const unsigned g_pre = preempt_calls, g_bar = barrier_calls, g_unexp = unexpected;
        memcpy(guest, g_xram, ARENA);
        draws += r32(LOG_TOP);
        /* verify (every region checked from the true guest state; the guest result is kept) */
        memcpy(g_xram, before, ARENA);
        xctx cv = c0; preempt_calls = barrier_calls = 0; unexpected = 0; log_mismatch = 0; log_nan = 0;
        xv_native_606b0_force(1); f_000606B0(&cv); xv_native_606b0_force(0);
        int vok = same_ctx(&cv, &cg, why, sizeof why) && preempt_calls == g_pre && same_arena(g_xram, guest, 0, why, sizeof why) && !log_mismatch;
        const unsigned v_nan = log_nan;
        /* NaN data: the NaN flavor, or flare data aliased onto random stack bytes (alias 1/2: any bit pattern) */
        const int nan_data = sc.flavor || sc.alias == 1 || sc.alias == 2;
        if (v_nan) { nan_regions += v_nan; if (!nan_data) { vok = 0; snprintf(why, sizeof why, "NaN-only difference without NaN data"); } }
        if (!vok) { verify_bad++; if (++mismatches <= 10) printf("case %u (%d flares) VERIFY MISMATCH (logged %u): %s\n", k, sc.nflares, log_mismatch, why); }
        /* native */
        memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = barrier_calls = 0; unexpected = 0;
        xv_native_606b0_force(2); f_000606B0(&cn); xv_native_606b0_force(0);
        int ok = same_ctx(&cn, &cg, why, sizeof why);
        if (ok && (preempt_calls != g_pre || barrier_calls != g_bar)) { ok = 0; snprintf(why, sizeof why, "xv_preempt %u vs %u, barrier %u vs %u", preempt_calls, g_pre, barrier_calls, g_bar); }
        if (ok && !same_arena(g_xram, guest, nan_data, why, sizeof why)) ok = 0;
        if (g_unexp || unexpected) { unexp++; ok = 0; snprintf(why, sizeof why, "unexpected guest path: %s", unexpected_what); }
        /* NaN data: a NaN whose payload a two-NaN operation picked (host code generation) can be read back as integer
         * bits by later guest code (e.g. through aliased records); accepted only when the per-region verify of the same
         * case differed in NaN-only words and nothing else */
        if (!ok && nan_data && vok && v_nan) { nan_propagated++; ok = 1; }
        if (!ok && ++mismatches <= 10) printf("case %u (%d flares, flavor %d, alias %d, straddle %d) native MISMATCH: %s\n", k, sc.nflares, sc.flavor, sc.alias, sc.straddle, why);
        if (!ok && getenv("N6_DEBUG")) {          /* the draw-path call log, native vs guest */
            for (uint32_t e = 0; e < 900; ++e) {
                uint32_t a = LOG_BASE + e * 72u, x[18], y[18];
                memcpy(x, g_xram + a, 72); memcpy(y, guest + a, 72);
                if (!memcmp(x, y, 72)) continue;
                printf("  log %u: id %X native", e, x[0]); for (unsigned i = 1; i < 18; ++i) printf(" %08X", x[i]);
                printf("\n  log %u: id %X guest ", e, y[0]); for (unsigned i = 1; i < 18; ++i) printf(" %08X", y[i]); printf("\n");
            }
        }
        if (sc.nflares > 0) flares += (unsigned)sc.nflares;
    }
    printf("native-606b0 differential: %u cases, %u flares, %u draw-path stub calls, %u guest-unexpected, %u verify-mode failures (%u NaN-only words, %u cases with NaN bits propagated, all in NaN-data cases), %u mismatches\n",
           cases, flares, draws, unexp, verify_bad, nan_regions, nan_propagated, mismatches);
    { extern void xv_native_606b0_report(unsigned); xv_native_606b0_report(cases); }
    if (bench) {
        /* one typical scene repeated: guest vs native (hot caches) */
        xctx c0; scen sc = { 0, 0, 0, 0 };
        do { scene(&c0, &sc); } while (sc.nflares < 30 || sc.flavor || r32(0x2FC6C0) & 0xFFFF);
        memcpy(before, g_xram, ARENA);
        struct timespec t0, t1; double tg, tn; const unsigned reps = 20000;
        for (int pass = 0; pass < 2; ++pass) {
            xv_native_606b0_force(pass ? 2 : 0);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (unsigned i = 0; i < reps; ++i) { xctx cc = c0; w32(LOG_TOP, 0); f_000606B0(&cc); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double us = ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / reps;
            if (pass) tn = us; else tg = us;
        }
        xv_native_606b0_force(0);
        printf("bench: %d flares, us/call guest %.2f native %.2f (%.2fx)\n", sc.nflares, tg, tn, tg / tn);
    }
    return mismatches != 0;
}
