/* Private process-start trial. Included only by the Present/Swap owner. */
#pragma once
#ifndef XV_POLYGON_EDGE_TRIAL
#define XV_POLYGON_EDGE_TRIAL 0
#endif
#if XV_POLYGON_EDGE_TRIAL != 0 && XV_POLYGON_EDGE_TRIAL != 1
#error "XV_POLYGON_EDGE_TRIAL must be 0 or 1"
#endif
#if XV_POLYGON_EDGE_TRIAL
#ifndef XV_LIGHT_QUERY_CENSUS
#error "XV_POLYGON_EDGE_TRIAL requires the joined recording-owner boundary"
#endif
#include "xk_polygon_edge.h"
#include "xk_light_census.h"
#include "../../runtime/xv_benchmark.h"
extern int xv_watch_n __attribute__((weak));
extern int xv_trace_funcs __attribute__((weak));

static void xv_polygon_edge_trial_present(xctx *c)
{
    static unsigned completed;
    if (__atomic_load_n(&completed, __ATOMIC_ACQUIRE)) return;
    /* Prove native owner/current fiber and an idle pool before examining
     * owner-local diagnostics. Never initialize the pool or borrow its ctx. */
    if (xv_object_census_boundary(c) != XV_LC_OK) return;
    if (xv_native_polygon_edge_available()) {
        /* Existing controls own OFF/ON and any suspended active helper.
         * There is deliberately no probe admission or counter drain here. */
        __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
        xk_os_log("[polygon-edge-trial] existing controller preserved; mode unchanged; no settings saved\n");
        return;
    }
    if (XV_LIGHT_CENSUS_ON() ||
        __atomic_load_n(&xv_light_census_present_requested, __ATOMIC_ACQUIRE) ||
        xv_benchmark_active() ||
        (&xv_watch_n && xv_watch_n) || (&xv_trace_funcs && xv_trace_funcs)) return;
    /* This helper has no environment compatibility switch. Its unavailable
     * controller cannot have active users; both controls enforce idle owner. */
    xv_native_polygon_edge_init();
    xv_native_polygon_edge_override(1);
    __atomic_store_n(&completed, 1, __ATOMIC_RELEASE);
    xk_os_log("[polygon-edge-trial] joined recording-owner startup enabled 1; no settings saved\n");
}
#define XV_POLYGON_EDGE_TRIAL_PRESENT(c) xv_polygon_edge_trial_present(c)
#else
#define XV_POLYGON_EDGE_TRIAL_PRESENT(c) ((void)0)
#endif
