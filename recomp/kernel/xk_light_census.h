/* Halo3925 original-execution census. OFF by default; no query dispatch. */
#pragma once
#include "../xv_x86rt.h"
#ifdef XV_LIGHT_QUERY_CENSUS
#ifndef XV_EXPERIMENTAL_OBJECT_JOBS
#error XV_LIGHT_QUERY_CENSUS requires the object backend owner registration
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum xv_light_census_reason {
    XV_LC_OK, XV_LC_UNINITIALIZED, XV_LC_WORKER, XV_LC_NATIVE_OWNER,
    XV_LC_CONTEXT, XV_LC_MARKED, XV_LC_QUEUE, XV_LC_GUARD,
    XV_LC_NESTED, XV_LC_SUFFIX, XV_LC_MEMORY, XV_LC_OBJECT,
    XV_LC_COUNT, XV_LC_LIGHT, XV_LC_DUPLICATE, XV_LC_LOCALE,
    XV_LC_GEOMETRY, XV_LC_ORDER, XV_LC_EPOCH, XV_LC_WRAP,
    XV_LC_FUTURE_STAMP, XV_LC_EXIT, XV_LC_HANDOFF, XV_LC_STOP,
    XV_LC_BSP_SWITCH, XV_LC_MAP_END, XV_LC_LIST_RESET, XV_LC_LIGHT_DELETE,
    XV_LC_REASONS
};
enum { XV_LC_MAX_LIGHTS=8, XV_LC_SOURCES=16 };
/* Original query prefix only: count at 566DE, before the 64-result clamp and
 * allocator tail. Native-thread-owned rows are read/reset only after joining. */
enum { XV_QW_LANES=3, XV_QW_COUNTS=10, XV_QW_COSTS=12 };
typedef struct xv_query_work_stats {
    uint64_t entered, finished, invalid, depth_one, nested, unknown_depth;
    uint64_t counts[XV_QW_COUNTS], cost[XV_QW_COUNTS], max_cost[XV_QW_COUNTS];
    uint64_t single_counts[XV_QW_COUNTS], single_cost[XV_QW_COUNTS];
    uint64_t budgets[XV_QW_COSTS];
} xv_query_work_stats;
typedef struct xv_query_work_token {
    unsigned lane, depth;
    uint32_t sp;
    int budget;
} xv_query_work_token;
typedef struct xv_light_census_stats {
    uint64_t entries, opened, completed, cancelled, candidates;
    uint64_t queries, traversals, removals, multi_groups, multi_traversals;
    uint64_t candidate_multi_groups, candidate_multi_traversals;
    uint64_t guest_reads, guest_bytes, orphan_queries, orphan_removals;
    uint64_t entry_reads, entry_bytes, max_entry_bytes, max_group_bytes;
    uint32_t group_storage_bytes, stats_storage_bytes;
    uint64_t tag_count[10], query_count[10], traversal_count[10], mode[4];
    uint64_t candidate_traversal_count[10], entry_budget_nonpositive;
    struct { uint32_t pc; uint64_t groups; } sources[XV_LC_SOURCES+1];
    /* Admission counters are per hook, including OK. In particular worker
     * entry declines are groups, worker query declines are original calls;
     * they must never be mixed when interpreting absent owner workload. */
    uint32_t admission[3][XV_LC_GUARD+1]; /* entry, query, removal */
    uint32_t declined[XV_LC_REASONS]; /* all gates + first structural failure */
    xv_query_work_stats query_work[XV_QW_LANES];
} xv_light_census_stats;
extern unsigned xv_light_census_enabled;
/* Armed only by owner-side benchmark37 request consumption, not by the network.
 * A capability carries the exact Present/Swap HLE context to the renderer. */
extern unsigned xv_light_census_present_requested;
/* begin returns a cleanup token even for a rejected owner context. Only
 * current() returns an admitted capability; it rechecks the live boundary. */
unsigned xv_light_census_present_begin(xctx *);
void xv_light_census_present_end(unsigned *);
xctx *xv_light_census_present_current(void);
static inline void xv_light_census_present_cleanup(unsigned *token)
{if(*token)xv_light_census_present_end(token);}
#define XV_LIGHT_CENSUS_PRESENT_SCOPE(c) \
    unsigned xv_light_present_token_ __attribute__((cleanup(xv_light_census_present_cleanup))) = \
        __atomic_load_n(&xv_light_census_present_requested,__ATOMIC_RELAXED) ? xv_light_census_present_begin(c) : 0
unsigned xv_object_census_admit(const xctx *);
int xv_object_census_is_owner(void);
unsigned xv_object_census_boundary(const xctx *);
/* Returns owner=1 or actual worker=2/3, after native/context verification.
 * Reads no guest memory. Depth zero means unavailable, not unlocked work. */
unsigned xv_object_query_work_lane(const xctx *,int guard,unsigned *depth);
xv_query_work_token xv_query_work_begin(xctx *,int guard);
void xv_query_work_end(xctx *,xv_query_work_token *,int guard);
#define XV_QUERY_WORK_BEGIN(c,guard) \
    xv_query_work_token xv_query_work_ = {0}; \
    do {if(XV_LIGHT_CENSUS_ON())xv_query_work_=xv_query_work_begin(c,guard);}while(0)
#define XV_QUERY_WORK_END(c,guard) \
    do {if(xv_query_work_.lane)xv_query_work_end(c,&xv_query_work_,guard);}while(0)
/* Current native owner/live fiber, no queued/running service or logical guard.
 * Controller contract: toggle only at a drained frame boundary with no open
 * generated/math scopes. OFF scopes intentionally do not inspect native IDs.
 * The active observer checks scopes; it cannot reconstruct pre-enable scopes.
 * Neither API initializes the worker backend or dispatches pending jobs. */
int xv_light_census_control(xctx *,int enabled,int reset);
int xv_light_census_take(xctx *,xv_light_census_stats *,int reset);
unsigned xv_light_census_begin(xctx *);
void xv_light_census_end(xctx *,unsigned);
void xv_light_census_cleanup(unsigned *);
static inline void xv_light_census_scope_cleanup(unsigned *token)
{if(*token)xv_light_census_cleanup(token);}
void xv_light_census_suffix(xctx *);
void xv_light_census_query(xctx *);
void xv_light_census_remove(xctx *);
void xv_light_census_cancel(xctx *,unsigned reason);
const char *xv_light_census_reason_name(unsigned);
#define XV_LIGHT_CENSUS_ON() __atomic_load_n(&xv_light_census_enabled,__ATOMIC_RELAXED)
#define XV_LIGHT_CENSUS_CANCEL(c,r) do { if(XV_LIGHT_CENSUS_ON()||__atomic_load_n(&xv_light_census_present_requested,__ATOMIC_RELAXED)) xv_light_census_cancel(c,r); } while(0)
#define XV_LIGHT_CENSUS_SCOPE(c) \
    unsigned xv_light_census_token_ __attribute__((cleanup(xv_light_census_scope_cleanup))) = \
        XV_LIGHT_CENSUS_ON() ? xv_light_census_begin(c) : 0
#define XV_LIGHT_CENSUS_SUFFIX(c) \
    unsigned xv_light_census_token_ = 0; \
    do {if(XV_LIGHT_CENSUS_ON())xv_light_census_suffix(c);}while(0)
#define XV_LIGHT_CENSUS_END(c) do {if(xv_light_census_token_)xv_light_census_end(c,xv_light_census_token_);}while(0)
#define XV_LIGHT_CENSUS_QUERY(c) do {if(XV_LIGHT_CENSUS_ON())xv_light_census_query(c);}while(0)
#define XV_LIGHT_CENSUS_REMOVE(c) do {if(XV_LIGHT_CENSUS_ON())xv_light_census_remove(c);}while(0)
#ifdef __cplusplus
}
#endif
#else
#define XV_LIGHT_CENSUS_PRESENT_SCOPE(c) ((void)0)
#define XV_LIGHT_CENSUS_CANCEL(c,r) ((void)0)
#endif
