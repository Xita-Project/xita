/* XV_REC_QUAD (runtime/xv_quad_rewrite.h) against a verbatim copy of the original rewrite (runtime/xv_d3d.c
 * rewrite_quads) and its bound scan (index_bounds): result, count, pool fill, index values, bound, bytes outside
 * the written range, for QUADLIST and POLYGON, sequential and guest sources, every pool fill from empty to full. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../runtime/xv_quad_rewrite.h"

#define CAP 49152u        /* XV_QUAD_INDICES */
#define SEQ 65536u        /* XV_SEQ_INDICES */
static uint16_t seq[SEQ], seq_quads[CAP / 6 * 6];

/* runtime/xv_d3d.c rewrite_quads, with the pool and its fill as parameters */
static int rewrite_old(int quadlist, uint32_t *count, const void **indices, uint16_t *pool, uint32_t *used)
{
    const uint16_t *src = (const uint16_t *)*indices;
    uint32_t n = *count, out = 0;
    uint16_t *dst = pool + *used;
    uint32_t room = CAP - *used;
    if (quadlist) {
        for (uint32_t q = 0; q + 4 <= n && out + 6 <= room; q += 4) {
            dst[out++] = src[q]; dst[out++] = src[q + 1]; dst[out++] = src[q + 2];
            dst[out++] = src[q]; dst[out++] = src[q + 2]; dst[out++] = src[q + 3];
        }
    } else {
        for (uint32_t k = 1; k + 1 < n && out + 3 <= room; ++k) { dst[out++] = src[0]; dst[out++] = src[k]; dst[out++] = src[k + 1]; }
    }
    if (!out) return -1;
    *used += out;
    *indices = dst; *count = out;
    return 0;
}
static unsigned index_bounds(const uint16_t *indices, unsigned count)   /* runtime/xv_d3d.c */
{
    unsigned maximum = 0;
    for (unsigned i = 0; i < count; i++)
        if (indices[i] > maximum) maximum = indices[i];
    return count ? maximum + 1 : 0;
}

static uint32_t rng = 777;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    for (uint32_t i = 0; i < SEQ; i++) seq[i] = (uint16_t)i;
    for (uint32_t k = 0; k < CAP / 6; k++) {
        uint16_t v = (uint16_t)(4 * k), *q = seq_quads + 6 * k;
        q[0] = v; q[1] = v + 1; q[2] = v + 2; q[3] = v; q[4] = v + 2; q[5] = v + 3;
    }
    static uint16_t pool_old[CAP + 64], pool_new[CAP + 64], guest[SEQ];
    const uint32_t counts[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 16, 20, 63, 64, 80, 540, 1000, 4097, 32768, 65536};
    const uint32_t fills[] = {0, 1, 5, 6, 7, 100, CAP / 2, CAP - 7, CAP - 6, CAP - 5, CAP - 3, CAP - 2, CAP - 1, CAP};
    unsigned long cases = 0, seq_shared = 0;
    for (unsigned ci = 0; ci < sizeof counts / sizeof *counts; ci++)
        for (unsigned fi = 0; fi < sizeof fills / sizeof *fills; fi++)
            for (int quadlist = 0; quadlist < 2; quadlist++)
                for (int source = 0; source < 5; source++) {
                    uint32_t n = counts[ci], fill = fills[fi];
                    const uint16_t *src = seq;
                    if (source) {
                        for (uint32_t i = 0; i < n; i++)
                            guest[i] = source == 1 ? (uint16_t)rnd() : source == 2 ? (uint16_t)(rnd() % 300) :
                                       source == 3 ? (uint16_t)(65535 - rnd() % 4) : 0;
                        src = guest;
                    }
                    memset(pool_old, 0x5A, sizeof pool_old); memset(pool_new, 0x5A, sizeof pool_new);
                    uint32_t c_old = n, c_new = n, u_old = fill, u_new = fill; unsigned bound = 12345;
                    const void *i_old = src, *i_new = src;
                    int r_old = rewrite_old(quadlist, &c_old, &i_old, pool_old, &u_old);
                    int r_new = xv_quad_rewrite(quadlist, &c_new, &i_new, &bound, pool_new, &u_new, CAP, seq, seq_quads);
                    assert(r_old == r_new);
                    assert(u_old == u_new);
                    if (!r_old) {
                        assert(c_old == c_new);
                        assert(i_old == pool_old + fill);
                        assert(!memcmp(i_old, i_new, c_old * 2));
                        assert(bound == index_bounds(i_old, c_old));
                        if (i_new == seq_quads) { assert(quadlist && src == seq); seq_shared++;
                            for (unsigned i = 0; i < CAP + 64; i++) assert(pool_new[i] == 0x5A5A); }
                        else { assert(i_new == pool_new + fill);
                            assert(!memcmp(pool_old, pool_new, sizeof pool_old)); }
                    } else {
                        assert(c_old == n && c_new == n && i_old == src && i_new == src);
                        assert(!memcmp(pool_old, pool_new, sizeof pool_old));
                    }
                    cases++;
                }
    printf("rec_quad_test: %lu cases (%lu from the shared sequential list) pass\n", cases, seq_shared);
    return 0;
}
