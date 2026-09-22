/* host_reports.c - the Vita UI overlay calls the kernel's 60-frame report functions; the host harness has no
 * overlay, so a thread does it every 60 presented frames. Weak references: absent features are skipped. */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
unsigned xd3d_frame(void);
#define R(name) void name(unsigned) __attribute__((weak));
R(xv_owner_phase_report) R(xv_object_jobs_report) R(xv_quat_shared_report) R(xv_quat_cache_report)
R(xv_model_palette_report) R(xv_model_hierarchy_report) R(xv_object_basis_report) R(xv_object_collect_report)
R(xv_object_scan_report) R(xv_object_hierarchy_report) R(xv_object_hierarchy_assist_report) R(xv_visibility_pass_report)
R(xv_subcluster_report) R(xv_portal_polygon_report) R(xv_clip_region_report) R(xv_query_reuse_report)
R(xv_query_world_run_report) R(xv_native_math_report) R(xv_flare_report)
#define CALL(name) do { if (name) name(delta); } while (0)
void xk_wait_stats_request(void) __attribute__((weak));
void xv_host_sample_dump(unsigned) __attribute__((weak)); void xv_host_sample_start(void) __attribute__((weak));   /* [wait] dump (blocking sites of every guest thread) at the next yield */
#include <stdint.h>
extern uint8_t *g_img_base; extern unsigned xv_n_kicks, xv_n_fires;
/* XV_HOST_CLOCK_TRACE=1: every second, Halo's vblank clock (64-bit count at 0x1F8C80) against the frame-end
 * wait target (0x2E3660), plus the kernel's fire/kick counters - shows whether the pacing loop can ever exit. */
static void clock_trace(void)
{
    static int on = -1; if (on < 0) { const char *e = getenv("XV_HOST_CLOCK_TRACE"); on = e ? atoi(e) : 0; }
    if (!on || !g_img_base) return;
    uint64_t cnt, tgt; memcpy(&cnt, g_img_base + 0x1F8C80u, 8); memcpy(&tgt, g_img_base + 0x2E3660u, 8);
    fprintf(stderr, "[host] clock: vblank %llu target %llu frame %u fires %u kicks %u\n",
            (unsigned long long)cnt, (unsigned long long)tgt, xd3d_frame(), xv_n_fires, xv_n_kicks);
}
/* The 60-frame reports run on the OWNER at the device present (xd3d_r_present, host copy in xd3d.c), where the tick's
 * object jobs are drained and, in overlap mode 2, the scene has been joined: several report reads are owner-only
 * invariants (owner_drained, xv_object_math_report_check) and aborted the Pi soaks when a thread ran them. */
void xv_host_reports_present(unsigned now)
{
    static unsigned last; unsigned delta = now - last;
    if (delta < 60) return;
    last = now;
    fprintf(stderr, "[host] report at frame %u (%u frames)\n", now, delta);
    if (xk_wait_stats_request) xk_wait_stats_request();
    if (xv_host_sample_dump) xv_host_sample_dump(now);
    CALL(xv_owner_phase_report); CALL(xv_object_jobs_report); CALL(xv_quat_shared_report); CALL(xv_quat_cache_report);
    CALL(xv_model_palette_report); CALL(xv_model_hierarchy_report); CALL(xv_object_basis_report); CALL(xv_object_collect_report);
    CALL(xv_object_scan_report); CALL(xv_object_hierarchy_report); CALL(xv_object_hierarchy_assist_report); CALL(xv_visibility_pass_report);
    CALL(xv_subcluster_report); CALL(xv_portal_polygon_report); CALL(xv_clip_region_report); CALL(xv_query_reuse_report);
    CALL(xv_query_world_run_report); CALL(xv_native_math_report); CALL(xv_flare_report);
}
static void *reporter(void *arg)
{
    (void)arg;
    for (;;) { usleep(1000000); clock_trace(); }
    return 0;
}
void xv_host_reports_start(void) { if (xv_host_sample_start) xv_host_sample_start(); pthread_t t; if (!pthread_create(&t, 0, reporter, 0)) pthread_detach(t); }
