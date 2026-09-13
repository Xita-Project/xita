/* Byte-level reference for REP STOS. No game assets or SDK needed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xv_x86rt.h"

uint8_t *g_xram;
uint32_t *g_xpt;
enum { ARENA = 8 * 4096 };

static void check(unsigned sz, unsigned df, int mode, uint32_t value,
                  uint32_t address, unsigned count, unsigned fixture)
{
    uint8_t expected[ARENA];
    xctx c, want;
    memset(&c, 0xA5, sizeof c);
    c.df = df; c.r[0] = value; c.r[7] = address; c.r[1] = count;
    want = c;
    for (unsigned i = 0; i < ARENA; ++i)
        g_xram[i] = expected[i] = (uint8_t)(i * 31u + fixture * 13u);
    unsigned elements = mode == X_STR_ONCE ? 1 : count;
    for (unsigned n = 0; n < elements; ++n) {
        for (unsigned b = 0; b < sz; ++b) {
            uint32_t a = want.r[7] + b;
            expected[g_xpt[a >> 12] + (a & 4095)] = (uint8_t)(value >> (b * 8));
        }
        want.r[7] += df ? 0u - sz : sz;
    }
    if (mode != X_STR_ONCE) want.r[1] = 0;
    x_str_stos(&c, sz, mode);
    if (memcmp(&c, &want, sizeof c) || memcmp(g_xram, expected, ARENA)) {
        fprintf(stderr, "STOS fixture %u: size%u df%u mode%d value%08x address%08x count%u\n",
                fixture, sz, df, mode, value, address, count);
        abort();
    }
}

int main(void)
{
    static const uint32_t values[] = {
        0, UINT32_MAX, 0x01010101, 0xA5A5A5A5, 0x0000FFFF,
        0x1234AAAA, 0x01000001, 0x12345678
    };
    static const uint32_t addresses[] = {
        0x10000, 0x10001, 0x10002, 0x10003, 0x10FF9, 0x10FFA,
        0x10FFB, 0x10FFC, 0x10FFD, 0x10FFE, 0x10FFF, 0x11000,
        0xFFFFFFFD, 0xFFFFFFFE, 0xFFFFFFFF, 0
    };
    static const unsigned counts[] = {0, 1, 2, 7, 150, 1024, 1025, 4097};
    g_xram = malloc(ARENA);
    g_xpt = malloc((1u << 20) * sizeof *g_xpt);
    assert(g_xram && g_xpt);
    unsigned fixtures = 0;
    for (unsigned layout = 0; layout < 2; ++layout) {
        /* Nonadjacent pages, then aliased guest pages. Both cover address wrap. */
        for (unsigned p = 0; p < (1u << 20); ++p)
            g_xpt[p] = ((p * 3u) % (layout ? 3u : 7u)) * 4096;
        for (unsigned sz = 1; sz <= 4; sz *= 2)
        for (unsigned df = 0; df < 2; ++df)
        for (int mode = X_STR_ONCE; mode <= X_STR_REPNE; ++mode)
        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v)
        for (unsigned a = 0; a < sizeof addresses / sizeof *addresses; ++a)
        for (unsigned n = 0; n < sizeof counts / sizeof *counts; ++n)
            check(sz, df, mode, values[v], addresses[a], counts[n], fixtures++);
    }
    free(g_xpt); free(g_xram);
    printf("PASS: %u STOS fixtures; full context/arena, widths, patterns, directions, modes, page splits, aliases and wrap\n", fixtures);
    return 0;
}
