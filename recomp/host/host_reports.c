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
static void *reporter(void *arg)
{
    (void)arg; unsigned last = xd3d_frame(); unsigned ticks = 0;
    for (;;) {
        usleep(100000);
        if (++ticks % 10 == 0) clock_trace();
        unsigned now = xd3d_frame(), delta = now - last;
        if (delta < 60) continue;
        last = now;
        fprintf(stderr, "[host] report at frame %u (%u frames)\n", now, delta);
        CALL(xv_owner_phase_report); CALL(xv_object_jobs_report); CALL(xv_quat_shared_report); CALL(xv_quat_cache_report);
        CALL(xv_model_palette_report); CALL(xv_model_hierarchy_report); CALL(xv_object_basis_report); CALL(xv_object_collect_report);
        CALL(xv_object_scan_report); CALL(xv_object_hierarchy_report); CALL(xv_object_hierarchy_assist_report); CALL(xv_visibility_pass_report);
        CALL(xv_subcluster_report); CALL(xv_portal_polygon_report); CALL(xv_clip_region_report); CALL(xv_query_reuse_report);
        CALL(xv_query_world_run_report); CALL(xv_native_math_report); CALL(xv_flare_report);
    }
    return 0;
}
void xv_host_reports_start(void) { pthread_t t; if (!pthread_create(&t, 0, reporter, 0)) pthread_detach(t); }
