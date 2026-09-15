#include "xv_vertex_prepare.h"
#include "xv_vertex_upload.h"
#include "xv_gpu_upload.h"
#include "xv_cpu.h"
#include "xv_log.h"
#include "xv_quality_settings.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <assert.h>

static SceUID thread=-1, wake=-1, done=-1;
static int unavailable, stopping, complete;
static xv_vertex_prepare_batch *pending;
static unsigned batches, inline_batches, maximum_bytes;
static int minimum_bytes=65536, enabled=-1, override_enabled=-1;
static unsigned override_minimum;
static unsigned cutoff(void) { return override_enabled<0 ? (unsigned)minimum_bytes : override_minimum; }
static uint64_t bytes_total, worker_us, join_us, dispatch_us;

static void execute(xv_vertex_prepare_batch *b)
{
    b->ok=0;
    if (b->count>XV_VERTEX_PREPARE_STREAMS) return;
    for (unsigned i=0;i<b->count;i++) {
        xv_vertex_prepare_stream *s=&b->streams[i];
        s->result=s->refs ? xv_vertex_upload_referenced(b->slot,s->source,
            s->bytes,s->stride,s->refs) : xv_vertex_upload(b->slot,s->source,s->bytes);
        if (!s->result) return;
    }
    b->ok=1;
}
static int run(SceSize n,void *p)
{
    (void)n;(void)p;
    xv_cpu_log_thread("vertex-prepare");
    for (;;) {
        if (sceKernelWaitSema(wake,1,NULL)<0) continue;
        /* Atomic publication carries the batch and stop request. */
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&stopping,__ATOMIC_ACQUIRE)) return 0;
        xv_vertex_prepare_batch *b=__atomic_load_n(&pending,__ATOMIC_ACQUIRE);
        uint64_t start=sceKernelGetProcessTimeWide();
        execute(b);
        /* Stores to uncached GPU uploads originate on this core. */
        xv_gpu_write_barrier();
        worker_us+=sceKernelGetProcessTimeWide()-start;
        __atomic_store_n(&complete,1,__ATOMIC_RELEASE);
        /* A lost notification cannot release borrowed inputs or strand a join. */
        sceKernelSetEventFlag(done,1);
    }
}
static void release_handles(void)
{
    if(thread>=0)sceKernelDeleteThread(thread);
    if(wake>=0)sceKernelDeleteSema(wake);
    if(done>=0)sceKernelDeleteEventFlag(done);
    thread=wake=done=-1;
}
static int start_worker(void)
{
    if(thread>=0)return 1;
    if(unavailable)return 0;
    wake=sceKernelCreateSema("xv_prepare_wake",0,0,1,NULL);
    done=sceKernelCreateEventFlag("xv_prepare_done",0,0,NULL);
    if(wake<0 || done<0)goto fail;
    thread=sceKernelCreateThread("xv_vertex_prepare",run,
        sceKernelGetThreadCurrentPriority()+1,32*1024,0,SCE_KERNEL_CPU_MASK_USER_0,NULL);
    if(thread<0 || sceKernelStartThread(thread,0,NULL)<0)goto fail;
    xv_logf("[cpu-work] vertex preparation enabled on core 0; joined before guest resumes\n");
    return 1;
fail:
    xv_logf("[cpu-work] vertex preparation unavailable (thread %d wake %d done %d); caller fallback\n",thread,wake,done);
    release_handles();unavailable=1;return 0;
}
/* Called on the recording owner at a drained benchmark boundary. */
int xv_vertex_prepare_available(void)
{ assert(!pending);return start_worker(); }
void xv_vertex_prepare_override(int value,unsigned minimum)
{
    assert(!pending);
    assert(value<0 || (minimum>=1 && minimum<=32u*1024u*1024u));
    override_enabled=value<0 ? -1 : !!value;
    override_minimum=minimum;
}
void xv_vertex_prepare_begin(xv_vertex_prepare_batch *b)
{
    assert(b && !pending);
    if(enabled<0) {
        enabled=xv_quality_int("XV_VERTEX_PREPARE",0,0,1);
        minimum_bytes=xv_quality_int("XV_VERTEX_PREPARE_MIN_BYTES",65536,1,32*1024*1024);
    }
    uint64_t bytes=0;
    if(b->count<=XV_VERTEX_PREPARE_STREAMS)
        for(unsigned i=0;i<b->count;i++)bytes+=b->streams[i].bytes;
    if(bytes>maximum_bytes)maximum_bytes=bytes>UINT32_MAX?UINT32_MAX:(unsigned)bytes;
    b->ok=0;
    if((override_enabled<0 ? enabled : override_enabled) && b->count<=XV_VERTEX_PREPARE_STREAMS && bytes>=cutoff() && start_worker()) {
        uint64_t start=sceKernelGetProcessTimeWide();
        __atomic_store_n(&complete,0,__ATOMIC_RELAXED);
        __atomic_store_n(&pending,b,__ATOMIC_RELEASE);
        if(sceKernelSignalSema(wake,1)>=0) {
            batches++;bytes_total+=bytes;dispatch_us+=sceKernelGetProcessTimeWide()-start;
            return;
        }
        __atomic_store_n(&pending,NULL,__ATOMIC_RELEASE);
    }
    inline_batches++;execute(b);
}
int xv_vertex_prepare_finish(xv_vertex_prepare_batch *b)
{
    assert(!pending || pending==b);
    if(pending) {
        uint64_t start=sceKernelGetProcessTimeWide();
        while(!__atomic_load_n(&complete,__ATOMIC_ACQUIRE)) {
            unsigned bits;SceUInt timeout=1000;
            if(sceKernelWaitEventFlag(done,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,
                &bits,&timeout)<0 && !__atomic_load_n(&complete,__ATOMIC_ACQUIRE))
                sceKernelDelayThread(100);
        }
        join_us+=sceKernelGetProcessTimeWide()-start;
        __atomic_store_n(&pending,NULL,__ATOMIC_RELEASE);
    }
    return b->ok;
}
void xv_vertex_prepare_shutdown(void)
{
    if(pending)xv_vertex_prepare_finish(pending);
    if(thread>=0) {
        __atomic_store_n(&stopping,1,__ATOMIC_RELEASE);
        while(sceKernelSignalSema(wake,1)<0)sceKernelDelayThread(100);
        sceKernelWaitThreadEnd(thread,NULL,NULL);
    }
    release_handles();unavailable=stopping=complete=0;
}
void xv_vertex_prepare_report(unsigned frames)
{
    assert(!pending);
    if(batches || inline_batches)xv_logf("[vertex-prepare] %u frames: %u worker / %u inline batches, %llu KiB; worker %llu us, dispatch %llu us, join %llu us; max %u cutoff %u bytes (overlapping times)\n",
        frames,batches,inline_batches,(unsigned long long)(bytes_total>>10),
        (unsigned long long)worker_us,(unsigned long long)dispatch_us,(unsigned long long)join_us,maximum_bytes,cutoff());
    batches=inline_batches=maximum_bytes=0;bytes_total=worker_us=dispatch_us=join_us=0;
}
