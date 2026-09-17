/* Actual benchmark request/poll, control/logging doubles. No measured arm runs. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_benchmark.c"
#ifndef XV_POLYGON_EDGE_TRIAL
#define XV_POLYGON_EDGE_TRIAL 0
#endif
#ifndef XV_CLIP_REGION_TRIAL
#define XV_CLIP_REGION_TRIAL 0
#endif
unsigned xv_light_census_present_requested;
static unsigned controls,cases;
static int edge_mode,clip_mode;
void xv_logf(const char *fmt,...) {(void)fmt;}
void xv_native_polygon_edge_init(void) {controls++;}
void xv_native_polygon_edge_override(int e) {controls++;edge_mode=e>0;}
int xv_native_polygon_edge_available(void) {controls++;return 1;}
void xv_native_clip_region_init(void) {controls++;}
void xv_native_clip_region_override(int e) {controls++;clip_mode=e>0;}
int xv_native_clip_region_available(void) {controls++;return 1;}
int xv_native_clip_region_enabled(void) {controls++;return clip_mode;}
static void reset(int initial) {
    memset(&b,0,sizeof b);status=0x7539;request_state=remote_ready=remote_kind=0;
    controls=0;edge_mode=clip_mode=initial;xv_light_census_present_requested=0;
}
static void rejected(unsigned kind) {
    unsigned q=request_state,r=remote_ready,k=remote_kind,s=status,census=xv_light_census_present_requested;
    unsigned char state[sizeof b];memcpy(state,&b,sizeof b);int edge=edge_mode,clip=clip_mode;
    assert(xv_benchmark_remote_request(kind)==-1);
    assert(q==request_state&&r==remote_ready&&k==remote_kind&&s==status&&census==xv_light_census_present_requested);
    assert(!memcmp(state,&b,sizeof b)&&edge==edge_mode&&clip==clip_mode&&!controls);cases++;
}
int main(void) {
    assert(XV_BENCH_POLYGON_EDGE==34&&XV_BENCH_CLIP_REGION==36);
    const unsigned kinds[]={XV_BENCH_POLYGON_EDGE,XV_BENCH_CLIP_REGION,XV_BENCH_TEXTURE_STATE};
    for(int initial=0;initial<2;initial++)for(unsigned i=0;i<3;i++) {
        unsigned kind=kinds[i];int deny=(kind==34&&XV_POLYGON_EDGE_TRIAL)||(kind==36&&XV_CLIP_REGION_TRIAL);
        reset(initial);rejected(kind);xv_benchmark_remote_poll(1);assert(remote_ready==1&&!controls);
        if(deny){rejected(kind);assert(!xv_benchmark_remote_busy());}
        else {
            assert(!xv_benchmark_remote_request(kind));assert(request_state==kind&&!b.request&&!remote_kind&&!controls);cases++;
            rejected(kind);xv_benchmark_remote_poll(1);
            assert(request_state==BENCH_RUNNING&&remote_kind==kind&&b.request==2&&!controls);cases++;
        }
        reset(initial);xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_TEXTURE_STATE));
        rejected(kind);xv_benchmark_remote_poll(1);assert(remote_kind==XV_BENCH_TEXTURE_STATE&&b.request==2&&!controls);
        rejected(kind);rejected(0);rejected(44);rejected(~0u);
        reset(initial);xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_TEXTURE_STATE));
        xv_benchmark_remote_poll(0);assert(!request_state&&!remote_ready&&!remote_kind&&!b.request&&!controls);rejected(kind);
        assert(edge_mode==initial&&clip_mode==initial);
    }
    printf("PASS polygon=%d clip=%d: %u request/poll checks; rejected queues and modes unchanged\n",XV_POLYGON_EDGE_TRIAL,XV_CLIP_REGION_TRIAL,cases);
    return 0;
}
