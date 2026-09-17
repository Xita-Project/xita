#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../runtime/xv_benchmark.h"
static int mode,available=1,legacy_world=1;
static unsigned calls,values[16];
#include "../../runtime/xv_benchmark.c"
#ifndef TEST_NO_OBJECT_COLLECT
int xv_object_collect_available(void) {return available;}
int xv_object_collect_enabled(void) {return mode;}
void xv_object_collect_override(int value) { mode=value; }
#endif
void xv_logf(const char *fmt,...) {(void)fmt;}
void xv_benchmark_optimizations(int enabled)
{
    assert(xv_benchmark_compare_object_collect());assert(enabled==0 || enabled==1);
    assert(calls<16);values[calls++]=enabled;mode=enabled;
    assert(legacy_world==1); /* The new comparison must not change the old mode. */
}
static const float view[6]={1,2,3,0,1,0};
static void trial(unsigned initial,unsigned stop)
{
    calls=0;mode=initial;available=1;xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT));
    assert(xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT)<0);
    xv_benchmark_remote_poll(1);
    uint64_t now=1;unsigned height=xv_benchmark_step(now,544,1,view);
    assert(height==544 && mode==0 && calls==1);xv_benchmark_applied(now,height);
    for(unsigned i=0;i<1000 && xv_benchmark_active();i++) {
        now+=50000;
        if(i==210 && stop==1)xv_benchmark_compare_toggle();
        height=xv_benchmark_step(now,544,!(i==210 && stop==2),view);
        if(height)xv_benchmark_applied(now,height);
    }
    assert(!xv_benchmark_active() && !xv_benchmark_remote_busy() && mode==(int)initial);
    assert(values[0]==0 && values[1]==1 && values[calls-1]==initial);
    if(!stop)assert(calls==4 && values[2]==0);else assert(calls==3);
}
int main(void)
{
    assert(XV_BENCH_CLIP_REGION==36 && XV_BENCH_LIGHT_CENSUS==37 && XV_BENCH_DIAGNOSTIC_POLL==38 && XV_BENCH_DIAGNOSTIC_HIST==294 && XV_BENCH_QUERY_BOUNDARY==39 && XV_BENCH_OBJECT_COLLECT==43);
    xv_benchmark_remote_poll(1);
#ifdef TEST_NO_OBJECT_COLLECT
    assert(xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT)<0);
    assert(!calls && !xv_benchmark_remote_busy());
    puts("PASS: absent compiled candidate rejects43 without changing state");
#else
    available=0;assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT));xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(1,544,1,view) && !calls && !xv_benchmark_active());available=1;
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT));available=0;xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_step(1,544,1,view) && !calls && !xv_benchmark_active());available=1;
    assert(!xv_benchmark_remote_request(XV_BENCH_OBJECT_COLLECT));xv_benchmark_remote_poll(0);
    assert(!xv_benchmark_active() && !calls);
    for(unsigned initial=0;initial<2;initial++)for(unsigned stop=0;stop<3;stop++)trial(initial,stop);
    puts("PASS:43 admission, normal/cancel/lost-view exact initial restoration, unrelated query/world policy preservation");
#endif
    return 0;
}
