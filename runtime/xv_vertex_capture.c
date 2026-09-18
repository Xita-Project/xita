#include "xv_vertex_capture.h"
#include "xv_vertex_upload.h"
#include "xv_frame_slots.h"
#include "xv_gpu_upload.h"
#include "xv_quality_settings.h"
#include "xv_cpu.h"
#include "xv_log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <assert.h>
#include <string.h>

#ifndef XV_VERTEX_CAPTURE_DEFAULT
#define XV_VERTEX_CAPTURE_DEFAULT 0
#endif
#if XV_VERTEX_CAPTURE_DEFAULT != 0 && XV_VERTEX_CAPTURE_DEFAULT != 1
#error XV_VERTEX_CAPTURE_DEFAULT must be 0 or 1
#endif
#ifndef XV_VERTEX_CAPTURE_BYTES
#define XV_VERTEX_CAPTURE_BYTES (2u*1024u*1024u)
#endif
#define CAPTURE_JOBS 32u
typedef struct {
    xv_vertex_prepare_batch batch;
    const void *identity[XV_VERTEX_PREPARE_STREAMS];
    const void **targets[XV_VERTEX_PREPARE_STREAMS];
    xv_vertex_refs refs[XV_VERTEX_PREPARE_STREAMS];
    void (*complete)(void *,int);
    void *context;
} capture_job;
static SceUID cap_thread=-1,cap_wake=-1,cap_done=-1,cap_memory=-1;
static uint8_t *cap_arena;
static capture_job *cap_jobs;
static unsigned cap_used,cap_retired;
static unsigned cap_submitted,cap_completed; /* atomic publication counters */
static int cap_stopping,cap_unavailable,cap_enabled=-1;
static unsigned cap_jobs_total,cap_drains,cap_pressure,cap_failures,cap_max_pending;
static uint64_t cap_bytes,cap_capture_us,cap_worker_us,cap_join_us;

static void cap_execute(capture_job *job)
{
    xv_vertex_prepare_batch *b=&job->batch;b->ok=0;
    for(unsigned i=0;i<b->count;i++) {
        xv_vertex_prepare_stream *s=&b->streams[i];unsigned packed=0;
#if XV_PACKED_VERTEX_LAYOUT
        packed=s->packed;
#endif
        s->result=xv_vertex_upload_snapshot(b->slot,job->identity[i],s->source,
            s->bytes,s->stride,s->refs,packed);
        if(!s->result)return;
    }
    b->ok=1;
}
static int cap_run(SceSize bytes,void *arg)
{
    (void)bytes;(void)arg;xv_cpu_log_thread("vertex-capture");
    for(;;) {
        /* The finite idle wait also guarantees progress after a failed wake
         * notification. Queue counters, rather than event bits, own the work. */
        unsigned bits;SceUInt timeout=20000;
        sceKernelWaitEventFlag(cap_wake,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&bits,&timeout);
        if(__atomic_load_n(&cap_stopping,__ATOMIC_ACQUIRE))return 0;
        unsigned next=__atomic_load_n(&cap_completed,__ATOMIC_RELAXED);
        while(next!=__atomic_load_n(&cap_submitted,__ATOMIC_ACQUIRE)) {
            uint64_t start=sceKernelGetProcessTimeWide();
            cap_execute(&cap_jobs[next&(CAPTURE_JOBS-1)]);
            xv_gpu_write_barrier();
            cap_worker_us+=sceKernelGetProcessTimeWide()-start;
            __atomic_store_n(&cap_completed,++next,__ATOMIC_RELEASE);
            sceKernelSetEventFlag(cap_done,1);
        }
    }
}
static void cap_release(void)
{
    if(cap_thread>=0)sceKernelDeleteThread(cap_thread);
    if(cap_wake>=0)sceKernelDeleteEventFlag(cap_wake);
    if(cap_done>=0)sceKernelDeleteEventFlag(cap_done);
    if(cap_memory>=0)sceKernelFreeMemBlock(cap_memory);
    cap_thread=cap_wake=cap_done=cap_memory=-1;cap_arena=NULL;cap_jobs=NULL;
}
static int cap_start(void)
{
    if(cap_thread>=0)return 1;
    if(cap_unavailable)return 0;
    unsigned allocation=(XV_VERTEX_CAPTURE_BYTES+sizeof(capture_job)*CAPTURE_JOBS+4095u)&~4095u;
    cap_memory=sceKernelAllocMemBlock("xv_vertex_capture",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,allocation,NULL);
    if(cap_memory<0 || sceKernelGetMemBlockBase(cap_memory,(void **)&cap_arena)<0 || !cap_arena)goto fail;
    cap_jobs=(capture_job *)(cap_arena+XV_VERTEX_CAPTURE_BYTES);
    cap_wake=sceKernelCreateEventFlag("xv_capture_wake",0,0,NULL);
    cap_done=sceKernelCreateEventFlag("xv_capture_done",0,0,NULL);
    if(cap_wake<0 || cap_done<0)goto fail;
    cap_thread=sceKernelCreateThread("xv_vertex_capture",cap_run,
        sceKernelGetThreadCurrentPriority()+1,32*1024,0,SCE_KERNEL_CPU_MASK_USER_0,NULL);
    if(cap_thread<0 || sceKernelStartThread(cap_thread,0,NULL)<0)goto fail;
    xv_logf("[vertex-capture] core 0; %u KiB private inputs, %u ordered jobs; exact snapshots, drain before publication\n",
        XV_VERTEX_CAPTURE_BYTES>>10,CAPTURE_JOBS);
    return 1;
fail:
    cap_release();cap_unavailable=1;
    xv_logf("[vertex-capture] unavailable; synchronous preparation retained\n");return 0;
}
static void cap_collect(void)
{
    unsigned completed=__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE);
    while(cap_retired!=completed) {
        capture_job *j=&cap_jobs[cap_retired&(CAPTURE_JOBS-1)];
        if(j->batch.ok)for(unsigned i=0;i<j->batch.count;i++)
            *j->targets[i]=j->batch.streams[i].result;
        else cap_failures++;
        if(j->complete)j->complete(j->context,j->batch.ok);
        cap_retired++;
    }
}
void xv_vertex_capture_drain(void)
{
    unsigned submitted=__atomic_load_n(&cap_submitted,__ATOMIC_RELAXED);
    if(cap_retired==submitted) { cap_used=0;return; }
    uint64_t start=sceKernelGetProcessTimeWide();cap_drains++;
    while(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)!=submitted) {
        unsigned bits;SceUInt timeout=1000;
        if(sceKernelWaitEventFlag(cap_done,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&bits,&timeout)<0)
            sceKernelDelayThread(100);
    }
    cap_collect();cap_used=0;cap_join_us+=sceKernelGetProcessTimeWide()-start;
}
int xv_vertex_capture_submit(const xv_vertex_prepare_batch *batch,
    const void ***targets,void (*complete)(void *,int),void *context)
{
    if(cap_enabled<0)cap_enabled=xv_quality_int("XV_VERTEX_CAPTURE",XV_VERTEX_CAPTURE_DEFAULT,0,1);
    if(!cap_enabled || !batch || !batch->count || batch->count>XV_VERTEX_PREPARE_STREAMS ||
       batch->slot>=XV_FRAME_SLOTS || !targets)goto fallback;
    unsigned required=0;
    for(unsigned i=0;i<batch->count;i++) {
        const xv_vertex_prepare_stream *s=&batch->streams[i];
        if(!s->source || !s->bytes || !targets[i] ||
           s->bytes>XV_VERTEX_CAPTURE_BYTES-required)goto fallback;
        unsigned aligned=(s->bytes+15u)&~15u;
        if(aligned>XV_VERTEX_CAPTURE_BYTES-required)goto fallback;
        required+=aligned;
    }
    if(!cap_start())goto fallback;
    cap_collect();
    unsigned submitted=__atomic_load_n(&cap_submitted,__ATOMIC_RELAXED);
    if(submitted-cap_retired==CAPTURE_JOBS || required>XV_VERTEX_CAPTURE_BYTES-cap_used) {
        cap_pressure++;xv_vertex_capture_drain();
    }
    uint64_t start=sceKernelGetProcessTimeWide();
    capture_job *j=&cap_jobs[submitted&(CAPTURE_JOBS-1)];j->batch=*batch;
    j->complete=complete;j->context=context;
    for(unsigned i=0;i<batch->count;i++) {
        xv_vertex_prepare_stream *s=&j->batch.streams[i];
        j->identity[i]=s->source;j->targets[i]=targets[i];
        memcpy(cap_arena+cap_used,s->source,s->bytes);
        s->source=cap_arena+cap_used;s->result=NULL;
        cap_used+=(s->bytes+15u)&~15u;
        if(s->refs) { j->refs[i]=*s->refs;s->refs=&j->refs[i]; }
    }
    j->batch.ok=0;cap_jobs_total++;cap_bytes+=required;
    unsigned pending=submitted+1-cap_retired;if(pending>cap_max_pending)cap_max_pending=pending;
    __atomic_store_n(&cap_submitted,submitted+1,__ATOMIC_RELEASE);
    sceKernelSetEventFlag(cap_wake,1);
    cap_capture_us+=sceKernelGetProcessTimeWide()-start;
    return 1;
fallback:
    xv_vertex_capture_drain();return 0;
}
void xv_vertex_capture_shutdown(void)
{
    xv_vertex_capture_drain();
    if(cap_thread>=0) {
        __atomic_store_n(&cap_stopping,1,__ATOMIC_RELEASE);sceKernelSetEventFlag(cap_wake,1);
        while(sceKernelWaitThreadEnd(cap_thread,NULL,NULL)<0)sceKernelDelayThread(100);
    }
    cap_release();cap_unavailable=cap_stopping=0;cap_enabled=-1;
    cap_submitted=cap_completed=cap_retired=cap_used=0;
}
void xv_vertex_capture_report(unsigned frames)
{
    assert(cap_retired==__atomic_load_n(&cap_submitted,__ATOMIC_ACQUIRE));
    if(cap_jobs_total)xv_logf("[vertex-capture] %u frames: %u jobs %llu KiB; capture %llu us worker %llu us join %llu us; %u drains %u pressure max-pending %u failed %u (overlapping window totals)\n",
        frames,cap_jobs_total,(unsigned long long)(cap_bytes>>10),(unsigned long long)cap_capture_us,
        (unsigned long long)cap_worker_us,(unsigned long long)cap_join_us,
        cap_drains,cap_pressure,cap_max_pending,cap_failures);
    cap_jobs_total=cap_drains=cap_pressure=cap_max_pending=cap_failures=0;
    cap_bytes=cap_capture_us=cap_worker_us=cap_join_us=0;
}
