#ifndef XK_QUERY_MEMORY_H
#define XK_QUERY_MEMORY_H
#include <stdint.h>

/* Exact memory transaction record only: no execution/FP/context replay, guest
 * address translation, owner admission, callbacks, allocation or global state.
 * The caller must instrument ALL accesses/helpers, hold exclusive access from
 * begin through finish and through each validate/replay, and invalidate before
 * any unknown access, callback or root/lifetime change. Direct image identity
 * is an additional caller admission condition; record its physical accesses.
 */
#define XV_QUERY_MEMORY_BLOCK_SIZE 64u
#define XV_QUERY_MEMORY_BLOCKS 128u
#define XV_QUERY_MEMORY_MAPPINGS 128u
#define XV_QUERY_MEMORY_HASH_SIZE 256u

typedef struct {
    unsigned char *arena;
    uint32_t usable_size; /* Excludes trash; nonzero and a multiple of64. */
    const uint32_t *pages;
    uint32_t page_count; /* Allocated readable entries; at most1<<20. */
} XvQueryMemoryView;

enum {
    XV_QM_EMPTY, XV_QM_RECORDING, XV_QM_FINISHED, XV_QM_INVALID
};
enum {
    XV_QM_BAD_STATE = 1u, XV_QM_BOUNDS = 2u, XV_QM_OVERFLOW = 4u,
    XV_QM_ROOTS = 8u, XV_QM_REMAP = 16u, XV_QM_UNKNOWN = 32u,
    XV_QM_CALLBACK = 64u
};
typedef struct {
    uint32_t offset;
    uint64_t reads, writes;
    unsigned char initial[XV_QUERY_MEMORY_BLOCK_SIZE];
    unsigned char final[XV_QUERY_MEMORY_BLOCK_SIZE];
} XvQueryMemoryBlock;
typedef struct {
    uint32_t guest_page, physical_page;
} XvQueryMemoryMapping;
typedef struct {
    XvQueryMemoryView view;
    unsigned status, reason, block_count, mapping_count;
    uint16_t hash[XV_QUERY_MEMORY_HASH_SIZE]; /* Block index plus one. */
    XvQueryMemoryBlock blocks[XV_QUERY_MEMORY_BLOCKS];
    XvQueryMemoryMapping mappings[XV_QUERY_MEMORY_MAPPINGS];
} XvQueryMemory;

/* begin can initialize uninitialized storage or restart any prior state.
 * All subsequent calls require begin. Arena, state and page-table storage must
 * be disjoint, stable, accessible host allocations (never borrowed guest
 * pointers). Null pages is allowed only with page_count=0. View roots/size/count
 * must match at finish/replay. No API may run concurrently with another access.
 */
int xv_query_memory_begin(XvQueryMemory *state, const XvQueryMemoryView *view);
void xv_query_memory_invalidate(XvQueryMemory *state, unsigned reason);
/* Register EVERY translated page for reads AND writes, including each side of
 * cross-page operations, before the access. physical_page must equal the live
 * entry, be4096-aligned, and cover a full page below usable_size. Common physical
 * offsets merge aliases. Caller supplies translations; this API does not read
 * guest addresses. Mapping-only notifications do not snapshot arena blocks.
 */
int xv_query_memory_mapping(XvQueryMemory *state, uint32_t guest_page,
                            uint32_t physical_page);
/* Physical accesses may be unaligned/cross-block. Notify read BEFORE reading;
 * notify write BEFORE storing, even for same-value stores. Zero-length access
 * is allowed at any offset <= usable_size. Failed recording does not prevent
 * the original operation: it permanently invalidates this record until begin.
 */
int xv_query_memory_read(XvQueryMemory *state, uint32_t offset, uint32_t length);
int xv_query_memory_write(XvQueryMemory *state, uint32_t offset, uint32_t length);
int xv_query_memory_finish(XvQueryMemory *state, const XvQueryMemoryView *view);
/* Rejection is read-only, including state: no replay writes occur until every
 * root, mapping and initial-read dependency has passed. Success patches only
 * written bytes; unrelated current bytes are preserved. Repeated replay is
 * allowed when the current dependencies still match. The caller handles
 * invalidation/restart after a validation miss; there is no implicit retry.
 */
int xv_query_memory_validate(const XvQueryMemory *state,
                             const XvQueryMemoryView *view);
int xv_query_memory_replay(const XvQueryMemory *state,
                           const XvQueryMemoryView *view);
#endif
