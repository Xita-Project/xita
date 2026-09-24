/* Differential test: native f_00063C00 (recomp/kernel/xk_native_63c00.c) against the lifted guest bodies of f_00063C00,
 * f_000637A0, f_000B5EA0 (+ xk_math.c's xv_math_point_transform), f_00019E7B, f_0001EC1F, f_0001EABA (+ xk_crt_float.c),
 * extracted from a stage's shards by tools/test_native_63c00.py. Randomized flare tests in a synthetic guest arena:
 * shuffled tag pages, a reversed stack mapping, stack windows across page boundaries (split x87 stores) and misaligned
 * esp, random view matrices / projection rows / viewport / clamp bounds / floor control words (all rounding modes,
 * precision exception masked or not), sizes and points on every path (size or depth test failing, empty, negative and
 * overflowing areas, fistp out of range, int16 wrap), NaN/inf inputs, points aliasing the stack or the image, random
 * register/flag/x87 state. Each case runs the guest body (hook off), the native (mode 2) and verify mode (1) from
 * identical state and compares the whole arena, every xctx field, and every D3D HLE call (kind, registers, arguments,
 * x87 state at the call); verify mode must leave the guest's result and log no MISMATCH. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
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
static unsigned logged_mismatch, logged_lines; static char last_report[400];
void xk_os_log(const char *fmt, ...)
{
    char line[512]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (strstr(line, "MISMATCH")) { if (++logged_mismatch <= 5) printf("  log: %s", line); }
    if (strstr(line, "[native-63c00]") && strstr(line, "frames")) snprintf(last_report, sizeof last_report, "%s", line);
    logged_lines++;
}
uint64_t xk_os_monotonic_us(void) { return 0; }
static unsigned preempt_calls, x87_misses;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = 50; }
void xv_x87reg_miss(xctx *c, uint32_t ip) { (void)c; (void)ip; x87_misses++; }
volatile uint32_t xv_cur_fn; int xv_hle_timing; unsigned xv_hle_timed_calls;
void xv_hle_time_add(const char *n, uint64_t us) { (void)n; (void)us; }
void (*volatile xv_hle_tap)(xctx *, const char *);
int f_00061270_calls; void f_00061270(xctx *c) { (void)c; f_00061270_calls++; }
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
const char xv_object_job_marker = 0;
unsigned xv_light_census_enabled;
int xv_object_census_scope_begin(void) { return 0; }
void xv_object_census_scope_end(int *t) { (void)t; }
static unsigned math_locks, math_unlocks;
int xv_object_math_lock(void) { math_locks++; return 0; }
void xv_object_math_unlock(int *locked) { (void)locked; math_unlocks++; }
int xv_object_math_release_private(xctx *c, int *l, unsigned k, uint32_t o, unsigned ob, uint32_t s, unsigned sb) { (void)c; (void)l; (void)k; (void)o; (void)ob; (void)s; (void)sb; return 0; }
void xv_object_job_hle(xctx *c, unsigned a, void (*fn)(xctx *)) { (void)a; fn(c); }
#endif
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
/* The lifted CRT floor's error paths (NaN/inf argument, unmasked inexact): the native declines those, so both runs
 * execute the same guest code; deterministic stand-ins suffice. */
void f_0001EAF7(xctx *c) { c->r[0] = 1; c->r[4] += 4; }
void f_0001E9C7(xctx *c) { c->r[0] = 0x9C7; c->r[4] += 4; }
void f_0001EA1A(xctx *c) { c->r[0] = 0xA1A; c->r[4] += 4; }

/* ---- D3D HLE stand-ins: observe (tap, like XD3D_COUNT), record, return like the real ones ---------------------- */
typedef struct { uint8_t kind; uint32_t r[8], args[5], fsp; uint16_t fsw, fcw; double st[8]; int32_t preempt; uint32_t fk[9]; } hle_rec;
static hle_rec hlog[16]; static unsigned hlog_n; static int hle_light;   /* bench: no recording */
static void hle(xctx *c, const char *name, unsigned kind, unsigned nargs)
{
    if (xv_hle_tap) xv_hle_tap(c, name);
    if (hle_light) { hlog_n++; c->r[0] = 0; c->r[4] += 4u + 4u * nargs; return; }
    if (hlog_n < 16) {
        hle_rec *r = &hlog[hlog_n]; memset(r, 0, sizeof *r);
        r->kind = (uint8_t)kind; memcpy(r->r, c->r, sizeof r->r); r->fsp = c->fsp; r->fsw = c->fsw; r->fcw = c->fcw;
        memcpy(r->st, c->st, sizeof r->st); r->preempt = c->preempt;
        uint32_t fk[9] = { c->f_kind, c->f_op1, c->f_op2, c->f_res, c->f_bits, c->f_cf_override, c->f_cf, c->f_of_override, c->f_of };
        memcpy(r->fk, fk, sizeof fk);
        for (unsigned i = 0; i < nargs; ++i) r->args[i] = X_M32(c->r[4] + 4u + 4u * i);
    }
    hlog_n++;
    c->r[0] = kind == 4 ? 0x5EED0000u ^ hlog_n : 0; c->r[4] += 4u + 4u * nargs;
}
void xv_hle_D3DDevice_BeginVisibilityTest(xctx *c) { hle(c, "D3DDevice_BeginVisibilityTest", 0, 0); }
void xv_hle_D3DDevice_Begin(xctx *c) { hle(c, "D3DDevice_Begin", 1, 1); }
void xv_hle_D3DDevice_SetVertexData4f(xctx *c) { hle(c, "D3DDevice_SetVertexData4f", 2, 5); }
void xv_hle_D3DDevice_End(xctx *c) { hle(c, "D3DDevice_End", 3, 0); }
void xv_hle_D3DDevice_EndVisibilityTest(xctx *c) { hle(c, "D3DDevice_EndVisibilityTest", 4, 1); }

extern void f_00063C00(xctx *);
extern void xv_native_63c00_force(int);
extern void xv_native_63c00_report(unsigned);

/* ---- arena: image [0x1F0000, 0x2FD000) at a fixed offset (X_IMG and the table agree), tag and stack pages ------- */
enum { IMG_LO = 0x1F0000u, IMG_PAGES = 0x10Du, TAG = 0x40000000u, TAG_PAGES = 32, STACK = 0xD0000000u, STACK_PAGES = 4 };
enum { ARENA_PAGES = IMG_PAGES + TAG_PAGES + STACK_PAGES + 2, ARENA = ARENA_PAGES * 4096u };   /* + trash + guard */
static uint32_t trash_off;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static float fbits(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static float special(void)
{
    switch (rnd() % 7) {
    case 0: return fbits(0x7FC00000u | (rnd() & 0x3FFFFF));
    case 1: return fbits(0xFFC00000u | (rnd() & 0x3FFFFF));
    case 2: return fbits(0x7F800001u + (rnd() & 0x3FFFFE));   /* signaling */
    case 3: return INFINITY;
    case 4: return -INFINITY;
    case 5: return rnd() & 1 ? 3.0e38f : -3.0e38f;
    default: return rnd() & 1 ? 0.0f : -0.0f;
    }
}
static float maybe(float v, unsigned per_mille) { return rnd() % 1000 < per_mille ? special() : v; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }

static void map_memory(void)
{
    g_xram = calloc(1, ARENA); g_xpt = malloc(sizeof(uint32_t) << 20);
    trash_off = (IMG_PAGES + TAG_PAGES + STACK_PAGES) << 12;
    for (uint32_t i = 0; i < (1u << 20); ++i) g_xpt[i] = trash_off;
    for (uint32_t i = 0; i < IMG_PAGES; ++i) g_xpt[(IMG_LO >> 12) + i] = i << 12;
    g_img_base = g_xram - IMG_LO;
    uint32_t base = IMG_PAGES << 12, perm[TAG_PAGES];
    for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = base + (perm[i] << 12);
    base += TAG_PAGES << 12;
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = base + ((STACK_PAGES - 1 - i) << 12);
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
}

/* Scene flavors: 0 camera-like (a view matrix, a perspective projection, the 640x480 viewport: mostly drawn quads,
 * some behind the camera, partly off-screen, tiny or huge flares), 1 random (wide value ranges), 2 special values
 * (NaN, inf, huge, signed zeros in any input: exercises the declines and the guest's own handling). */
static unsigned flavor_count[4];
static float sp(float v, int flavor, unsigned per_mille) { return flavor == 2 && rnd() % 1000 < per_mille ? special() : v; }
static void context(xctx *c, uint32_t E);
static uint32_t stack_frame(void);
/* Flavor 3, ties: an identity camera with dyadic values, so the edges, extents, depth and size are exact and can be
 * made equal to the clamp bounds, 1.0, [1F0A68]: every compare's equal outcome (the fsw condition codes and the
 * branch the lift takes on equality). */
static uint32_t scene_ties(xctx *c)
{
    const float eps = rnd() % 2 ? 0.0f : 0.25f;
    const double one = rnd() % 3 ? 1.0 : 2.0;                            /* [1F0A78] = 2: the raise stores 1.0f, not it */
    wf(0x1F0A68, eps); wf(0x1F0A78, (float)one); wf(0x1F0AA0, 0.5f);
    w32(0x1F2840, 0x0000067Fu | (rnd() % 4 == 0 ? 0x0800u : 0u));      /* floor (or trunc), precision masked */
    int16_t r4[4] = { 0, 0, (int16_t)(64 * (1 + rnd() % 12)), (int16_t)(64 * (1 + rnd() % 8)) };
    x_guest_write_pages(0x2FC6F4, r4, 8);
    const double W = r4[2], H = r4[3];
    wf(0x2FC72C, 1.0f);                                                  /* scale 1, identity, no translation */
    for (unsigned i = 1; i < 13; ++i) wf(0x2FC72C + 4 * i, (i == 1 || i == 5 || i == 9) ? 1.0f : 0.0f);
    float pr[16] = { 0 };
    pr[0] = 1.0f; pr[5] = 1.0f; pr[10] = 1.0f; pr[15] = 1.0f;          /* x = P0, y = P1, depth = P2, w = 1 */
    for (unsigned i = 0; i < 16; ++i) wf(0x2FC860 + 4 * i, pr[i]);
    /* point: dyadic view-space coordinates; depth P2 sometimes exactly eps or 1 */
    const double P0 = (double)((int)(rnd() % 17) - 8) / 8.0, P1 = (double)((int)(rnd() % 17) - 8) / 8.0;
    const unsigned dz = rnd() % 4;
    const double P2 = dz == 0 ? eps : dz == 1 ? 1.0 : (double)(1 + rnd() % 64) / 4.0;   /* depth == eps, z == [1F0A78] */
    double size = dz == 3 && rnd() % 2 ? eps : (double)(1 + rnd() % 32) / 16.0;
    if (rnd() % 4 == 0) size = 2.0 / H;                                   /* x extent exactly [1F0A78] */
    const double invw = one;                                             /* w = 1 */
    const double SX = ((P0 * invw + one) * H - one) * 0.5, SY = ((one - invw * P1) * W - one) * 0.5;
    double RX = (H * invw * size) * 0.5, RY = (W * invw * size) * 0.5;
    if (one > RX) RX = 1.0;                                              /* the raise stores 1.0f */
    if (one > RY) RY = 1.0;
    const double edge[4] = { SX - RX, SY - RY, SX + RX, SY + RY };
    const double e = edge[rnd() % 4];
    double lo = rnd() % 3 ? 0.0 : -64.0, hi = 640.0;
    switch (rnd() % 4) { case 0: lo = e; break; case 1: hi = e; break; case 2: lo = e; hi = e; break; }
    wf(0x1F0D84, (float)lo); wf(0x1F0AE4, (float)hi);
    const uint32_t E = stack_frame();
    const uint32_t pt = TAG + 4u * (rnd() % ((TAG_PAGES * 0x1000u - 16u) / 4u));
    wf(pt, (float)P0); wf(pt + 4, (float)P1); wf(pt + 8, (float)P2);
    const float fsize = (float)size; uint32_t sb; memcpy(&sb, &fsize, 4);
    w32(E + 4, pt); w32(E + 8, sb); w32(E + 0xC, rnd());
    context(c, E);
    return E;
}
static uint32_t scene(xctx *c)
{
    const int flavor = rnd() % 100 < 8 ? 3 : rnd() % 100 < 60 ? 0 : rnd() % 100 < 65 ? 1 : 2;
    flavor_count[flavor]++;
    if (flavor == 3) return scene_ties(c);
    const int cam = flavor == 0;
    /* constants in .rdata/.data */
    wf(0x1F0A68, sp(rnd() % 10 < 7 ? 0.0f : rnd() % 3 == 0 ? -0.0f : frand(-1, 1), flavor, 60));
    wf(0x1F0A78, sp(cam || rnd() % 100 < 70 ? 1.0f : frand(0.25f, 3), flavor, 40));
    wf(0x1F0AA0, sp(cam || rnd() % 100 < 70 ? 0.5f : frand(0.1f, 2), flavor, 40));
    float lo = rnd() % 10 < 7 ? 0.0f : frand(-200, 200), hi = rnd() % 10 < 6 ? 640.0f : rnd() % 3 == 0 ? 479.0f : frand(-100, 70000);
    if (!cam) switch (rnd() % 10) {
    case 0: hi = lo; break;                                               /* empty range */
    case 1: { float t = lo; lo = hi + frand(1, 300); hi = t; break; }    /* inverted: negative areas */
    case 2: lo = -frand(30000, 70000); hi = frand(30000, 70000); break;   /* int16 wrap, imul overflow */
    case 3: lo = -frand(1e9f, 1e10f); hi = frand(1e9f, 1e10f); break;     /* fistp out of range */
    case 4: lo = -32768.0f + frand(0, 0.9f); hi = 32767.0f + frand(0, 0.9f); break;   /* 65535 x 65535: imul overflow */
    }
    wf(0x1F0D84, sp(lo, flavor, 60)); wf(0x1F0AE4, sp(hi, flavor, 60));
    uint16_t rc = rnd() % 10 < 8 ? 1 : rnd() % 4;
    uint16_t cw = (uint16_t)((rnd() % 10 < 9 ? 0x027Fu : (rnd() & 0xF3FFu)) & ~0x0C00u) | (uint16_t)(rc << 10);
    if (rnd() % 12 == 0) cw &= (uint16_t)~0x20u;                         /* precision exception unmasked */
    w32(0x1F2840, (rnd() & 0xFFFF0000u) | cw);
    /* viewport rectangle: int16 x0 y0 x1 y1 */
    int16_t r4[4] = { 0, 0, 640, 480 };
    if (!cam || rnd() % 6 == 0) for (unsigned i = 0; i < 4; ++i) r4[i] = (int16_t)(rnd() % 3 ? (int)(rnd() % 1400) - 200 : (int)(int16_t)rnd());
    x_guest_write_pages(0x2FC6F4, r4, 8);
    /* view matrix 0x2FC72C: scale, 3x3 (rows 1-3/4-6/7-9 as B5EA0 reads them), translation */
    float m[13];
    m[0] = rnd() % 10 < 7 ? 1.0f : frand(0.25f, 3);
    for (unsigned i = 1; i < 13; ++i) m[i] = cam ? (i < 10 ? ((i - 1) % 4 == 0 ? 1.0f : 0.0f) + frand(-0.3f, 0.3f) : frand(-5, 5)) : frand(-50, 50);
    for (unsigned i = 0; i < 13; ++i) wf(0x2FC72C + 4 * i, sp(m[i], flavor, 25));
    /* projection rows 2FC860..2FC89C (index = offset/4): x 8 4 0 12, y 9 5 1 13, depth 10 6 2 14, w 11 7 3 15 */
    float pr[16];
    for (unsigned i = 0; i < 16; ++i) pr[i] = cam ? frand(-0.05f, 0.05f) : frand(-40, 40);
    if (cam) {
        pr[0] = frand(0.6f, 1.8f); pr[5] = frand(0.8f, 2.4f);              /* focal x (also the size row), focal y */
        pr[10] = frand(0.9f, 1.1f); pr[14] = -frand(0.05f, 2.0f);          /* depth = z - near */
        pr[11] = 1.0f + frand(-0.01f, 0.01f); pr[15] = frand(-0.05f, 0.05f);   /* w = z */
    }
    for (unsigned i = 0; i < 16; ++i) wf(0x2FC860 + 4 * i, sp(pr[i], flavor, 20));
    const uint32_t E = stack_frame();
    /* the point (world space; the camera looks down +z of its view space) */
    uint32_t pt;
    const unsigned where = rnd() % 100;
    if (where < 84) pt = TAG + 4u * (rnd() % ((TAG_PAGES * 0x1000u - 16u) / 4u));
    else if (where < 90) pt = TAG + rnd() % (TAG_PAGES * 0x1000u - 16u);                 /* unaligned */
    else if (where < 95) pt = TAG + 0x1000u * (1 + rnd() % (TAG_PAGES - 1)) - (1 + rnd() % 11);   /* across pages */
    else if (where < 98) pt = E - 0x90u + rnd() % 0xB0u;                                /* in or next to the stack window */
    else pt = 0x2FC72Cu + 4u * (rnd() % 16);                                           /* the matrix itself */
    if (where < 95) {
        float z = rnd() % 8 == 0 ? frand(-20, 2) : frand(1, 150);
        float spread = rnd() % 4 == 0 ? 1.6f : 0.9f;                       /* sometimes off-screen */
        float v[3] = { frand(-spread, spread) * z, frand(-spread, spread) * z, z };
        if (!cam) for (unsigned i = 0; i < 3; ++i) v[i] = frand(-5000, 5000);
        if (cam && m[0] != 1.0f) for (unsigned i = 0; i < 3; ++i) v[i] /= m[0];
        for (unsigned i = 0; i < 3; ++i) wf(pt + 4 * i, sp(v[i], flavor, 30));
    }
    float size;
    const unsigned sz = rnd() % 100;
    if (sz < 70) size = cam ? (rnd() % 10 == 0 ? frand(5, 400) : frand(0.02f, 6.0f)) : rnd() % 3 ? frand(0.001f, 400.0f) : frand(1e3f, 1e9f);
    else if (sz < 80) size = frand(-5, 0);
    else if (sz < 88) size = rnd() % 2 ? 0.0f : -0.0f;
    else size = frand(0, 1e-3f);
    size = sp(size, flavor, 60);
    uint32_t size_bits; memcpy(&size_bits, &size, 4);
    w32(E + 4, pt); w32(E + 8, size_bits); w32(E + 0xC, rnd());
    context(c, E);
    return E;
}
/* the stack: E 4-aligned (misaligned 3%), the window [E-0x80, E+0x10) sometimes across a page boundary */
static uint32_t stack_frame(void)
{
    uint32_t E;
    if (rnd() % 5 == 0) { uint32_t b = STACK + 0x1000u * (1 + rnd() % (STACK_PAGES - 1)); E = b - 0x10u + 4u * (rnd() % 0x24u); }
    else E = STACK + 0x100u + 4u * (rnd() % ((STACK_PAGES * 0x1000u - 0x200u) / 4u));
    if (rnd() % 30 == 0) E += 1 + rnd() % 3;
    for (uint32_t a = E - 0x100u; a < E + 0x40u; a += 4) w32(a, rnd());   /* the stack below and above: noise */
    w32(E, 0x6224Cu);
    return E;
}
static void context(xctx *c, uint32_t E)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = E;
    c->fsp = rnd() & 7; c->fsw = (uint16_t)rnd();
    c->fcw = rnd() % 10 < 7 ? 0x027F : (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) { double d = (double)(int32_t)rnd() / 7.0; if (rnd() % 16 == 0) { uint64_t b = 0x7FF8000000000000ull | rnd(); memcpy(&d, &b, 8); } c->st[i] = d; }
    c->f_kind = rnd() % 6; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = (uint32_t[]){ 8, 16, 32 }[rnd() % 3];
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1; c->df = rnd() & 1;
    c->preempt = (int32_t)(rnd() % 200) - 20;
}

static int same_ctx(const xctx *a, const xctx *b, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_cf) F(f_of_override) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8)) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}
static int same_log(const hle_rec *a, unsigned an, const hle_rec *b, unsigned bn, char *why, size_t n)
{
    if (an != bn) { snprintf(why, n, "HLE calls %u vs %u", an, bn); return 0; }
    for (unsigned i = 0; i < an && i < 16; ++i) if (memcmp(&a[i], &b[i], sizeof a[i])) {
        const hle_rec *x = &a[i], *y = &b[i];
        for (unsigned j = 0; j < 8; ++j) if (x->r[j] != y->r[j]) { snprintf(why, n, "HLE %u (kind %u) r%u %08X vs %08X", i, x->kind, j, x->r[j], y->r[j]); return 0; }
        for (unsigned j = 0; j < 5; ++j) if (x->args[j] != y->args[j]) { snprintf(why, n, "HLE %u (kind %u) arg%u %08X vs %08X", i, x->kind, j, x->args[j], y->args[j]); return 0; }
        snprintf(why, n, "HLE %u (kind %u vs %u) x87/flags state", i, x->kind, y->kind); return 0;
    }
    return 1;
}

#include <time.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
/* User-space instruction count of this thread (perf_event_paranoid <= 2): the in-order Vita/A9 cost follows the dynamic
 * instruction count far more than an out-of-order x86 core's wall time does. -1 when unavailable. */
static int insn_fd = -2;
static int64_t insns(void)
{
    if (insn_fd == -2) {
        struct perf_event_attr a; memset(&a, 0, sizeof a);
        a.type = PERF_TYPE_HARDWARE; a.size = sizeof a; a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.exclude_kernel = 1; a.exclude_hv = 1;
        insn_fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    }
    if (insn_fd < 0) return -1;
    int64_t v; return read(insn_fd, &v, sizeof v) == sizeof v ? v : -1;
}
#else
static int64_t insns(void) { return -1; }
#endif
static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec; }
/* bench: camera-flavor scenes, the guest body vs the native on the same inputs (HLE stand-ins are cheap), ns/call. */
static int bench(unsigned scenes, unsigned reps)
{
    uint8_t *snap = malloc(ARENA); uint64_t tg = 0, tn = 0; unsigned calls = 0, drawn = 0; int64_t ig = 0, in = 0;
    for (unsigned k = 0; k < scenes; ++k) {
        xctx c0; uint32_t E;
        do { E = scene(&c0); } while (0);
        (void)E; memcpy(snap, g_xram, ARENA);
        xctx c = c0; hlog_n = 0; xv_native_63c00_force(0); f_00063C00(&c);
        if (hlog_n != 8) continue;                                    /* drawn quads only: the full path */
        extern unsigned xv_native_63c00_declined(void);
        unsigned d0 = xv_native_63c00_declined();
        c = c0; xv_native_63c00_force(2); f_00063C00(&c); memcpy(g_xram, snap, ARENA);
        if (xv_native_63c00_declined() != d0) continue;              /* the native declines it: the guest would run */
        drawn++;
        hle_light = 1;
        for (int mode = 0; mode < 2; ++mode) {
            xv_native_63c00_force(mode ? 2 : 0);
            uint64_t t0 = now_ns(); int64_t i0 = insns();
            for (unsigned r = 0; r < reps; ++r) { c = c0; c.preempt = 1 << 30; hlog_n = 0; f_00063C00(&c); }
            int64_t i1 = insns(); uint64_t t = now_ns() - t0;
            if (mode) { tn += t; in += i1 - i0; } else { tg += t; ig += i1 - i0; }
            memcpy(g_xram, snap, ARENA);
        }
        hle_light = 0;
        calls += reps;
    }
    printf("bench: %u drawn scenes x %u reps: guest %.1f ns/call, native %.1f ns/call (%.2fx), HLE stand-ins included (no recording)\n",
           drawn, reps, (double)tg / calls, (double)tn / calls, (double)tg / (double)(tn ? tn : 1));
    if (ig > 0 && in > 0) printf("bench: instructions/call guest %.0f native %.0f (%.2fx)\n", (double)ig / calls, (double)in / calls, (double)ig / (double)in);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    int with_verify = argc > 3 ? atoi(argv[3]) : 1;
    map_memory();
    if (argc > 4 && !strcmp(argv[4], "bench")) return bench(cases, 2000);
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    unsigned mismatches = 0, verify_bad = 0, drawn = 0, fail = 0, empty = 0, misses_g = 0;
    hle_rec glog[16]; unsigned glog_n;
    for (unsigned k = 0; k < cases; ++k) {
        xctx c0; scene(&c0);
        memcpy(before, g_xram, ARENA);
        /* guest */
        xctx cg = c0; preempt_calls = 0; x87_misses = 0; hlog_n = 0;
        xv_native_63c00_force(0);
        f_00063C00(&cg);
        unsigned gp = preempt_calls, gm = x87_misses; misses_g += gm;
        memcpy(glog, hlog, sizeof glog); glog_n = hlog_n;
        memcpy(guest, g_xram, ARENA);
        if (glog_n == 8) drawn++; else if (!glog_n && cg.f_bits == 8) fail++; else if (!glog_n) empty++;
        /* native */
        memcpy(g_xram, before, ARENA);
        xctx cn = c0; preempt_calls = 0; x87_misses = 0; hlog_n = 0;
        xv_native_63c00_force(2);
        f_00063C00(&cn);
        char why[200] = "";
        int ok = same_ctx(&cn, &cg, why, sizeof why) && same_log(hlog, hlog_n, glog, glog_n, why, sizeof why);
        if (ok && (preempt_calls != gp || x87_misses != gm)) { ok = 0; snprintf(why, sizeof why, "preempt %u/%u x87 misses %u/%u", preempt_calls, gp, x87_misses, gm); }
        if (ok && memcmp(guest, g_xram, ARENA)) {
            ok = 0;
            for (uint32_t i = 0; i < ARENA; ++i) if (guest[i] != g_xram[i]) {
                const char *reg = i < IMG_PAGES * 4096u ? "image" : i < (IMG_PAGES + TAG_PAGES) * 4096u ? "tag" : i < trash_off ? "stack" : "trash";
                long e_off = 0;
                if (!strcmp(reg, "stack")) { uint32_t pg = (i >> 12) - IMG_PAGES - TAG_PAGES; uint32_t va = STACK + ((STACK_PAGES - 1 - pg) << 12) + (i & 0xFFF); e_off = (long)va - (long)c0.r[4]; }
                snprintf(why, sizeof why, "arena+%06X (%s, E%+ld) guest %02X native %02X", i, reg, e_off, guest[i], g_xram[i]); break;
            }
        }
        if (!ok && ++mismatches <= 12) printf("case %u MISMATCH: %s (guest HLE %u, eax %08X)\n", k, why, glog_n, cg.r[0]);
        /* verify mode: must leave exactly the guest's result */
        if (with_verify) {
            memcpy(g_xram, before, ARENA);
            xctx cv = c0; preempt_calls = 0; x87_misses = 0; hlog_n = 0;
            unsigned lm = logged_mismatch;
            xv_native_63c00_force(1);
            f_00063C00(&cv);
            xv_native_63c00_force(0);
            char vwhy[200] = "";
            int vok = same_ctx(&cv, &cg, vwhy, sizeof vwhy) && same_log(hlog, hlog_n, glog, glog_n, vwhy, sizeof vwhy) && !memcmp(guest, g_xram, ARENA);
            if (logged_mismatch != lm) { vok = 0; snprintf(vwhy, sizeof vwhy, "verify logged %u MISMATCH lines", logged_mismatch - lm); }
            if (!vok && ++verify_bad <= 6) printf("case %u VERIFY: %s\n", k, vwhy[0] ? vwhy : "arena differs from the guest run");
        }
    }
    xv_native_63c00_report(60);
    printf("native-63c00 differential: %u cases (flavors camera %u random %u special %u ties %u; guest: %u drawn, %u failed projection, %u empty/negative, %u x87-reg misses), %u mismatches, %u verify-mode failures\n",
           cases, flavor_count[0], flavor_count[1], flavor_count[2], flavor_count[3], drawn, fail, empty, misses_g, mismatches, verify_bad);
    printf("  %s", last_report);
    extern unsigned xv_native_63c00_cov[];
    static const char *const cn[] = { "size-fail", "depth-fail", "depth-clamp", "rx-raise", "ry-raise", "edge<lo", "edge>hi", "inexact-floor",
                                      "fistp-range", "int16-wrap", "imul-overflow", "negative", "zero", "drawn", "matrix-scale" };
    printf("  branches (native runs):"); for (unsigned i = 0; i < sizeof cn / sizeof cn[0]; ++i) printf(" %s %u", cn[i], xv_native_63c00_cov[i]); printf("\n");
    return mismatches || verify_bad;
}
