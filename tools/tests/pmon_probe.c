#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "../../runtime/xv_pmon_probe.c"
static unsigned calls,fail_at,stops,value,event,passed,cleanup_errors;
static int running,bad_zero,bad_soft,zero_cycles,fail_stops;
static int step(void) { return ++calls==fail_at?-77:0; }
void xv_logf(const char *format,...)
{
    if(strstr(format,"response passed"))passed++;
    if(strstr(format,"cleanup-stop failed"))cleanup_errors++;
}
int scePerfArmPmonReset(SceUID id)
{ assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF);int r=step();if(!r)value=0;return r; }
int scePerfArmPmonSelectEvent(SceUID id,SceUInt32 counter,SceUInt8 code)
{ assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF && counter==0);int r=step();if(!r)event=code;return r; }
int scePerfArmPmonSetCounterValue(SceUID id,SceUInt32 counter,SceUInt32 v)
{ assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF && counter==0 && v==0);int r=step();if(!r)value=v;return r; }
int scePerfArmPmonGetCounterValue(SceUID id,SceUInt32 counter,SceUInt32 *v)
{
    assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF && counter==0);int r=step();
    if(!r)*v=bad_zero && !value?1:bad_soft && value==32?31:value;
    return r;
}
int scePerfArmPmonStart(SceUID id)
{ assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF && !running);int r=step();if(!r)running=1;return r; }
int scePerfArmPmonStop(SceUID id)
{
    assert(id==SCE_PERF_ARM_PMON_THREAD_ID_SELF && running);stops++;int r=step();
    if(fail_stops)return -88;
    if(!r) {running=0;if(event==SCE_PERF_ARM_PMON_CYCLE_COUNT)value=zero_cycles?0:100;}
    return r;
}
int scePerfArmPmonSoftwareIncrement(SceUInt32 mask)
{ assert(mask==1 && running && event==SCE_PERF_ARM_PMON_SOFT_INCREMENT);int r=step();if(!r)value++;return r; }
static void reset(void)
{
    calls=fail_at=stops=value=event=passed=cleanup_errors=0;
    running=bad_zero=bad_soft=zero_cycles=fail_stops=0;
}
int main(void)
{
    reset();assert(probe_run(0,NULL)==0 && passed==1 && !running && stops==2);
    unsigned n=calls;
    for(unsigned i=1;i<=n;i++) {
        reset();fail_at=i;assert(probe_run(0,NULL)<0 && !passed && !running);
    }
    reset();bad_zero=1;assert(probe_run(0,NULL)<0 && !passed && !running && stops==0);
    reset();bad_soft=1;assert(probe_run(0,NULL)<0 && !passed && !running);
    reset();zero_cycles=1;assert(probe_run(0,NULL)<0 && !passed && !running);
    reset();fail_stops=1;assert(probe_run(0,NULL)<0 && !passed && stops==2 && cleanup_errors==1);
    printf("PASS: counter response, %u individual API failures, invalid/zero readings and failed cleanup never qualify\n",n);
}
