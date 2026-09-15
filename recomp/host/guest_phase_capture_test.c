/* Production benchmark + phase accounting, including the Present call order. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xv_phase.h"
#include "../../runtime/xv_benchmark.h"
static uint64_t now;
static unsigned clock_reads, reports, capture_on, capture_off;
uint64_t xk_os_monotonic_us(void) { clock_reads++; return now; }
void xk_os_log(const char *fmt, ...)
{
    char text[2048];va_list ap;va_start(ap,fmt);
    vsnprintf(text,sizeof text,fmt,ap);va_end(ap);
    if(strstr(text,"[guest-phase] 60 frames")) {
        assert(strstr(text,"dropped 0 invalid 0"));reports++;
    }
    if(strstr(text,"[guest-phase-capture] on"))capture_on++;
    if(strstr(text,"[guest-phase-capture] off"))capture_off++;
}
void xv_logf(const char *fmt, ...) { (void)fmt; }
#include "../../runtime/xv_benchmark.c"
#include "../xv_phase.c"
const xv_phase_target xv_phase_targets[]={{1,"child"}};
const unsigned xv_phase_target_count=1;
void xv_benchmark_optimizations(int enabled)
{
    assert(xv_benchmark_compare_guest_phases());
    xv_phase_capture_override(enabled);
}
static float view[6]={1,2,3,0,1,0};
static unsigned frame;
static int context;
static void present(int control)
{
    xv_phase_frame(++frame);
    unsigned next=xv_benchmark_step(now,360,control,view);
    if(next) {assert(next==360);xv_benchmark_applied(now,next);}
}
static void one_frame(void)
{
    XV_PHASE_SCOPE(&context,0);
    now+=1000;
    present(1);
}
static void start(void)
{
    xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_remote_request(XV_BENCH_GUEST_PHASES));
    assert(xv_benchmark_remote_request(XV_BENCH_FLARE)<0);
    xv_benchmark_remote_poll(1);present(1);
    assert(xv_benchmark_active()&&!xv_phase_enabled);
}
int main(void)
{
    setenv("XV_PHASE_TIMING","0",1);xv_phase_init();
    assert(xv_phase_capture_available());start();
    for(unsigned i=0;i<540;i++)one_frame();
    assert(reports==3 && capture_on==1 && capture_off==3);
    assert(!xv_benchmark_active()&&!xv_phase_enabled&&!invalid&&!dropped);
    unsigned clocks=clock_reads;
    for(unsigned i=0;i<60;i++)one_frame();
    assert(clocks==clock_reads && reports==3);
    /* Cancel/loss of first-person control in the middle of a report. Both
     * must invalidate live scopes and discard, rather than emit, that window. */
    for(unsigned lost=0;lost<2;lost++) {
        start();for(unsigned i=0;i<270;i++)one_frame();
        assert(xv_phase_enabled && frames==30);
        if(!lost)xv_benchmark_compare_toggle();
        present(!lost);
        assert(!xv_benchmark_active()&&!xv_phase_enabled&&!frames);
    }
    assert(reports==5 && capture_on==3 && !invalid&&!dropped);
    puts("PASS: remote phase capture, 3 complete windows, owner-boundary cleanup, cancellation/lost view and zero off-state timer reads");
}
