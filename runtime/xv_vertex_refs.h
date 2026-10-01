#ifndef XV_VERTEX_REFS_H
#define XV_VERTEX_REFS_H
#include <stdint.h>
#include <string.h>

/* Eight consecutive vertices per bit. The mask is built from the same cached
 * index chunks published to the GPU. It is recording-thread scratch, not guest
 * memory or a hash; every fetched vertex is covered. */
typedef struct {
    uint32_t bits[256];
    unsigned vertices, groups;
} xv_vertex_refs;

static inline void xv_vertex_refs_clear(xv_vertex_refs *r)
{ memset(r, 0, sizeof *r); }

static inline void xv_vertex_refs_add(xv_vertex_refs *r, uint16_t index)
{
    unsigned group = index >> 3, word = group >> 5;
    uint32_t bit = 1u << (group & 31);
    if (!(r->bits[word] & bit)) { r->bits[word] |= bit; r->groups++; }
    if ((unsigned)index + 1 > r->vertices) r->vertices = (unsigned)index + 1;
}

static inline int xv_vertex_refs_sparse(const xv_vertex_refs *r, unsigned bytes,
                                         unsigned stride)
{
    return r && stride && r->groups && r->groups <= 8192 &&
           r->vertices >= 512 && r->vertices <= 65536 && r->vertices <= UINT32_MAX / stride &&
           r->vertices * stride == bytes && r->groups * 16u < r->vertices;
}

/* Equality is exact over complete records in all referenced groups. Unread
 * records may differ; subsequent draws must validate their own reference set.
 * Coalescing groups bounds callback overhead while preserving every fetch. */
static inline int xv_vertex_refs_equal(const xv_vertex_refs *r, unsigned stride,
    const void *left, const void *right,
    int (*equal)(const void *, const void *, unsigned), uint64_t *bytes, unsigned *runs)
{
    unsigned group = 0, limit = (r->vertices + 7u) >> 3;
    while (group < limit) {
        uint32_t word = r->bits[group >> 5] & (UINT32_MAX << (group & 31));
        if (!word) { group = (group | 31u) + 1u; continue; }
        unsigned first = (group & ~31u) + (unsigned)__builtin_ctz(word);
        unsigned end = first + 1;
        while (end < limit && (r->bits[end >> 5] & (1u << (end & 31)))) end++;
        unsigned last_vertex = end * 8u;
        if (last_vertex > r->vertices) last_vertex = r->vertices;
        unsigned offset = first * 8u * stride, length = (last_vertex - first * 8u) * stride;
        *bytes += length; (*runs)++;
        if (!equal((const uint8_t *)left + offset, (const uint8_t *)right + offset, length)) return 0;
        group = end;
    }
    return 1;
}
#endif
