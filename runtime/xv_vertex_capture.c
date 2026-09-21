#include "xv_vertex_capture.h"
#include "xv_vertex_upload.h"
#include "xv_bytes_equal.h"
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
#ifndef XV_VERTEX_CAPTURE_NOTIFY
#define XV_VERTEX_CAPTURE_NOTIFY 0
#endif
#if XV_VERTEX_CAPTURE_NOTIFY != 0 && XV_VERTEX_CAPTURE_NOTIFY != 1
#error XV_VERTEX_CAPTURE_NOTIFY must be 0 or 1
#endif
#if XV_VERTEX_CAPTURE_NOTIFY
/* Queue counters own jobs. This handshake only suppresses redundant events. */
enum { CAP_SLEEPING, CAP_RUNNING, CAP_PENDING };
static unsigned cap_wake_state,cap_waiting;
static unsigned cap_wake_signals,cap_wake_skips,cap_done_signals,cap_done_skips;
static unsigned cap_wake_failures,cap_done_notify_failures;
/* Compile-time fixture frontiers; ordinary builds contain no calls here. */
enum { CAP_BEFORE_SLEEP, CAP_AFTER_SLEEP, CAP_BEFORE_COMPLETE,
       CAP_AFTER_COMPLETE, CAP_AFTER_NOTIFY, CAP_DRAIN_ARMED, CAP_DRAIN_WAIT,
    CAP_BEFORE_EXECUTE };
#ifndef XV_CAPTURE_NOTIFY_POINT
#define XV_CAPTURE_NOTIFY_POINT(point) ((void)0)
#endif
#endif
#ifndef XV_VERTEX_CAPTURE_REUSE
#define XV_VERTEX_CAPTURE_REUSE 0
#endif
#if XV_VERTEX_CAPTURE_REUSE != 0 && XV_VERTEX_CAPTURE_REUSE != 1
#error XV_VERTEX_CAPTURE_REUSE must be 0 or 1
#endif
#ifndef XV_VERTEX_CAPTURE_READY
#define XV_VERTEX_CAPTURE_READY 0
#endif
#if XV_VERTEX_CAPTURE_READY != 0 && XV_VERTEX_CAPTURE_READY != 1
#error XV_VERTEX_CAPTURE_READY must be 0 or 1
#endif
#if XV_VERTEX_CAPTURE_READY && !XV_VERTEX_CAPTURE_REUSE
#error XV_VERTEX_CAPTURE_READY requires XV_VERTEX_CAPTURE_REUSE
#endif
#if XV_VERTEX_CAPTURE_READY
static unsigned cap_ready_draws;
#endif
#ifndef XV_VERTEX_PERSISTENT
#define XV_VERTEX_PERSISTENT 0
#endif
#if XV_VERTEX_PERSISTENT != 0 && XV_VERTEX_PERSISTENT != 1
#error XV_VERTEX_PERSISTENT must be 0 or 1
#endif
#if XV_VERTEX_PERSISTENT
#include "xv_vertex_persistent.h"
#endif
typedef struct {
    xv_vertex_prepare_batch batch;
    const void *identity[XV_VERTEX_PREPARE_STREAMS];
    const void **targets[XV_VERTEX_PREPARE_STREAMS];
    xv_vertex_refs refs[XV_VERTEX_PREPARE_STREAMS];
#if XV_VERTEX_PERSISTENT
    unsigned persistent[XV_VERTEX_PREPARE_STREAMS],persistent_copy[XV_VERTEX_PREPARE_STREAMS];
#endif
#if XV_VERTEX_CAPTURE_PACKED
    unsigned compact[XV_VERTEX_PREPARE_STREAMS];
#endif
#if XV_VERTEX_CAPTURE_REUSE
    unsigned reuse[XV_VERTEX_PREPARE_STREAMS];
#endif
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
static unsigned cap_masks_copied,cap_masks_omitted;
static uint64_t cap_bytes,cap_capture_us,cap_worker_us,cap_join_us;
#ifndef XV_VERTEX_CAPTURE_DETAIL_DEFAULT
#define XV_VERTEX_CAPTURE_DETAIL_DEFAULT 0
#endif
#if XV_VERTEX_CAPTURE_DETAIL_DEFAULT != 0 && XV_VERTEX_CAPTURE_DETAIL_DEFAULT != 1
#error XV_VERTEX_CAPTURE_DETAIL_DEFAULT must be 0 or 1
#endif
/* Owner-only, opt-in samples. Elapsed time includes scheduling/preemption;
 * these nested scopes are not additional frame time or CPU-cycle counters. */
static int cap_detail_enabled=-1;
static unsigned cap_detail_serial,cap_detail_samples;
static unsigned cap_detail_compares,cap_detail_copies,cap_detail_publishes;
static uint64_t cap_detail_compare_us,cap_detail_copy_us,cap_detail_publish_us;
static uint64_t cap_detail_compare_bytes,cap_detail_copy_bytes;
#if XV_VERTEX_CAPTURE_PACKED
static int cap_compact_enabled=-1;
static unsigned cap_compact_streams;
static uint64_t cap_compact_saved;
static int cap_compact(const xv_vertex_prepare_stream *s)
{
    if(cap_compact_enabled<0)cap_compact_enabled=xv_quality_int("XV_VERTEX_CAPTURE_PACKED",1,0,1);
    return cap_compact_enabled && s->packed==XV_PACKED_PREFIX16 && s->stride==32 && !(s->bytes%32);
}
#endif

#if XV_VERTEX_CAPTURE_REUSE
#define CAPTURE_ENTRIES 512u
#define CAPTURE_BUCKETS 256u
/* Owner-only keys and immutable payloads, append-only until capacity reclaim.
 * A joined drain invalidates every GPU result, but may retain CPU snapshots.
 * The worker neither reads guest memory nor owns the source identity keys. */
typedef struct {
    const void *identity;
    unsigned offset,bytes,stride,packed,compact,slot,next,current,generation;
} capture_entry;
static capture_entry cap_entries[CAPTURE_ENTRIES];
static const void *cap_results[CAPTURE_ENTRIES];
static unsigned cap_buckets[CAPTURE_BUCKETS],cap_entry_count;
static int cap_reuse_enabled=-1;
static int cap_retain_enabled=-1;
static unsigned cap_reuse_checks,cap_reuse_hits,cap_reuse_prepared;
static uint64_t cap_reuse_bytes;
static unsigned cap_retained_hits,cap_reclaims;
static uint64_t cap_retained_bytes;
static void cap_reuse_reset(void)
{ cap_entry_count=0;memset(cap_buckets,0,sizeof cap_buckets); }
#ifndef XV_CAPTURE_TRUST_TAGS
#define XV_CAPTURE_TRUST_TAGS 0
#endif
#if XV_CAPTURE_TRUST_TAGS
/* Halo's 22 MB tag cache (physical 0x3A6000) holds the static BSP and model
 * vertex buffers. It is rewritten only by file reads (map load, BSP switch),
 * which the file layer reports here. A reuse source inside that region under
 * the same read generation is treated as unchanged without the byte compare;
 * an independent check every 64 trusted lookups compares and disables trust on
 * the first mismatch. Dynamic (heap) vertex data keeps the full compare. */
#ifndef XV_CAPTURE_TRUST_TAGS_DEFAULT
#define XV_CAPTURE_TRUST_TAGS_DEFAULT 0
#endif
extern uint8_t *g_xram __attribute__((weak));
enum { TAG_PHYS_BASE=0x3A6000u, TAG_PHYS_BYTES=0x1600000u };
static int trust_enabled=-1;
static unsigned trust_generation,trust_disabled,trust_hits,trust_verified,trust_mismatches;
static uint64_t trust_bytes;
/* Owner-only sequence; timing diagnostics must not select correctness checks. */
static unsigned trust_verify_serial;
void xv_vertex_capture_tags_written(uint32_t guest,uint32_t bytes)
{
    uint32_t phys=guest>=0xF0000000u?guest-0xF0000000u:guest>=0x80000000u?guest-0x80000000u:guest;
    if(phys<TAG_PHYS_BASE+TAG_PHYS_BYTES && phys+bytes>TAG_PHYS_BASE)
        __atomic_fetch_add(&trust_generation,1u,__ATOMIC_RELAXED);
}
static int trust_source(const void *source,unsigned bytes)
{
    if(!&g_xram || !g_xram || (const uint8_t *)source<g_xram)return 0;
    uintptr_t off=(const uint8_t *)source-g_xram;
    return off>=TAG_PHYS_BASE && off+bytes<=TAG_PHYS_BASE+TAG_PHYS_BYTES;
}
#endif
static void cap_reuse_retire(void)
{
    if(cap_retain_enabled<0)
        cap_retain_enabled=xv_quality_int("XV_VERTEX_CAPTURE_RETAIN",0,0,1);
    if(cap_reuse_enabled!=1 || !cap_retain_enabled || !cap_entry_count) {
        cap_used=0;cap_reuse_reset();return;
    }
    /* All preparation jobs have completed and their callbacks were collected.
     * GPU addresses cannot cross this boundary: the caller may reset a slot,
     * reorder draws, or run synchronous fallback preparation immediately after
     * drain. A future exact CPU hit must prepare/validate its GPU storage again. */
    for(unsigned i=0;i<cap_entry_count;i++) {
        cap_results[i]=NULL;cap_entries[i].current=0;
    }
}
static unsigned cap_reuse_find(const xv_vertex_prepare_stream *s,unsigned slot,unsigned packed,unsigned compact,int sample)
{
    if(cap_reuse_enabled<0)cap_reuse_enabled=xv_quality_int("XV_VERTEX_CAPTURE_REUSE",1,0,1);
    /* Sparse uploads may intentionally retain stale unfetched records. Do not
     * reuse their result for another mask, even when the captured bytes match. */
    if(!cap_reuse_enabled || xv_vertex_refs_sparse(s->refs,s->bytes,s->stride))return 0;
#if XV_PACKED_VERTEX_LAYOUT
    if(packed && packed!=XV_PACKED_PREFIX16)return 0;
#endif
    unsigned bucket=((uintptr_t)s->source>>4)&(CAPTURE_BUCKETS-1);
    for(unsigned id=cap_buckets[bucket];id;id=cap_entries[id-1].next) {
        capture_entry *e=&cap_entries[id-1];
        if(e->identity!=s->source || e->bytes!=s->bytes || e->stride!=s->stride ||
           e->packed!=packed || e->compact!=compact || e->slot!=slot)continue;
        cap_reuse_checks++;
        uint64_t detail_start=sample?sceKernelGetProcessTimeWide():0;
        int equal,compared=1;
#if XV_CAPTURE_TRUST_TAGS
        if(trust_enabled<0)trust_enabled=xv_quality_int("XV_CAPTURE_TRUST_TAGS",XV_CAPTURE_TRUST_TAGS_DEFAULT,0,1);
        int trusted=trust_enabled && !trust_disabled &&
            e->generation==__atomic_load_n(&trust_generation,__ATOMIC_RELAXED) &&
            trust_source(s->source,s->bytes);
        int verify=trusted && !(trust_verify_serial++ & 63u);
        if(trusted && !verify) {
            trust_hits++;trust_bytes+=compact?s->bytes/2:s->bytes;
            equal=1;compared=0;
        } else {
#endif
#if XV_VERTEX_CAPTURE_PACKED
        if(compact)equal=xv_packed_equal(s->source,cap_arena+e->offset,s->bytes/32);
        else
#endif
        /* We need equality, not ordering. Use the same bounded NEON block
         * loads as resident uploads, including unaligned inputs and tails. */
        equal=xv_bytes_equal_blocks(s->source,cap_arena+e->offset,s->bytes);
#if XV_CAPTURE_TRUST_TAGS
        if(trusted) {
            trust_verified++;
            if(!equal) {
                trust_mismatches++;trust_disabled=1;
                xv_logf("[capture-trust] sampled verification found a changed tag-resident source (%u bytes); trust disabled for this session\n",s->bytes);
            }
        }
        }
#endif
        if(sample && compared) {
            cap_detail_compare_us+=sceKernelGetProcessTimeWide()-detail_start;
            cap_detail_compares++;
            /* Logical input bytes, not measured memory-bus traffic. */
            cap_detail_compare_bytes+=compact?s->bytes/2:s->bytes;
        }
        if(equal) {
            cap_reuse_hits++;cap_reuse_bytes+=compact?s->bytes/2:s->bytes;
            if(!e->current) {
                cap_retained_hits++;cap_retained_bytes+=compact?s->bytes/2:s->bytes;
                e->current=1;
            }
            return id;
        }
        /* Only compare the newest version of this exact key. Repeatedly
         * rewritten effects cannot scan every earlier version in the arena. */
        break;
    }
    return 0;
}
static unsigned cap_reuse_add(const xv_vertex_prepare_stream *s,unsigned slot,unsigned packed,unsigned compact)
{
    if(!cap_reuse_enabled || xv_vertex_refs_sparse(s->refs,s->bytes,s->stride) ||
       cap_entry_count==CAPTURE_ENTRIES)return 0;
#if XV_PACKED_VERTEX_LAYOUT
    if(packed && packed!=XV_PACKED_PREFIX16)return 0;
#endif
    unsigned bucket=((uintptr_t)s->source>>4)&(CAPTURE_BUCKETS-1),id=++cap_entry_count;
    cap_entries[id-1]=(capture_entry){s->source,cap_used,s->bytes,s->stride,packed,compact,slot,cap_buckets[bucket],1,0};
#if XV_CAPTURE_TRUST_TAGS
    cap_entries[id-1].generation=__atomic_load_n(&trust_generation,__ATOMIC_RELAXED);
#endif
    cap_results[id-1]=NULL;cap_buckets[bucket]=id;return id;
}
#endif

static void cap_execute(capture_job *job)
{
    xv_vertex_prepare_batch *b=&job->batch;b->ok=0;
#if XV_VERTEX_PERSISTENT
    /* Finish every promised cache upload, including jobs whose ordinary
     * streams subsequently fail. Later FIFO hits may already refer to it. */
    for(unsigned i=0;i<b->count;i++)if(job->persistent_copy[i])vp_upload(job->persistent[i]);
#endif
    for(unsigned i=0;i<b->count;i++) {
        xv_vertex_prepare_stream *s=&b->streams[i];unsigned packed=0;
#if XV_VERTEX_PERSISTENT
        if(job->persistent[i]) { s->result=vp_result(job->persistent[i]);continue; }
#endif
#if XV_VERTEX_CAPTURE_REUSE
        unsigned id=job->reuse[i];
        if(id && cap_results[id-1]) {
            s->result=cap_results[id-1];cap_reuse_prepared++;continue;
        }
#endif
#if XV_PACKED_VERTEX_LAYOUT
        packed=s->packed;
#endif
#if XV_VERTEX_CAPTURE_PACKED
        if(job->compact[i])s->result=xv_vertex_upload_compact_snapshot(b->slot,
            job->identity[i],s->source,s->bytes/2);
        else
#endif
        s->result=xv_vertex_upload_snapshot(b->slot,job->identity[i],s->source,
            s->bytes,s->stride,s->refs,packed);
        if(!s->result)return;
#if XV_VERTEX_CAPTURE_REUSE
        if(id)cap_results[id-1]=s->result;
#endif
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
#if XV_VERTEX_CAPTURE_NOTIFY
        for(;;) {
            /* Read a producer's PENDING publication before examining its jobs. */
            __atomic_exchange_n(&cap_wake_state,CAP_RUNNING,__ATOMIC_ACQ_REL);
#endif
        if(__atomic_load_n(&cap_stopping,__ATOMIC_ACQUIRE))return 0;
        unsigned next=__atomic_load_n(&cap_completed,__ATOMIC_RELAXED);
        while(next!=__atomic_load_n(&cap_submitted,__ATOMIC_ACQUIRE)) {
#if XV_VERTEX_CAPTURE_NOTIFY
            XV_CAPTURE_NOTIFY_POINT(CAP_BEFORE_EXECUTE);
#endif
            uint64_t start=sceKernelGetProcessTimeWide();
            cap_execute(&cap_jobs[next&(CAPTURE_JOBS-1)]);
            xv_gpu_write_barrier();
            cap_worker_us+=sceKernelGetProcessTimeWide()-start;
#if XV_VERTEX_CAPTURE_NOTIFY
            XV_CAPTURE_NOTIFY_POINT(CAP_BEFORE_COMPLETE);
#endif
            __atomic_store_n(&cap_completed,++next,__ATOMIC_RELEASE);
#if XV_VERTEX_CAPTURE_NOTIFY
            XV_CAPTURE_NOTIFY_POINT(CAP_AFTER_COMPLETE);
            /* RMW pairs with the owner's RMW before its completed reread:
             * either it sees this completion or we observe its wait and signal. */
            if(__atomic_exchange_n(&cap_waiting,0,__ATOMIC_ACQ_REL)) {
                __atomic_fetch_add(&cap_done_signals,1,__ATOMIC_RELAXED);
                if(sceKernelSetEventFlag(cap_done,1)<0)
                    __atomic_fetch_add(&cap_done_notify_failures,1,__ATOMIC_RELAXED);
            } else __atomic_fetch_add(&cap_done_skips,1,__ATOMIC_RELAXED);
            XV_CAPTURE_NOTIFY_POINT(CAP_AFTER_NOTIFY);
#else
            sceKernelSetEventFlag(cap_done,1);
#endif
        }
#if XV_VERTEX_CAPTURE_NOTIFY
            unsigned expected=CAP_RUNNING;
            XV_CAPTURE_NOTIFY_POINT(CAP_BEFORE_SLEEP);
            if(__atomic_compare_exchange_n(&cap_wake_state,&expected,CAP_SLEEPING,
                    0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
                XV_CAPTURE_NOTIFY_POINT(CAP_AFTER_SLEEP);
                break;
            }
            /* A producer set PENDING after our empty check. Drain again;
             * do not rely on a second submission or the idle timeout. */
        }
#endif
    }
}
static void cap_release(void)
{
    if(cap_thread>=0)sceKernelDeleteThread(cap_thread);
    if(cap_wake>=0)sceKernelDeleteEventFlag(cap_wake);
    if(cap_done>=0)sceKernelDeleteEventFlag(cap_done);
    if(cap_memory>=0)sceKernelFreeMemBlock(cap_memory);
    cap_thread=cap_wake=cap_done=cap_memory=-1;cap_arena=NULL;cap_jobs=NULL;
#if XV_VERTEX_CAPTURE_NOTIFY
    /* Only after worker join, or a start failure with no running worker. */
    __atomic_store_n(&cap_wake_state,CAP_SLEEPING,__ATOMIC_RELAXED);
    __atomic_store_n(&cap_waiting,0,__ATOMIC_RELAXED);
#endif
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
#if XV_VERTEX_CAPTURE_NOTIFY
    __atomic_store_n(&cap_wake_state,CAP_SLEEPING,__ATOMIC_RELAXED);
    __atomic_store_n(&cap_waiting,0,__ATOMIC_RELAXED);
#endif
    cap_thread=sceKernelCreateThread("xv_vertex_capture",cap_run,
        sceKernelGetThreadCurrentPriority()+1,32*1024,0,SCE_KERNEL_CPU_MASK_USER_0,NULL);
    if(cap_thread<0 || sceKernelStartThread(cap_thread,0,NULL)<0)goto fail;
    xv_logf("[vertex-capture] core 0; %u KiB private inputs, %u ordered jobs; exact snapshots, drain before publication\n",
        XV_VERTEX_CAPTURE_BYTES>>10,CAPTURE_JOBS);
#if XV_VERTEX_CAPTURE_NOTIFY
    xv_logf("[vertex-capture-notify] enabled; coalesced events, ordered FIFO and finite timeout recovery retained\n");
#endif
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
    if(cap_retired==submitted) {
#if XV_VERTEX_CAPTURE_REUSE
        cap_reuse_retire();
#else
        cap_used=0;
#endif
        return;
    }
    uint64_t start=sceKernelGetProcessTimeWide();cap_drains++;
#if XV_VERTEX_CAPTURE_NOTIFY
    for(;;) {
        /* A plain store is insufficient on weakly ordered CPUs. The RMW
         * reads the worker's release RMW when completion wins this race. */
        __atomic_exchange_n(&cap_waiting,1,__ATOMIC_ACQ_REL);
        XV_CAPTURE_NOTIFY_POINT(CAP_DRAIN_ARMED);
        if(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)==submitted)break;
        XV_CAPTURE_NOTIFY_POINT(CAP_DRAIN_WAIT);
#else
    while(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)!=submitted) {
#endif
        unsigned bits;SceUInt timeout=1000;
        if(sceKernelWaitEventFlag(cap_done,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&bits,&timeout)<0)
            sceKernelDelayThread(100);
    }
#if XV_VERTEX_CAPTURE_NOTIFY
    __atomic_exchange_n(&cap_waiting,0,__ATOMIC_ACQ_REL);
#endif
    cap_collect();cap_join_us+=sceKernelGetProcessTimeWide()-start;
#if XV_VERTEX_CAPTURE_REUSE
    cap_reuse_retire();
#else
    cap_used=0;
#endif
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
           s->bytes>XV_VERTEX_CAPTURE_BYTES)goto fallback;
        unsigned captured=s->bytes;
#if XV_VERTEX_CAPTURE_PACKED
        if(cap_compact(s))captured/=2;
#endif
        unsigned aligned=(captured+15u)&~15u;
        if(aligned>XV_VERTEX_CAPTURE_BYTES-required)goto fallback;
        required+=aligned;
    }
    if(!cap_start())goto fallback;
    cap_collect();
    if(cap_detail_enabled<0)
        cap_detail_enabled=xv_quality_int("XV_VERTEX_CAPTURE_DETAIL",XV_VERTEX_CAPTURE_DETAIL_DEFAULT,0,1);
    int sample=cap_detail_enabled && !(cap_detail_serial++&63u);
    if(sample)cap_detail_samples++;
    unsigned submitted=__atomic_load_n(&cap_submitted,__ATOMIC_RELAXED);
#if XV_VERTEX_CAPTURE_READY
    uint64_t start=sceKernelGetProcessTimeWide();
    unsigned ready_ids[XV_VERTEX_PREPARE_STREAMS];
    const void *ready_results[XV_VERTEX_PREPARE_STREAMS];
    /* collect acquired every completed job. With no predecessor outstanding,
     * cap_results is immutable until this owner publishes another job. Preserve
     * callback order and never borrow a worker result still being written. */
    int probed=cap_retired==submitted;
    if(probed) {
        int ready=1;
        for(unsigned i=0;i<batch->count;i++) {
            const xv_vertex_prepare_stream *s=&batch->streams[i];
            unsigned packed=0,compact=0;
#if XV_PACKED_VERTEX_LAYOUT
            packed=s->packed;
#endif
#if XV_VERTEX_CAPTURE_PACKED
            compact=cap_compact(s);
#endif
            unsigned id=cap_reuse_find(s,batch->slot,packed,compact,sample);
            ready_ids[i]=id;ready_results[i]=id?cap_results[id-1]:NULL;
            if(!ready_results[i])ready=0;
        }
        if(ready) {
            /* Publish all-or-nothing, after every exact source comparison.
             * GPU-copy completion still belongs to the existing seal/wait. */
            for(unsigned i=0;i<batch->count;i++)*targets[i]=ready_results[i];
            if(complete)complete(context,1);
            cap_ready_draws++;
            cap_capture_us+=sceKernelGetProcessTimeWide()-start;
            return 1;
        }
    }
#endif
    if(submitted-cap_retired==CAPTURE_JOBS || required>XV_VERTEX_CAPTURE_BYTES-cap_used) {
#if XV_VERTEX_CAPTURE_READY
        cap_capture_us+=sceKernelGetProcessTimeWide()-start;
#endif
        cap_pressure++;xv_vertex_capture_drain();
        if(required>XV_VERTEX_CAPTURE_BYTES-cap_used) {
            /* Only a joined arena-pressure boundary discards retained bytes.
             * GPU-copy jobs read the separate uploader mirror, not this arena. */
            cap_used=0;
#if XV_VERTEX_CAPTURE_REUSE
            cap_reuse_reset();cap_reclaims++;
#endif
        }
#if XV_VERTEX_CAPTURE_READY
        /* A drain invalidates GPU results and can discard snapshot identities. */
        probed=0;start=sceKernelGetProcessTimeWide();
#endif
    }
#if !XV_VERTEX_CAPTURE_READY
    uint64_t start=sceKernelGetProcessTimeWide();
#endif
    capture_job *j=&cap_jobs[submitted&(CAPTURE_JOBS-1)];j->batch=*batch;
    j->complete=complete;j->context=context;
    unsigned written=0;
    for(unsigned i=0;i<batch->count;i++) {
        xv_vertex_prepare_stream *s=&j->batch.streams[i];
        j->identity[i]=s->source;j->targets[i]=targets[i];
        unsigned captured=s->bytes;
#if XV_VERTEX_PERSISTENT
        j->persistent[i]=vp_capture(batch->slot,s,&j->persistent_copy[i]);
        if(j->persistent[i]) {
            /* The FIFO owns an immutable cached mirror, including pending
             * hits. The worker must never dereference this live source. */
            s->source=NULL;s->refs=NULL;s->result=NULL;
            continue;
        }
#endif
#if XV_VERTEX_CAPTURE_REUSE
        unsigned packed=0,compact=0;
#if XV_PACKED_VERTEX_LAYOUT
        packed=s->packed;
#endif
#if XV_VERTEX_CAPTURE_PACKED
        compact=cap_compact(s);j->compact[i]=compact;
#endif
        unsigned id;
#if XV_VERTEX_CAPTURE_READY
        if(probed)id=ready_ids[i];
        else
#endif
        id=cap_reuse_find(s,batch->slot,packed,compact,sample);
        if(id) {
            j->reuse[i]=id;s->source=cap_arena+cap_entries[id-1].offset;s->result=NULL;
            /* Non-sparse references are semantically unused by upload(). */
            s->refs=NULL;
            continue;
        }
        j->reuse[i]=cap_reuse_add(s,batch->slot,packed,compact);
#endif
        uint64_t copy_start=sample?sceKernelGetProcessTimeWide():0;
#if XV_VERTEX_CAPTURE_PACKED
        j->compact[i]=cap_compact(s);
        if(j->compact[i]) {
            captured/=2;xv_packed_copy(cap_arena+cap_used,s->source,s->bytes/32);
            cap_compact_streams++;cap_compact_saved+=captured;
        } else
#endif
        memcpy(cap_arena+cap_used,s->source,captured);
        if(sample) {
            cap_detail_copy_us+=sceKernelGetProcessTimeWide()-copy_start;
            cap_detail_copies++;cap_detail_copy_bytes+=captured;
        }
        s->source=cap_arena+cap_used;s->result=NULL;
        cap_used+=(captured+15u)&~15u;
        written+=(captured+15u)&~15u;
        /* upload_snapshot only consults coverage for sparse raw inputs. Dense
         * streams take full-span validation, and packed streams discard refs.
         * Do not copy their 1 KiB masks into every queued job. Never retain the
         * producer's scratch pointer, even when the worker will ignore it. */
        int need_refs=xv_vertex_refs_sparse(s->refs,s->bytes,s->stride);
#if XV_PACKED_VERTEX_LAYOUT
        if(s->packed)need_refs=0;
#endif
        if(need_refs) { j->refs[i]=*s->refs;s->refs=&j->refs[i];cap_masks_copied++; }
        else { cap_masks_omitted+=s->refs!=NULL;s->refs=NULL; }
    }
    j->batch.ok=0;cap_jobs_total++;cap_bytes+=written;
    unsigned pending=submitted+1-cap_retired;if(pending>cap_max_pending)cap_max_pending=pending;
    uint64_t publish_start=sample?sceKernelGetProcessTimeWide():0;
    __atomic_store_n(&cap_submitted,submitted+1,__ATOMIC_RELEASE);
#if XV_VERTEX_CAPTURE_NOTIFY
    if(__atomic_exchange_n(&cap_wake_state,CAP_PENDING,__ATOMIC_ACQ_REL)==CAP_SLEEPING) {
        __atomic_fetch_add(&cap_wake_signals,1,__ATOMIC_RELAXED);
        if(sceKernelSetEventFlag(cap_wake,1)<0)
            __atomic_fetch_add(&cap_wake_failures,1,__ATOMIC_RELAXED);
    } else __atomic_fetch_add(&cap_wake_skips,1,__ATOMIC_RELAXED);
#else
    sceKernelSetEventFlag(cap_wake,1);
#endif
    if(sample) {
        cap_detail_publish_us+=sceKernelGetProcessTimeWide()-publish_start;
        cap_detail_publishes++;
    }
    cap_capture_us+=sceKernelGetProcessTimeWide()-start;
    return 1;
fallback:
    xv_vertex_capture_drain();return 0;
}
void xv_vertex_capture_begin_slot(unsigned slot)
{
    /* Caller acquired the GPU slot first; drain alone is not GPU retirement. */
    xv_vertex_capture_drain();
#if XV_VERTEX_PERSISTENT
    vp_retire_slot(slot);
#else
    (void)slot;
#endif
}
void xv_vertex_capture_shutdown(void)
{
    xv_vertex_capture_drain();
    if(cap_thread>=0) {
        __atomic_store_n(&cap_stopping,1,__ATOMIC_RELEASE);sceKernelSetEventFlag(cap_wake,1);
        while(sceKernelWaitThreadEnd(cap_thread,NULL,NULL)<0)sceKernelDelayThread(100);
    }
    cap_release();cap_unavailable=cap_stopping=0;cap_enabled=-1;
    cap_detail_enabled=-1;cap_detail_serial=0;
#if XV_VERTEX_PERSISTENT
    vp_shutdown();
#endif
    cap_submitted=cap_completed=cap_retired=cap_used=0;
#if XV_VERTEX_CAPTURE_PACKED
    cap_compact_enabled=-1;
#endif
#if XV_VERTEX_CAPTURE_REUSE
    cap_reuse_enabled=cap_retain_enabled=-1;cap_reuse_reset();
#endif
}
void xv_vertex_capture_report(unsigned frames)
{
    assert(cap_retired==__atomic_load_n(&cap_submitted,__ATOMIC_ACQUIRE));
#if XV_CAPTURE_TRUST_TAGS
    xv_logf("[capture-trust] %u frames: enabled %d disabled %u generation %u; trusted %u hits %llu KiB compare skipped; sampled verified %u mismatches %u\n",
        frames,trust_enabled,trust_disabled,__atomic_load_n(&trust_generation,__ATOMIC_RELAXED),
        trust_hits,(unsigned long long)(trust_bytes>>10),trust_verified,trust_mismatches);
    trust_hits=trust_verified=0;trust_bytes=0;
#endif
    if(cap_detail_samples)xv_logf("[vertex-capture-detail] %u frames: %u sampled submissions (1/64); compare %u calls %llu bytes %llu us; copy %u calls %llu bytes %llu us; publish %u calls %llu us; nested elapsed samples, not frame totals\n",
        frames,cap_detail_samples,cap_detail_compares,
        (unsigned long long)cap_detail_compare_bytes,(unsigned long long)cap_detail_compare_us,
        cap_detail_copies,(unsigned long long)cap_detail_copy_bytes,
        (unsigned long long)cap_detail_copy_us,cap_detail_publishes,
        (unsigned long long)cap_detail_publish_us);
    cap_detail_samples=cap_detail_compares=cap_detail_copies=cap_detail_publishes=0;
    cap_detail_compare_us=cap_detail_copy_us=cap_detail_publish_us=0;
    cap_detail_compare_bytes=cap_detail_copy_bytes=0;
#if XV_VERTEX_CAPTURE_NOTIFY
    /* Worker notification bookkeeping follows completed publication. Even a
     * joined report must exchange these atomically; boundary jobs can land in
     * either adjacent report. Shutdown's unconditional wake is not counted. */
    unsigned wake=__atomic_exchange_n(&cap_wake_signals,0,__ATOMIC_RELAXED);
    unsigned wake_skip=__atomic_exchange_n(&cap_wake_skips,0,__ATOMIC_RELAXED);
    unsigned done=__atomic_exchange_n(&cap_done_signals,0,__ATOMIC_RELAXED);
    unsigned done_skip=__atomic_exchange_n(&cap_done_skips,0,__ATOMIC_RELAXED);
    unsigned wake_fail=__atomic_exchange_n(&cap_wake_failures,0,__ATOMIC_RELAXED);
    unsigned done_fail=__atomic_exchange_n(&cap_done_notify_failures,0,__ATOMIC_RELAXED);
    xv_logf("[vertex-capture-notify] %u frames: wake signal/skip %u/%u done signal/skip %u/%u failed wake/done %u/%u; event attempts, ordered jobs unchanged\n",
        frames,wake,wake_skip,done,done_skip,wake_fail,done_fail);
#endif
#if XV_VERTEX_PERSISTENT
    vp_report(frames);
#endif
#if XV_VERTEX_CAPTURE_READY
    xv_logf("[vertex-capture-ready] %u frames: %u completed draws reused inline; exact inputs, ordered callbacks, existing GPU tickets retained\n",
        frames,cap_ready_draws);
    cap_ready_draws=0;
#endif
    if(cap_jobs_total)xv_logf("[vertex-capture] %u frames: %u jobs %llu KiB; capture %llu us worker %llu us join %llu us; %u drains %u pressure max-pending %u failed %u (overlapping window totals)\n",
        frames,cap_jobs_total,(unsigned long long)(cap_bytes>>10),(unsigned long long)cap_capture_us,
        (unsigned long long)cap_worker_us,(unsigned long long)cap_join_us,
        cap_drains,cap_pressure,cap_max_pending,cap_failures);
    cap_jobs_total=cap_drains=cap_pressure=cap_max_pending=cap_failures=0;
    cap_bytes=cap_capture_us=cap_worker_us=cap_join_us=0;
    if(cap_masks_copied || cap_masks_omitted)
        xv_logf("[vertex-capture-masks] %u frames: %u sparse masks copied / %u unused masks omitted; %llu KiB metadata writes avoided\n",
            frames,cap_masks_copied,cap_masks_omitted,
            (unsigned long long)cap_masks_omitted*sizeof(xv_vertex_refs)>>10);
    cap_masks_copied=cap_masks_omitted=0;
#if XV_VERTEX_CAPTURE_REUSE
    if(cap_reuse_checks)xv_logf("[vertex-capture-reuse] %u frames: %u checks %u exact hits; %llu KiB staging writes avoided; %u worker preparations reused\n",
        frames,cap_reuse_checks,cap_reuse_hits,(unsigned long long)(cap_reuse_bytes>>10),cap_reuse_prepared);
    cap_reuse_checks=cap_reuse_hits=cap_reuse_prepared=0;cap_reuse_bytes=0;
    if(cap_retained_hits || cap_reclaims)
        xv_logf("[vertex-capture-retain] %u frames: %u exact hits after drain, %llu KiB staging writes avoided; %u arena reclaims; CPU snapshots only, GPU results revalidated\n",
            frames,cap_retained_hits,(unsigned long long)(cap_retained_bytes>>10),cap_reclaims);
    cap_retained_hits=cap_reclaims=0;cap_retained_bytes=0;
#endif
#if XV_VERTEX_CAPTURE_PACKED
    if(cap_compact_streams)xv_logf("[vertex-capture-packed] %u frames: %u streams, %llu KiB staging writes avoided; exact 16-byte shader inputs\n",
        frames,cap_compact_streams,(unsigned long long)(cap_compact_saved>>10));
    cap_compact_streams=0;cap_compact_saved=0;
#endif
}
