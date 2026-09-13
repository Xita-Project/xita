#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { ARENA = 1 << 20, MODEL = 0x12000, POSE = 0x21000, NODES = 0x31000, SP = 0x51000 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void original_palette(xctx *), current_palette(xctx *), candidate_palette(xctx *);
int xv_math_model_palette(xctx *);
static unsigned yields;
void __wrap_xv_preempt(xctx *c) { yields++; c->preempt = 100; }
static uint32_t rng = 92351;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void same_context(xctx a, xctx b, unsigned k)
{
    /* SSE NaN payload selection can differ across host ISAs; all non-NaN
     * values, flags, registers and scheduling state must be byte-identical. */
    for (unsigned i = 0; i < 8; i++) {
        if (isnan(a.st[i]) && isnan(b.st[i])) a.st[i] = b.st[i] = 0;
        for (unsigned j = 0; j < 4; j++)
            if (isnan(a.xmm[i][j]) && isnan(b.xmm[i][j])) a.xmm[i][j] = b.xmm[i][j] = 0;
    }
    if (memcmp(&a, &b, sizeof a)) {
        for (unsigned i = 0; i < sizeof a; i++)
            if (((uint8_t *)&a)[i] != ((uint8_t *)&b)[i])
                fprintf(stderr, "case %u context byte %u expected %02x got %02x\n", k, i,
                        ((uint8_t *)&a)[i], ((uint8_t *)&b)[i]);
        abort();
    }
}

static void same_memory(uint8_t *expected, uint32_t output, unsigned count, unsigned k)
{
    for (unsigned i = 0; i < count * 13; i++) {
        unsigned offset = (uint8_t *)X_G(output + i * 4) - g_xram;
        float a, b;
        memcpy(&a, expected + offset, 4); memcpy(&b, g_xram + offset, 4);
        if (isnan(a) && isnan(b)) memcpy(expected + offset, g_xram + offset, 4);
    }
    if (memcmp(expected, g_xram, ARENA)) {
        for (unsigned i = 0; i < ARENA; i++) if (expected[i] != g_xram[i]) {
            fprintf(stderr, "case %u arena byte %u expected %02x got %02x\n", k, i, expected[i], g_xram[i]);
            break;
        }
        abort();
    }
}

static xctx fixture(unsigned k, unsigned count)
{
    for (unsigned i = 0; i < ARENA / 4096; i++) g_xpt[i] = (i ^ 0x40) * 4096;
    memset(g_xram, 0xa5, ARENA);
    xctx c = {0};
    for (unsigned i = 0; i < 8; i++) {
        c.r[i] = next(); c.st[i] = i + .375;
        for (unsigned j = 0; j < 4; j++) c.xmm[i][j] = i * 4 + j + .25f;
    }
    c.r[4] = SP + (k % 256) * 4;
    c.r[5] = MODEL;
    c.r[7] = POSE + (k % 1000) * 4;
    c.preempt = count;
    c.fsp = k % 8; c.fsw = (uint16_t)next(); c.fcw = 0x37f;
    c.f_kind = XK_SUB; c.f_bits = 32;
    c.f_op1 = next(); c.f_op2 = next(); c.f_res = c.f_op1 - c.f_op2;
    c.f_cf = next(); c.f_of = next(); c.f_cf_override = c.f_of_override = 1;
    X_M32(MODEL + 0xb8) = count;
    X_M32(MODEL + 0xbc) = NODES + (k % 256) * 4;
    static const uint32_t edges[] = {0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,
                                     0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x3f800000};
    /* Sometimes the two read-only inputs overlap; outputs never alias them. */
    if (k % 13 == 0) X_M32(MODEL + 0xbc) = c.r[7] - 0x68;
    for (unsigned n = 0; n < count; n++) for (unsigned j = 0; j < 13; j++) {
        uint32_t a = next(), b = next();
        if (k % 3 == 0) {
            a = (a & 0x807fffff) | ((110 + k % 30) << 23);
            b = (b & 0x807fffff) | ((110 + k % 30) << 23);
        }
        if (k % 3 == 1) {
            a = edges[(k + n + j) % (sizeof edges / sizeof *edges)];
            b = edges[(k + 3 * n + j) % (sizeof edges / sizeof *edges)];
        }
        X_M32(c.r[7] + n * 52 + j * 4) = a;
        X_M32(X_M32(MODEL + 0xbc) + 0x68 + n * 156 + j * 4) = b;
    }
    return c;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int enabled = !strcmp(argv[1], "enabled");
    unsetenv("XV_NATIVE_MODEL_PALETTE");
    setenv("XV_NATIVE_MATH", !strcmp(argv[1], "math-disabled") ? "0" : "1", 1);
    if (strcmp(argv[1], "unset"))
        setenv("XV_NATIVE_MODEL_PALETTE", !strcmp(argv[1], "disabled") ? "0" : "1", 1);
    g_xram = malloc(ARENA); g_img_base = g_xram; g_xpt = calloc(1 << 20, 4);
    uint8_t *before = malloc(ARENA), *expected = malloc(ARENA);
    assert(g_xram && g_xpt && before && expected);
    unsigned accepted = 0, rejected = 0;
    for (unsigned k = 0; k < 4096; k++) {
        const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
        if (k % 1024 == 0) assert(!fesetround(modes[k / 1024]));
        unsigned count = 1 + k % 64;
        xctx initial = fixture(k, count), reference = initial, candidate = initial;
        if (count == 1 && k % 2 == 0) initial.preempt = reference.preempt = candidate.preempt = 0;
        memcpy(before, g_xram, ARENA);
        yields = 0; original_palette(&reference); assert(!yields);
        memcpy(expected, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        xctx current = initial;
        current_palette(&current);
        same_context(reference, current, k);
        same_memory(expected, initial.r[4] + 0xe4, count, k);
        memcpy(g_xram, before, ARENA);
        int used = xv_math_model_palette(&candidate);
        if (used) { accepted++; assert(enabled); }
        else {
            rejected++; assert(!enabled);
            assert(!memcmp(&initial, &candidate, sizeof initial));
            assert(!memcmp(before, g_xram, ARENA));
            candidate_palette(&candidate);
        }
        assert(!yields);
        same_context(reference, candidate, k);
        same_memory(expected, initial.r[4] + 0xe4, count, k);
    }
    assert(!fesetround(FE_TONEAREST));
    /* Every decline must precede any write, including for physical aliases,
     * split mappings and ranges that would wrap the 32-bit guest address. */
    for (unsigned variant = 0; variant < 19; variant++) {
        xctx c = fixture(1, 32);
        uint32_t output = c.r[4] + 0xe4;
        switch (variant) {
        case 0: X_M32(MODEL + 0xb8) = 0; break;
        case 1: X_M32(MODEL + 0xb8) = 65; break;
        case 2: X_M32(MODEL + 0xb8) = 0xffffffffu; break;
        case 3: c.r[7] = 0; break;
        case 4: c.r[7]++; break;
        case 5: c.r[5]++; break;
        case 6: c.r[4]++; break;
        case 7: c.r[7] = 0xfffffffcu; break;
        case 8: c.r[4] = 0xffffff00u; break;
        case 9: X_M32(MODEL + 0xbc) = 0xfffffffcu; break;
        case 10: c.r[4] = 16; break;
        case 11: c.preempt = 31; break;
        case 12: c.r[7] = output; break;
        case 13: X_M32(MODEL + 0xbc) = c.r[4] - 32 - 0x68; break;
        case 14: g_xpt[c.r[7] >> 12] = g_xpt[output >> 12]; break;
        case 15: c.r[7] = 0x21ffc; g_xpt[0x22] = g_xpt[0x25]; break;
        case 16: g_xpt[0x32] = g_xpt[0x35]; break;
        case 17: c.r[5] = UINT32_MAX - 4; break;
        case 18: c.preempt = INT32_MIN; break;
        }
        xctx initial = c;
        memcpy(before, g_xram, ARENA);
        assert(!xv_math_model_palette(&c));
        assert(!memcmp(&initial, &c, sizeof c));
        assert(!memcmp(before, g_xram, ARENA));
    }
    /* The injected hook must fall through when the loop would hand off. */
    for (unsigned budget = 1; budget <= 7; budget++) {
        xctx initial = fixture(budget, 8), reference, candidate;
        initial.preempt = budget; reference = candidate = initial;
        memcpy(before, g_xram, ARENA);
        yields = 0; original_palette(&reference); unsigned original_yields = yields;
        assert(original_yields == 1);
        memcpy(expected, g_xram, ARENA); memcpy(g_xram, before, ARENA);
        yields = 0; candidate_palette(&candidate);
        assert(yields == original_yields);
        same_context(reference, candidate, 5000 + budget);
        same_memory(expected, initial.r[4] + 0xe4, 8, 5000 + budget);
    }
    printf("PASS %s: %u batches accepted / %u fallback, 4096 original comparisons, "
           "19 unchanged declines, 7 guest handoffs; full context and arena\n", argv[1], accepted, rejected);
    if (enabled) for (unsigned count = 1; count <= 64; count *= 2) {
        xctx initial = fixture(300, count), c;
        double duration[2];
        for (unsigned path = 0; path < 2; path++) {
            double start = now();
            for (unsigned i = 0; i < 20000; i++) {
                c = initial;
                if (path) candidate_palette(&c); else current_palette(&c);
            }
            duration[path] = now() - start;
        }
        printf("Host microbenchmark %u nodes: current %.1f ns, batch %.1f ns (not Vita frame time)\n",
               count, duration[0] * 50000, duration[1] * 50000);
    }
    free(before); free(expected); free(g_xram); free(g_xpt);
    return 0;
}
