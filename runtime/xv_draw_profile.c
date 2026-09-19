#include <stdlib.h>
#include <string.h>
#include "xv_draw_profile.h"
#include "xv_log.h"

extern uint64_t xk_os_monotonic_us(void) __attribute__((weak));
/* Written/reported only by the guest draw-recording thread. Elapsed time can
 * include preemption; these are neither CPU-cycle counts nor GPU timings. */
static int enabled = -1;
static uint64_t elapsed[XV_DRAW_STAGE_COUNT];
static unsigned calls[XV_DRAW_STAGE_COUNT];
#ifndef XV_DRAW_PROFILE_DEFAULT
#define XV_DRAW_PROFILE_DEFAULT 1
#endif
#if XV_DRAW_PROFILE_DEFAULT != 0 && XV_DRAW_PROFILE_DEFAULT != 1
#error XV_DRAW_PROFILE_DEFAULT must be 0 or 1
#endif

uint64_t xv_draw_profile_begin(void)
{
    if (enabled < 0) {
        const char *e = getenv("XV_DRAW_PROFILE");
        enabled = (e ? atoi(e) != 0 : XV_DRAW_PROFILE_DEFAULT) && xk_os_monotonic_us;
    }
    return enabled ? xk_os_monotonic_us() + 1 : 0;
}
void xv_draw_profile_step(enum xv_draw_stage stage, uint64_t *stamp)
{
    if (!*stamp) return;
    uint64_t now = xk_os_monotonic_us() + 1;
    elapsed[stage] += now - *stamp;
    calls[stage]++;
    *stamp = now;
}
void xv_draw_profile_report(unsigned frames)
{
    if (!frames) return;
    extern void xv_vertex_prepare_report(unsigned) __attribute__((weak));
    if (xv_vertex_prepare_report) xv_vertex_prepare_report(frames);
    extern void xv_d3d_prep_cache_report(unsigned) __attribute__((weak));
    if (xv_d3d_prep_cache_report) xv_d3d_prep_cache_report(frames);
    /* Joined counters remain useful without per-draw clocks, and their
     * reporting/reset must not depend on whether detailed timing is enabled. */
    if (enabled <= 0) return;
    double scale = 1.0 / (1000.0 * frames);
    xv_logf("[draw-prep] %u frames %u draws: setup %.3f state %.3f indices %.3f program %.3f streams %.3f constants %.3f textures %.3f diagnostics %.3f ms/frame (elapsed caller time)\n",
        frames, calls[XV_DRAW_INDICES], elapsed[0]*scale, elapsed[1]*scale,
        elapsed[2]*scale, elapsed[3]*scale, elapsed[4]*scale, elapsed[5]*scale,
        elapsed[6]*scale, elapsed[7]*scale);
    memset(elapsed, 0, sizeof elapsed);
    memset(calls, 0, sizeof calls);
}
