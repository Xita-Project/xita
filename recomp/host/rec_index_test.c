/* XV_REC_INDEX single-read index copies against the original routines: destination and mirror bytes, bounds,
 * the complete coverage structure, and the fused hit copy (equal, and a difference at every position). Runs with
 * and without NEON (build for x86-64 and armhf). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../runtime/xv_index_copy.h"

static uint32_t rng = 12345;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void fill(uint8_t *src, unsigned n, unsigned pattern)
{
    unsigned base = next() & 0xFFFF;
    for (unsigned i = 0; i < n; i++) {
        uint16_t v;
        switch (pattern) {
        case 0: v = 0; break;
        case 1: v = 65535; break;
        case 2: v = (uint16_t)i; break;
        case 3: v = (uint16_t)next(); break;
        case 4: v = (uint16_t)(base + (next() % 24)); break;                 /* one cluster: mostly one word */
        case 5: v = (uint16_t)((i & 1) ? 300 + (next() & 7) : 20 + (next() & 7)); break;  /* two alternating words */
        case 6: v = (uint16_t)(65535 - (next() % 300)); break;               /* the top of the range */
        default: v = (uint16_t)((i / 3) * 2 + (i % 3)); break;               /* strip-like */
        }
        memcpy(src + 2 * i, &v, 2);
    }
}

int main(void)
{
    const unsigned counts[] = {1, 2, 3, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 256, 257, 300, 511, 512, 513,
                               1000, 4095, 4096, 4097, 65536, 131071};
    unsigned cases = 0;
    for (unsigned c = 0; c < sizeof counts / sizeof *counts; c++)
        for (unsigned pattern = 0; pattern < 8; pattern++)
            for (unsigned offset = 0; offset < 2; offset++) {
                unsigned n = counts[c];
                uint8_t *alloc = malloc(n * 2 + 8), *src = alloc + offset;
                uint8_t *d_old = malloc(n * 2 + 32), *d_new = malloc(n * 2 + 32);
                uint16_t *m_new = malloc(n * 2 + 32);
                assert(alloc && d_old && d_new && m_new);
                fill(src, n, pattern);
                static xv_vertex_refs r_old, r_neon, r_new, r_mir;
                memset(&r_old, 0x5A, sizeof r_old); memset(&r_neon, 0x5A, sizeof r_neon);
                memset(&r_new, 0xA5, sizeof r_new); memset(&r_mir, 0xA5, sizeof r_mir);
                /* coverage: scalar, NEON (when built) and the single-read variant, with and without mirror */
                memset(d_old, 0x11, n * 2 + 32); memset(d_new, 0x22, n * 2 + 32);
                unsigned v_old = xv_index_copy_reference_bounds(d_old + 16, src, n, &r_old);
                unsigned v_neon = xv_index_copy_reference_bounds_neon(d_old + 16, src, n, &r_neon);
                unsigned v_new = xv_index_copy2_reference_bounds(d_new + 16, NULL, src, n, &r_new);
                assert(v_old == v_neon && v_old == v_new);
                assert(!memcmp(&r_old, &r_neon, sizeof r_old) && !memcmp(&r_old, &r_new, sizeof r_old));
                assert(!memcmp(d_old + 16, d_new + 16, n * 2) && !memcmp(d_new + 16, src, n * 2));
                for (unsigned i = 0; i < 16; i++) assert(d_new[i] == 0x22 && d_new[16 + n * 2 + i] == 0x22);
                memset(d_new, 0x33, n * 2 + 32); memset(m_new, 0x44, n * 2 + 32);
                unsigned v_mir = xv_index_copy2_reference_bounds(d_new + 16, m_new, src, n, &r_mir);
                assert(v_mir == v_old && !memcmp(&r_mir, &r_old, sizeof r_old));
                assert(!memcmp(d_new + 16, src, n * 2) && !memcmp(m_new, src, n * 2));
                assert(((uint8_t *)m_new)[n * 2] == 0x44 && d_new[16 + n * 2] == 0x33);
                /* bounds only */
                memset(d_old, 0x11, n * 2 + 32); memset(d_new, 0x22, n * 2 + 32); memset(m_new, 0x44, n * 2 + 32);
                unsigned b_old = xv_index_copy_bounds(d_old + 16, src, n);
                unsigned b_neon = xv_index_copy_bounds_neon(d_old + 16, src, n);
                unsigned b_new = xv_index_copy2_bounds(d_new + 16, NULL, src, n);
                unsigned b_mir = xv_index_copy2_bounds(d_new + 16, m_new, src, n);
                assert(b_old == b_neon && b_old == b_new && b_old == b_mir && b_old == v_old);
                assert(!memcmp(d_new + 16, src, n * 2) && !memcmp(m_new, src, n * 2) && d_new[16 + n * 2] == 0x22);
                /* fused hit copy: equal mirror, then one differing index at several positions */
                memcpy(m_new, src, n * 2);
                memset(d_new, 0x55, n * 2 + 32);
                assert(xv_index_copy_if_equal(d_new + 16, src, m_new, n) == 1);
                assert(!memcmp(d_new + 16, src, n * 2) && d_new[16 + n * 2] == 0x55 && d_new[15] == 0x55);
                unsigned positions[] = {0, n / 3, n / 2, n - 1};
                for (unsigned k = 0; k < 4; k++) {
                    uint16_t *m = m_new; unsigned at = positions[k];
                    m[at] ^= (uint16_t)(1u << (k * 4));
                    assert(xv_index_copy_if_equal(d_new + 16, src, m_new, n) == 0);
                    m[at] ^= (uint16_t)(1u << (k * 4));
                }
                cases++;
                free(alloc); free(d_old); free(d_new); free(m_new);
            }
#if defined(__ARM_NEON)
    const char *kind = "NEON";
#else
    const char *kind = "scalar";
#endif
    printf("PASS: %u index lists (%s): single-read copies equal the original bytes, bounds and coverage; fused hit copy\n", cases, kind);
    return 0;
}
