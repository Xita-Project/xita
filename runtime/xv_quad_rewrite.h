/* xv_quad_rewrite.h - XV_REC_QUAD's QUADLIST/POLYGON -> triangle-list rewrite (runtime/xv_d3d.c rewrite_quads_opt).
 *
 * The original rewrite (xv_d3d.c rewrite_quads) and its bound scan (index_bounds) read uncached GPU memory on the
 * Vita: the sequential source list, and the written quad pool. This version reads nothing it writes:
 *   - a sequential QUADLIST (source == the 0,1,2,... list) returns the matching prefix of seq_quads, the immutable
 *     list quad k -> 4k, 4k+1, 4k+2, 4k, 4k+2, 4k+3, writes nothing, and advances *used exactly as the original;
 *   - any other source writes the same values to pool + *used, loading each source value once (a sequential source
 *     is its own position, never loaded), and tracks the bound on the way.
 * Same result, count, fill and index values as the original, and *bound == index_bounds() of those values.
 * capacity: indices in one list's pool (XV_QUAD_INDICES); seq_quads holds at least capacity / 6 * 6 indices. */
#pragma once
#include <stdint.h>

static inline int xv_quad_rewrite(int quadlist, uint32_t *count, const void **indices, unsigned *bound,
    uint16_t *pool, uint32_t *used, uint32_t capacity, const uint16_t *seq, const uint16_t *seq_quads)
{
    const uint16_t *src = (const uint16_t *)*indices;
    int sequential = src == seq;
    uint32_t n = *count, room = capacity - *used, out = 0;
    if (quadlist && sequential) {
        uint32_t quads = n / 4, fit = room / 6;
        if (quads > fit) quads = fit;
        if (!quads) return -1;
        *used += quads * 6;
        *indices = seq_quads; *count = quads * 6; *bound = quads * 4;
        return 0;
    }
    uint16_t *dst = pool + *used;
    unsigned top = 0;
#define XV_QSRC(i) (sequential ? (uint16_t)(i) : src[i])
    if (quadlist) {
        for (uint32_t q = 0; q + 4 <= n && out + 6 <= room; q += 4) {
            uint16_t a = XV_QSRC(q), b = XV_QSRC(q + 1), c = XV_QSRC(q + 2), d = XV_QSRC(q + 3);
            dst[out] = a; dst[out + 1] = b; dst[out + 2] = c; dst[out + 3] = a; dst[out + 4] = c; dst[out + 5] = d;
            out += 6;
            uint16_t m1 = a > b ? a : b, m2 = c > d ? c : d, m = m1 > m2 ? m1 : m2;
            if (m > top) top = m;
        }
    } else if (n >= 3 && room >= 3) {             /* POLYGON: a fan around the first vertex */
        uint16_t first = XV_QSRC(0), prev = XV_QSRC(1);
        top = first > prev ? first : prev;
        for (uint32_t k = 1; k + 1 < n && out + 3 <= room; ++k) {
            uint16_t next = XV_QSRC(k + 1);
            dst[out] = first; dst[out + 1] = prev; dst[out + 2] = next;
            out += 3; prev = next;
            if (next > top) top = next;
        }
    }
#undef XV_QSRC
    if (!out) return -1;
    *used += out;
    *indices = dst; *count = out; *bound = top + 1u;
    return 0;
}
