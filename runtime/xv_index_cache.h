#ifndef XV_INDEX_CACHE_H
#define XV_INDEX_CACHE_H
#include "xv_vertex_refs.h"
#include "xv_bytes_equal.h"

/* Recording-owner scratch only. GPU commands retain the existing frame-ring
 * address, never this mirror or its metadata. Replacement cannot alter a draw
 * already recorded. A new frame must discard GPU pointers, even when CPU
 * mirrors and their exact-byte-derived metadata are retained. */
#define XV_INDEX_CACHE_ENTRIES 64u
#define XV_INDEX_CACHE_MAX 4096u
#define XV_INDEX_CACHE_MIN 64u
typedef struct {
    const void *source, *retained;
    unsigned count, vertices, references;
    xv_vertex_refs refs;
    uint16_t mirror[XV_INDEX_CACHE_MAX];
} xv_index_cache_entry;
typedef struct { xv_index_cache_entry entry[XV_INDEX_CACHE_ENTRIES]; } xv_index_cache;

static inline void xv_index_cache_reset(xv_index_cache *cache)
{
    if (cache) for (unsigned i=0;i<XV_INDEX_CACHE_ENTRIES;i++) {
        cache->entry[i].source=NULL;
        cache->entry[i].retained=NULL;
    }
}

static inline void xv_index_cache_new_frame(xv_index_cache *cache)
{
    if (cache) for (unsigned i=0;i<XV_INDEX_CACHE_ENTRIES;i++)
        cache->entry[i].retained=NULL;
}

static inline xv_index_cache_entry *xv_index_cache_select(xv_index_cache *cache,
    const void *source, unsigned count)
{
    if (!cache || !source || count<XV_INDEX_CACHE_MIN || count>XV_INDEX_CACHE_MAX)
        return NULL;
    uintptr_t p=(uintptr_t)source;
    unsigned hash=(unsigned)((p>>1)^(p>>12)^count);
    return &cache->entry[hash&(XV_INDEX_CACHE_ENTRIES-1u)];
}

/* Pointer/count select a candidate; exact current bytes establish reuse.
 * Coverage computed with another reference policy is deliberately not reused. */
static inline int xv_index_cache_match(const xv_index_cache_entry *entry,
    const void *source,unsigned count,unsigned references)
{
    return entry && entry->source==source && entry->count==count &&
        entry->references==references &&
        xv_bytes_equal(source,entry->mirror,count*sizeof(uint16_t));
}
#endif
