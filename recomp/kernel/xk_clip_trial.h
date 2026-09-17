/* Private process-start trial. Included only by the Present/Swap owner. */
#pragma once
#ifndef XV_CLIP_REGION_TRIAL
#define XV_CLIP_REGION_TRIAL 0
#endif
#if XV_CLIP_REGION_TRIAL != 0 && XV_CLIP_REGION_TRIAL != 1
#error "XV_CLIP_REGION_TRIAL must be 0 or 1"
#endif
#if XV_CLIP_REGION_TRIAL
#ifndef XV_LIGHT_QUERY_CENSUS
#error "XV_CLIP_REGION_TRIAL requires the joined recording-owner boundary"
#endif
#include "xk_clip_region.h"
#include "xk_light_census.h"
#include "../../runtime/xv_benchmark.h"
extern int xv_watch_n __attribute__((weak));
extern int xv_trace_funcs __attribute__((weak));

static void xv_clip_trial_present(xctx *c)
{
    static unsigned completed;
    if (__atomic_load_n(&completed, __ATOMIC_ACQUIRE)) return;
    /* This neither initializes the pool nor borrows a worker context. It
     * proves native owner, current guest fiber, drained queue and services. */
    if (xv_object_census_boundary(c) != XV_LC_OK) return;
    if (xv_native_clip_region_available()) {
        /* Existing controls own this mode, possibly across suspended helpers.
         * Never reset them as a side effect of a later Present. */
        __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
        xk_os_log("[clip-region-trial] existing controller preserved; enabled %d; no settings saved\n",
                  xv_native_clip_region_enabled());
        return;
    }
    if (XV_LIGHT_CENSUS_ON() ||
        __atomic_load_n(&xv_light_census_present_requested, __ATOMIC_ACQUIRE) ||
        xv_benchmark_active() ||
        (&xv_watch_n && xv_watch_n) || (&xv_trace_funcs && xv_trace_funcs)) return;
    if (!xv_clip_region_compatible()) {
        __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
        xk_os_log("[clip-region-trial] native clip/register configuration declined; no settings saved\n");
        return;
    }
    /* The uninitialized controller cannot have an active/suspended region.
     * Both calls enforce this same drained owner; no additional GPU wait. */
    xv_native_clip_region_init();
    xv_native_clip_region_override(1);
    __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
    xk_os_log("[clip-region-trial] joined recording-owner startup enabled 1; no settings saved\n");
}
#define XV_CLIP_TRIAL_PRESENT(c) xv_clip_trial_present(c)
#else
#define XV_CLIP_TRIAL_PRESENT(c) ((void)0)
#endif
