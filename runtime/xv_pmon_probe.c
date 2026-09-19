/* Opt-in startup capability check, never frame instrumentation. */
#include "xv_log.h"
#include <psp2/perf.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>

static int probe_counter(unsigned event, int software, uint32_t *value)
{
    const SceUID self=SCE_PERF_ARM_PMON_THREAD_ID_SELF;
    const char *stage="reset";
    int rc=0,started=0;
#define CHECK(label, call) do { stage=(label); rc=(call); if(rc<0)goto failed; } while(0)
    CHECK("reset",scePerfArmPmonReset(self));
    CHECK("select",scePerfArmPmonSelectEvent(self,0,(SceUInt8)event));
    CHECK("set-zero",scePerfArmPmonSetCounterValue(self,0,0));
    CHECK("read-zero",scePerfArmPmonGetCounterValue(self,0,value));
    if(*value) { stage="zero-response";rc=-1;goto failed; }
    CHECK("start",scePerfArmPmonStart(self));started=1;
    if(software) {
        for(unsigned i=0;i<32;i++)CHECK("increment",scePerfArmPmonSoftwareIncrement(1));
    } else {
        /* A bounded observable integer workload. Not a calibration of cycles,
         * cache effects, scheduling attribution or gameplay performance. */
        volatile uint32_t work=1;
        for(unsigned i=0;i<32768;i++)work=work*1664525u+1013904223u;
        (void)work;
    }
    CHECK("stop",scePerfArmPmonStop(self));started=0;
    CHECK("read",scePerfArmPmonGetCounterValue(self,0,value));
    if(software?*value!=32:*value==0) { stage="event-response";rc=-1;goto failed; }
#undef CHECK
    return 0;
failed:
    /* A failed stop still gets one cleanup attempt. Never report success if
     * selection, reading or cleanup failed, even when the output is zero. */
    if(started) {
        int stop=scePerfArmPmonStop(self);
        if(stop<0)xv_logf("[pmon-probe] cleanup-stop failed %08x\n",(unsigned)stop);
    }
    xv_logf("[pmon-probe] event %02x stage %s failed %08x value %u; counters unqualified\n",
        event,stage,(unsigned)rc,*value);
    return rc;
}
static int probe_run(SceSize bytes,void *arg)
{
    (void)bytes;(void)arg;
    uint32_t increments=0,cycles=0;
    int rc=probe_counter(SCE_PERF_ARM_PMON_SOFT_INCREMENT,1,&increments);
    if(rc<0)return rc;
    rc=probe_counter(SCE_PERF_ARM_PMON_CYCLE_COUNT,0,&cycles);
    if(rc<0)return rc;
    xv_logf("[pmon-probe] response passed: software %u/32 cycle-event %u; dedicated thread only, cache and scheduling attribution unqualified\n",
        increments,cycles);
    return 0;
}
void xv_pmon_probe_start(void)
{
    SceUID thread=sceKernelCreateThread("xv_pmon_probe",probe_run,
        sceKernelGetThreadCurrentPriority()+1,32*1024,0,SCE_KERNEL_CPU_MASK_USER_2,NULL);
    if(thread<0) { xv_logf("[pmon-probe] thread creation failed %08x\n",(unsigned)thread);return; }
    int rc=sceKernelStartThread(thread,0,NULL);
    if(rc<0) { xv_logf("[pmon-probe] thread start failed %08x\n",(unsigned)rc);sceKernelDeleteThread(thread);return; }
    SceUInt timeout=1000000;int result=0;
    rc=sceKernelWaitThreadEnd(thread,&result,&timeout);
    if(rc<0) {
        /* Do not delete a possibly live thread or free data it could use.
         * It owns only its own stack and counters; no guest work is running. */
        xv_logf("[pmon-probe] wait failed %08x; no successful qualification\n",(unsigned)rc);
        return;
    }
    sceKernelDeleteThread(thread);
    xv_logf("[pmon-probe] finished result %08x; no gameplay counters enabled\n",(unsigned)result);
}
