#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../runtime/xv_render_profile.h"

#ifndef XV_TEST_GUEST_TRACE
#define XV_TEST_GUEST_TRACE 0
#endif
const unsigned xv_guest_trace_enabled = XV_TEST_GUEST_TRACE;
static uint64_t now;
static unsigned clock_calls, reports, draw_calls, work_reports;
static char cutout_report[1024];
static char draw_report[1024];
static char report[1024], submit_report[1024], work_report[1024];
static unsigned scene_calls, target_reports;
static char target_report[10][1024];
static int simulated_draw(void) { draw_calls++; now+=400; return -17; }
static int simulated_end(void) { scene_calls++; now+=100; return -23; }
#ifndef XV_TEST_NO_CLOCK
uint64_t xk_os_monotonic_us(void) { clock_calls++; return now; }
#endif
void xv_logf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    char line[1024]; vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (strstr(line, "[render-time]") && strstr(line, "ms/frame")) {
        strcpy(report,line); reports++;
    }
    if (strstr(line,"[render-submit]")) strcpy(submit_report,line);
    if (strstr(line,"[render-work]")) { strcpy(work_report,line); work_reports++; }
    if(strstr(line,"[cutout-work]"))strcpy(cutout_report,line);
    if(strstr(line,"[render-draws]"))strcpy(draw_report,line);
    unsigned target, first, last;
    if (sscanf(line, "[render-target] mesh %u..%u target %u:", &first, &last, &target) == 3) {
        assert(target < 10); snprintf(target_report[target], sizeof target_report[target], "%s", line); target_reports++;
    }
}
static void step(unsigned us, enum xv_render_stage stage)
{
    now += us;
    xv_render_profile_stage(stage);
}
int main(int argc, char **argv)
{
    unsetenv("XV_RENDER_PROFILE");
    if (argc > 1) setenv("XV_RENDER_PROFILE", argv[1], 1);
    int on = argc > 1 ? atoi(argv[1]) != 0 : XV_TEST_GUEST_TRACE;
#ifdef XV_TEST_NO_CLOCK
    on = 0;
#endif
    /* Networking/shutdown render calls outside a pump frame are ignored. */
    xv_render_profile_stage(XV_RENDER_PREVIOUS_FINISH);
    xv_render_profile_end();
    xv_render_profile_scene_end(0, 0);
    assert(XV_RENDER_END(9, simulated_end()) == -23);
    assert(!target_reports); scene_calls = 0; now = 0;
    assert(!clock_calls && !reports);
    for (unsigned i = 0; i < 60; ++i) {
        xv_render_profile_begin(1000 + i);
        if(i%2)xv_render_profile_cutout(7); /* first clock origin is zero */
        assert(XV_RENDER_CALL(XV_RENDER_DRAW,simulated_draw())==-17);
        xv_render_profile_work(0x1234,3,i&1);
        uint64_t token=xv_render_profile_call_begin(); now+=200;
        xv_render_profile_call_end(XV_RENDER_VERTEX_UNIFORM,token);
        step(400, XV_RENDER_PREVIOUS_FINISH);
        assert(!xv_render_profile_call_begin()); /* Wait spans are not submission. */
        step(2000, XV_RENDER_SUBMIT);
        step(3000, XV_RENDER_TARGET_FINISH);
        step(4000, XV_RENDER_SUBMIT);
        step(5000, XV_RENDER_TARGET_FINISH);
        step(4000, XV_RENDER_SUBMIT);
        step(1000, XV_RENDER_DISPLAY_QUEUE);
        step(7000, XV_RENDER_SUBMIT);
        step(1000, XV_RENDER_FRAME_FINISH);
        step(6000, XV_RENDER_RETIRE);
        now += 8000;
        xv_render_profile_end();
        assert(reports == (unsigned)(on && i == 59));
        now += 900000; /* idle/pacing time must not enter any stage */
        xv_render_profile_stage(XV_RENDER_DISPLAY_QUEUE);
        xv_render_profile_end();
    }
    if (on) {
        assert(draw_calls==60 && work_reports==1);
        assert(strstr(cutout_report,"30 draws / 210 indices"));
        assert(strstr(work_report,"shader 00001234: draws 60 indices 180 no-alpha 30"));
        assert(strstr(report, "60 frames mesh 1000..1059:"));
        assert(strstr(report, "submit 11.000 previous-finish 2.000 target-finish 8.000 frame-finish 6.000 display-queue 7.000 retire 8.000 total 42.000"));
        assert(strstr(report, "finish-calls 60/120/60 queue-calls 60"));
        assert(strstr(submit_report,"begin 0.000 end 0.000 vertex-uniform 0.200 fragment-uniform 0.000 draw 0.400 shader 0.000 other 10.400"));
        assert(strstr(submit_report,"calls 0/0/60/0/60/0"));
        assert(strstr(draw_report,"60 sceGxmDraw calls / 60 frames; avg 1.00 min 1 max 1; frames >500 0 >800 0"));
        /* A subsequent all-submission window must not retain prior waits. */
        for (unsigned i = 0; i < 60; ++i) {
            xv_render_profile_begin(2000 + i); now += 3000;
            xv_render_profile_end();
        }
        assert(reports == 2);
        assert(strstr(cutout_report,"0 draws / 0 indices"));
        assert(strstr(report, "60 frames mesh 2000..2059: submit 3.000 previous-finish 0.000 target-finish 0.000 frame-finish 0.000 display-queue 0.000 retire 0.000 total 3.000"));
        assert(strstr(report, "finish-calls 0/0/0 queue-calls 0"));
        assert(strstr(submit_report,"draw 0.000 shader 0.000 other 3.000"));
        assert(work_reports==1);
        assert(strstr(draw_report,"0 sceGxmDraw calls / 60 frames; avg 0.00 min 0 max 0; frames >500 0 >800 0"));
        for (unsigned i = 0; i < 60; ++i) {
            xv_render_profile_begin(3000 + i);
            for (unsigned n = 0; n <= i % 2; ++n) assert(XV_RENDER_END(0, simulated_end()) == -23);
            assert(XV_RENDER_END(2, simulated_end()) == -23);
            xv_render_profile_end();
        }
        assert(scene_calls == 150 && target_reports == 2);
        assert(strstr(submit_report, "end 0.250") && strstr(submit_report, "calls 0/150/0/0/0/0"));
        assert(strstr(target_report[0], "scenes 90 max 2/frame end 0.150"));
        assert(strstr(target_report[2], "scenes 60 max 1/frame end 0.100"));
        for (unsigned i = 0; i < 60; ++i) {
            xv_render_profile_begin(4000 + i); now += 100;
            xv_render_profile_end();
        }
        assert(target_reports == 2); /* no prior target rows leak into an empty window */
        /* Preserve exact threshold boundaries and include API failures in the
         * invocation count. Empty frames must contribute to min and average. */
        const unsigned counts[] = {0, 500, 501, 800, 801};
        for (unsigned i = 0; i < 60; ++i) {
            xv_render_profile_begin(5000 + i);
            for (unsigned j = 0; j < counts[i % 5]; ++j)
                assert(XV_RENDER_CALL(XV_RENDER_DRAW, simulated_draw()) == -17);
            xv_render_profile_end();
        }
        assert(strstr(draw_report,"31224 sceGxmDraw calls / 60 frames; avg 520.40 min 0 max 801; frames >500 36 >800 12"));
        for (unsigned i = 0; i < 60; ++i) {
            xv_render_profile_begin(6000 + i);
            xv_render_profile_end();
        }
        assert(strstr(draw_report,"0 sceGxmDraw calls / 60 frames; avg 0.00 min 0 max 0; frames >500 0 >800 0"));
    } else assert(!clock_calls && !reports && !work_reports && !draw_report[0] && draw_calls==60);
    puts("PASS: render stages, draw count ranges/thresholds, failed draws, window reset, idle exclusion and disabled/no-clock behavior");
    return 0;
}
