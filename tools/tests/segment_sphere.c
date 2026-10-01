/* Differential fixture for the optional 0xB0CB0 segment/sphere body.
 * Links with a private generated unit defining reference_000B0CB0 (original)
 * and candidate_000B0CB0 (hooked emission). Host builds run main(); ARM
 * builds (TEST_ARM) export seg_prepare and the fixture globals to a driver. */
#define _GNU_SOURCE
#include "xv_x86rt.h"
#include <stddef.h>
#ifndef TEST_ARM
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#endif

enum { PAGES = 64, ARENA = PAGES * 4096, STACK_PAGE = 0x20, NONE = 0xFFFFFFFFu };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
#ifdef TEST_ARM
uint8_t seg_arena[ARENA + 8];
uint32_t seg_pages[1u << 20];
#endif
xctx seg_ctx;
/* Expected admission (candidate ON), coverage class, yields and observations. */
uint32_t seg_expect_admit, seg_class, seg_yields, seg_events, seg_seed;
extern unsigned xv_segment_sphere_mode, xv_segment_sphere_count;
void reference_000B0CB0(xctx *), candidate_000B0CB0(xctx *);
const unsigned layout[] = {sizeof(xctx), offsetof(xctx, r), offsetof(xctx, st), offsetof(xctx, fsp),
                           offsetof(xctx, fsw), offsetof(xctx, preempt)};
void test_boot(void) {}

static uint32_t rng;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t hash(uint32_t h, const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* Yields hash the whole context and the call's stack neighbourhood, then
 * change registers, x87 state, lazy flags, budget and a stack byte. */
void __wrap_xv_preempt(xctx *c)
{
    uint32_t h = hash(2166136261u, (const uint8_t *)c, sizeof *c);
    uint32_t sp = c->r[4];
    for (uint32_t a = sp - 0x20u; a != sp + 0x30u; a++) { uint8_t b = X_M8(a); h = hash(h, &b, 1); }
    seg_events = hash(seg_events, (const uint8_t *)&h, 4);
    seg_yields++;
    c->preempt = 1 + (int32_t)(h % 37u);
    switch (h % 5u) {
    case 1: c->r[0] = h; break;
    case 2: c->fsp = (h >> 3) & 7u; c->st[(h >> 7) & 7u] = (double)(int32_t)h; break;
    case 3: c->fsw ^= (uint16_t)h; c->f_cf ^= 1u; c->f_kind = XK_SUB; break;
    case 4: X_M8(sp + 1u) ^= (uint8_t)h; c->r[1] = ~h; break;
    default: break;
    }
}
void __wrap_xv_trap(xctx *c, uint32_t eip) { (void)c; (void)eip; __builtin_trap(); }

static uint32_t float_word(void)
{
    static const uint32_t special[] = {
        0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00001u,
        0x7F800001u, 0x00000001u, 0x80000001u, 0x000FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu,
        0x00800000u, 0x3F800000u, 0x5F000000u, 0x1E000000u};
    uint32_t pick = next() % 20u;
    if (pick < 12u) {
        float v = (float)((int32_t)(next() % 33u) - 16) * 0.5f;
        uint32_t w; __builtin_memcpy(&w, &v, 4); return w;
    }
    if (pick < 16u) return (next() & 0x807FFFFFu) | ((next() % 255u) << 23);
    if (pick < 18u) return next() & 0x807FFFFFu;
    return special[next() % (sizeof special / sizeof *special)];
}

static void put_float3(uint32_t address)
{
    for (unsigned i = 0; i < 3; i++) { uint32_t w = float_word(); x_guest_write(address + i * 4u, &w, 4); }
}

static int nan_word(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }

/* Independent coverage classification of the lifted path (not a correctness
 * oracle): 1 inside, 2 leaving, 3 no root, 4 behind start (yield), 5 other,
 * 6 NaN direction continuation, 0 declined. */
static uint32_t classify(uint32_t s, uint32_t a, uint32_t b, uint32_t d)
{
    float f[8];
    x_guest_read(f, a, 12); x_guest_read(f + 3, b, 12);
    x_guest_read(f + 6, s + 0x14u, 4); x_guest_read(f + 7, 0x1F0A68u, 4);
    double x = (double)f[0] - f[3], y = (double)f[1] - f[4], z = (double)f[2] - f[5];
    double q = (z * z + y * y) + x * x - (double)f[6] * f[6];
    float q32 = (float)q, k = f[7];
    if (q < k) return 1;
    uint32_t w[3];
    x_guest_read(w, d, 12);
    for (unsigned i = 0; i < 3; i++) if (nan_word(w[i])) return 6;
    float g[3];
    __builtin_memcpy(g, w, 12);
    double t = ((double)g[2] * z + (double)g[1] * y) + (double)g[0] * x;
    float t32 = (float)t;
    if (!(t32 < k || t32 != t32)) return 2;
    double l = ((double)g[2] * g[2] + (double)g[1] * g[1]) + (double)g[0] * g[0];
    double disc = (double)t32 * t32 - l * q32;
    if (disc <= k) return 3;
    double m = -l - t32;
    return m < k ? 4 : 5;
}

void seg_prepare(uint32_t seed)
{
#ifdef TEST_ARM
    g_xram = g_img_base = seg_arena;
    g_xpt = seg_pages;
#endif
    static int mapped;
    if (!mapped) {
        for (unsigned i = 0; i < (1u << 20); i++) g_xpt[i] = (i < PAGES ? (i ^ 1u) : PAGES - 1u) * 4096u;
        mapped = 1;
    }
    /* Only these entries change between cases; the helper never writes maps. */
    g_xpt[0x39] = (0x39u ^ 1u) * 4096u;
    g_xpt[0x1F0] = 12u * 4096u;
    rng = seed * 2654435761u + 0x9E3779B9u;
    if (!rng) rng = 1;
    seg_seed = seed;
    for (unsigned i = 0; i < ARENA + 8u; i++) g_xram[i] = (uint8_t)next();
    uint32_t k = next() % 16u ? 0u : float_word();
    x_guest_write(0x1F0A68u, &k, 4);
    /* Entry offsets include page ends, where the helper declines, and stack
     * frames whose [S+4]/[S+8]/[S+0Ch] word stores straddle the page. */
    uint32_t offsets[] = {0x800u, 0x40u, 0xFE8u + 0x10u, 0xFE9u + 0x10u, 0xFFCu, 0x14u, 0x10u, 0x0u,
                          0x1u, 0x2u, 0x5u, 0x6u, 0x9u, 0xAu, 0xBu, 0xDu};
    uint32_t entry = (STACK_PAGE << 12) + ((next() % 3u) ? (0x100u + (next() % 0xD00u)) : offsets[next() % 16u]);
    uint32_t s = entry - 0x10u;
    uint32_t a = (0x28u << 12) + next() % 4096u, b = (0x30u << 12) + next() % 4096u;
    uint32_t d = (0x38u << 12) + next() % 4096u;
    put_float3(a); put_float3(b); put_float3(d);
    uint32_t radius = float_word();
    x_guest_write(entry + 4u, &radius, 4);
    uint32_t ret = next(); x_guest_write(entry, &ret, 4);
    switch (next() % 16u) {   /* aliases of inputs and stack stores */
    case 0: d = s; break;
    case 1: d = s + 4u; break;
    case 2: d = s + 0x10u; break;
    case 3: a = s + 0x14u; break;
    case 4: b = s + 0x8u; break;
    case 5: g_xpt[0x1F0] = g_xpt[entry >> 12]; x_guest_write(0x1F0A68u, &k, 4); break;
    case 6: g_xpt[0x39] = g_xpt[STACK_PAGE]; d = (0x39u << 12) + ((s + 4u) & 4095u); break;
    case 7: a = (0x28u << 12) + 4094u; put_float3(a); break;
    default: break;
    }
    for (unsigned i = 0; i < sizeof seg_ctx; i++) ((uint8_t *)&seg_ctx)[i] = (uint8_t)next();
    seg_ctx.r[0] = b; seg_ctx.r[1] = a; seg_ctx.r[2] = d; seg_ctx.r[4] = entry;
    seg_ctx.fsp &= 7u; seg_ctx.fcw = 0x037F;
    seg_ctx.f_kind %= 6u; seg_ctx.f_bits = (uint32_t[]){8, 16, 32}[next() % 3u];
    seg_ctx.f_cf_override &= 1u; seg_ctx.f_of_override &= 1u; seg_ctx.f_cf &= 1u; seg_ctx.f_of &= 1u;
    seg_ctx.preempt = next() % 3u ? 1 : (int32_t)(next() % 200u) - 50;
    seg_yields = 0; seg_events = 2166136261u;
    /* Expected admission, from actual guest bytes after all aliases. */
    uint32_t w[8];
    x_guest_read(w, a, 12); x_guest_read(w + 3, b, 12);
    x_guest_read(w + 6, s + 0x14u, 4); x_guest_read(w + 7, 0x1F0A68u, 4);
    int admit = (s & 4095u) <= 4096u - 0x18u && g_xpt[0x1F0] != g_xpt[s >> 12];
    for (unsigned i = 0; i < 8; i++) if (nan_word(w[i])) admit = 0;
    seg_expect_admit = (uint32_t)admit;
    seg_class = admit ? classify(s, a, b, d) : 0u;
}

#ifndef TEST_ARM
static uint8_t arena[ARENA + 8], initial[ARENA + 8], expected_arena[ARENA + 8];
static void fail(const char *what, unsigned seed, int rounding, unsigned lane)
{
    fprintf(stderr, "segment/sphere mismatch: %s seed %u rounding %d lane %u class %u\n",
            what, seed, rounding, lane, seg_class);
    abort();
}
int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 20000u;
    int check_admission = !(argc > 2 && !strcmp(argv[2], "state-only"));
    g_xram = g_img_base = arena;
    g_xpt = malloc((1u << 20) * sizeof *g_xpt);
    assert(g_xpt);
    static const int modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
    unsigned classes[7] = {0}, yields = 0, admitted = 0;
    for (unsigned seed = 0; seed < cases; seed++) {
        seg_prepare(seed);
        memcpy(initial, arena, sizeof arena);
        xctx start = seg_ctx;
        int rounding = modes[seed % 4u];
        fesetround(rounding);
        xctx expected = start;
        feclearexcept(FE_ALL_EXCEPT);
        seg_yields = 0; seg_events = 2166136261u;
        reference_000B0CB0(&expected);
        int expected_flags = fetestexcept(FE_ALL_EXCEPT);
        uint32_t expected_yields = seg_yields, expected_events = seg_events;
        memcpy(expected_arena, arena, sizeof arena);
        for (unsigned lane = 1; lane <= 2; lane++) {
            memcpy(arena, initial, sizeof arena);
            xv_segment_sphere_mode = lane == 1;
            xv_segment_sphere_count = 0;
            xctx actual = start;
            feclearexcept(FE_ALL_EXCEPT);
            seg_yields = 0; seg_events = 2166136261u;
            candidate_000B0CB0(&actual);
            if (fetestexcept(FE_ALL_EXCEPT) != expected_flags) fail("fp exceptions", seed, rounding, lane);
            if (memcmp(&actual, &expected, sizeof actual)) fail("context", seed, rounding, lane);
            if (memcmp(arena, expected_arena, sizeof arena)) fail("memory", seed, rounding, lane);
            if (seg_yields != expected_yields || seg_events != expected_events) fail("yields", seed, rounding, lane);
            if (check_admission && xv_segment_sphere_count != (lane == 1 ? seg_expect_admit : 0u))
                fail("admission", seed, rounding, lane);
        }
        fesetround(FE_TONEAREST);
        classes[seg_class]++;
        yields += expected_yields;
        admitted += seg_expect_admit;
    }
    printf("PASS segment/sphere host: %u cases x 2 candidate lanes, admitted %u, yields %u, "
           "classes declined %u inside %u leaving %u no-root %u behind %u other %u nan-direction %u\n",
           cases, admitted, yields, classes[0], classes[1], classes[2], classes[3], classes[4], classes[5], classes[6]);
    for (unsigned i = 1; i < 7; i++) assert(classes[i] > cases / 400u);
    return 0;
}
#endif
