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
#ifndef XV_VERTEX_CAPTURE_REUSE
#define XV_VERTEX_CAPTURE_REUSE 0
#endif
#if XV_VERTEX_CAPTURE_REUSE != 0 && XV_VERTEX_CAPTURE_REUSE != 1
#error XV_VERTEX_CAPTURE_REUSE must be 0 or 1
#endif
typedef struct {
    xv_vertex_prepare_batch batch;
    const void *identity[XV_VERTEX_PREPARE_STREAMS];
    const void **targets[XV_VERTEX_PREPARE_STREAMS];
    xv_vertex_refs refs[XV_VERTEX_PREPARE_STREAMS];
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
    unsigned offset,bytes,stride,packed,compact,slot,next,current;
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
static void cap_reuse_retire(void)
{
    if(cap_retain_enabled<0)
        cap_retain_enabled=xv_quality_int("XV_VERTEX_CAPTURE_RETAIN",1,0,1);
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
static unsigned cap_reuse_find(const xv_vertex_prepare_stream *s,unsigned slot,unsigned packed,unsigned compact)
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
        int equal;
#if XV_VERTEX_CAPTURE_PACKED
        if(compact)equal=xv_packed_equal(s->source,cap_arena+e->offset,s->bytes/32);
        else
#endif
        /* We need equality, not ordering. Use the same bounded NEON block
         * loads as resident uploads, including unaligned inputs and tails. */
        equal=xv_bytes_equal_blocks(s->source,cap_arena+e->offset,s->bytes);
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
    cap_entries[id-1]=(capture_entry){s->source,cap_used,s->bytes,s->stride,packed,compact,slot,cap_buckets[bucket],1};
    cap_results[id-1]=NULL;cap_buckets[bucket]=id;return id;
}
#endif

static void cap_execute(capture_job *job)
{
    xv_vertex_prepare_batch *b=&job->batch;b->ok=0;
    for(unsigned i=0;i<b->count;i++) {
        xv_vertex_prepare_stream *s=&b->streams[i];unsigned packed=0;
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
    if(cap_retired==submitted) {
#if XV_VERTEX_CAPTURE_REUSE
        cap_reuse_retire();
#else
        cap_used=0;
#endif
        return;
    }
    uint64_t start=sceKernelGetProcessTimeWide();cap_drains++;
    while(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)!=submitted) {
        unsigned bits;SceUInt timeout=1000;
        if(sceKernelWaitEventFlag(cap_done,1,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&bits,&timeout)<0)
            sceKernelDelayThread(100);
    }
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
    unsigned submitted=__atomic_load_n(&cap_submitted,__ATOMIC_RELAXED);
    if(submitted-cap_retired==CAPTURE_JOBS || required>XV_VERTEX_CAPTURE_BYTES-cap_used) {
        cap_pressure++;xv_vertex_capture_drain();
        if(required>XV_VERTEX_CAPTURE_BYTES-cap_used) {
            /* Only a joined arena-pressure boundary discards retained bytes.
             * GPU-copy jobs read the separate uploader mirror, not this arena. */
            cap_used=0;
#if XV_VERTEX_CAPTURE_REUSE
            cap_reuse_reset();cap_reclaims++;
#endif
        }
    }
    uint64_t start=sceKernelGetProcessTimeWide();
    capture_job *j=&cap_jobs[submitted&(CAPTURE_JOBS-1)];j->batch=*batch;
    j->complete=complete;j->context=context;
    unsigned written=0;
    for(unsigned i=0;i<batch->count;i++) {
        xv_vertex_prepare_stream *s=&j->batch.streams[i];
        j->identity[i]=s->source;j->targets[i]=targets[i];
        unsigned captured=s->bytes;
#if XV_VERTEX_CAPTURE_REUSE
        unsigned packed=0,compact=0;
#if XV_PACKED_VERTEX_LAYOUT
        packed=s->packed;
#endif
#if XV_VERTEX_CAPTURE_PACKED
        compact=cap_compact(s);j->compact[i]=compact;
#endif
        unsigned id=cap_reuse_find(s,batch->slot,packed,compact);
        if(id) {
            j->reuse[i]=id;s->source=cap_arena+cap_entries[id-1].offset;s->result=NULL;
            /* Non-sparse references are semantically unused by upload(). */
            s->refs=NULL;
            continue;
        }
        j->reuse[i]=cap_reuse_add(s,batch->slot,packed,compact);
#endif
#if XV_VERTEX_CAPTURE_PACKED
        j->compact[i]=cap_compact(s);
        if(j->compact[i]) {
            captured/=2;xv_packed_copy(cap_arena+cap_used,s->source,s->bytes/32);
            cap_compact_streams++;cap_compact_saved+=captured;
        } else
#endif
        memcpy(cap_arena+cap_used,s->source,captured);
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
