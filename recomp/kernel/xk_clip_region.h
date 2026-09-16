#ifndef XV_CLIP_REGION_H
#define XV_CLIP_REGION_H
#include "xv_x86rt.h"

/* Optional build, default OFF. Bind on the eventual recording native thread
 * with all workers joined/idle. Controls and reporting require that same
 * drained owner, including no suspended region. Negative restores OFF. */
void xv_native_clip_region_init(void);
int xv_native_clip_region_available(void);
/* Atomic read-only state, for mutually exclusive diagnostic admission. */
int xv_native_clip_region_enabled(void);
void xv_native_clip_region_override(int enabled);

/* Accumulated completed work, not time. Unsigned interval counters wrap.
 * No guest loads, clocks or per-call thread queries are used for accounting. */
typedef struct xv_clip_region_work {
    unsigned regions, planes, clips, input_vertices, capacity_failures;
    unsigned max_clips;
} xv_clip_region_work;
void xv_clip_region_read_work(xv_clip_region_work *out);
void xv_clip_region_report(unsigned frames);

/* Only valid after the original first B7F50/B7F53 positive test. Zero preserves
 * context/memory for fallback. Interior guest entry points never call this.
 * markers says the calling unit was generated with --trace-funcs. */
int xv_math_clip_region(xctx *c, int markers);
int xv_clip_region_begin(void);
void xv_clip_region_end(const xv_clip_region_work *work);

/* Generated current-clip bridge: owner initializes config while drained;
 * reads thereafter are immutable except the existing drained register override.
 * Account exactly once per clip, under its original math guard. */
int xv_clip_region_compatible(void);
void xv_clip_region_account(void);
#endif
