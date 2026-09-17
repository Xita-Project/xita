#include "xv_vertex_upload.h"
#include "xv_frame_slots.h"
#include "xv_gpu_upload.h"
#include "xv_log.h"
#include "xv_bytes_equal.h"
#include "xv_snapshot_copy.h"
#include "xv_quality_settings.h"
#include "xv_upload_worker.h"
#include "xv_vertex_profile.h"
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <string.h>

#ifndef XV_VERTEX_BLOCK_LOADS_DEFAULT
#define XV_VERTEX_BLOCK_LOADS_DEFAULT 0
#endif
#if XV_VERTEX_BLOCK_LOADS_DEFAULT != 0 && XV_VERTEX_BLOCK_LOADS_DEFAULT != 1
#error XV_VERTEX_BLOCK_LOADS_DEFAULT must be 0 or 1
#endif

#ifndef XV_VERTEX_RESIDENT_DEFAULT
#define XV_VERTEX_RESIDENT_DEFAULT 0
#endif
#if XV_VERTEX_RESIDENT_DEFAULT != 0 && XV_VERTEX_RESIDENT_DEFAULT != 1
#error XV_VERTEX_RESIDENT_DEFAULT must be 0 or 1
#endif

#ifndef XV_VERTEX_RESIDENT_REFERENCES_DEFAULT
#define XV_VERTEX_RESIDENT_REFERENCES_DEFAULT 0
#endif
#if XV_VERTEX_RESIDENT_REFERENCES_DEFAULT != 0 && XV_VERTEX_RESIDENT_REFERENCES_DEFAULT != 1
#error XV_VERTEX_RESIDENT_REFERENCES_DEFAULT must be 0 or 1
#endif

/* Each slot owns an uncached GPU snapshot plus a cached comparison mirror.
 * Repeated passes reuse byte-identical data; a same-frame rewrite appends a new
 * version. The source address alone is never an immutability proof. Allocated
 * lazily and retained between frames; no mapping/allocation on cache hits. */
#ifndef XV_VERTEX_UPLOAD_BYTES
#define XV_VERTEX_UPLOAD_BYTES (8u * 1024 * 1024)
#endif
#define UPLOAD_ENTRIES 2048u
#define UPLOAD_BUCKETS 256u
#ifndef XV_VERTEX_WORKER_BATCH
#define XV_VERTEX_WORKER_BATCH (64u * 1024u)
#endif
/* Optional in host tests; the Vita runtime links the concrete worker object. */
int xv_upload_worker_submit(void *,const void *,unsigned,uint32_t *) __attribute__((weak));
void xv_upload_worker_wait(uint32_t) __attribute__((weak));
void xv_upload_worker_report(unsigned) __attribute__((weak));
void xv_upload_worker_shutdown(void) __attribute__((weak));
int xv_upload_worker_snapshot(void *,const void *,unsigned) __attribute__((weak));
typedef struct {
    const void *source;
    unsigned bytes, offset, next;
#if XV_PACKED_VERTEX_LAYOUT
    unsigned layout;
#endif
} upload_entry;
static struct {
    SceUID gpu_uid, cpu_uid;
    uint8_t *gpu, *cpu;
    unsigned used, count, valid_bytes, bucket[UPLOAD_BUCKETS];
    unsigned dispatched, dirty_first, dirty_end, dirty_bytes;
    uint32_t ticket;
    int started, asynchronous, has_ticket;
    upload_entry entries[UPLOAD_ENTRIES];
} pools[XV_FRAME_SLOTS];
static unsigned copies, reused, failures, high_water;
static uint64_t copied_bytes, compared_bytes;
static unsigned resident_checks, resident_hits;
static uint64_t resident_compared, resident_bytes;
static unsigned resident_reference_checks, resident_reference_hits, resident_reference_runs;
static uint64_t resident_reference_requested, resident_reference_compared;
static int resident_references_enabled(void)
{
    static int configured = -1;
    if (configured < 0) {
        const char *e = getenv("XV_VERTEX_RESIDENT_REFERENCES");
        configured = e ? atoi(e) != 0 : XV_VERTEX_RESIDENT_REFERENCES_DEFAULT;
    }
    return configured;
}
#if XV_PACKED_VERTEX_LAYOUT
static unsigned packed_calls;
static uint64_t packed_vertices;
static int packed_equal(const void *a,const void *b,unsigned vertices)
{
    uint64_t profile=xv_vertex_work_begin();
    int equal=xv_packed_equal(a,b,vertices);
    xv_vertex_work_end(equal?XV_VERTEX_EQUAL:XV_VERTEX_DIFFERENT,vertices*16,profile);
    return equal;
}
#endif
/* Producer-side traffic, independent of asynchronously sampled worker totals.
 * Dirty envelopes include unchanged internal gaps to bound dispatch count. */
static uint64_t gpu_queued_bytes, gpu_caller_bytes, gpu_clean_bytes, gpu_overcopy_bytes;
static unsigned gpu_queued_batches, gpu_caller_batches, gpu_clean_batches;
static int resident_override = -1;
static int compare_override = -1;
static int blocks_override = -1;
static unsigned block_checks;
static uint64_t block_bytes;
static int copy_override = -1;
static unsigned fused_copies;
static uint64_t fused_bytes;
static int references_override = -1;
static unsigned reference_checks, reference_hits, reference_runs;
static uint64_t reference_requested, reference_compared;
static int worker_override = -1;
static int snapshot_worker_override = -1;
void xv_snapshot_worker_override(int enabled)
{ snapshot_worker_override = enabled < 0 ? -1 : !!enabled; }
static int snapshot_worker_enabled(void)
{
    static int configured = -1;
    if (configured < 0) configured = xv_quality_int("XV_SNAPSHOT_WORKER",0,0,1);
    return snapshot_worker_override < 0 ? configured : snapshot_worker_override;
}
void xv_vertex_worker_override(int enabled)
{ worker_override = enabled < 0 ? -1 : !!enabled; }
int xv_vertex_worker_enabled(void)
{
    static int configured = -1;
    if (configured < 0) configured = xv_quality_int("XV_VERTEX_WORKER",0,0,1);
    return worker_override < 0 ? configured : worker_override;
}

static void dispatch(unsigned slot, int final)
{
    unsigned first=pools[slot].dispatched, bytes=pools[slot].used-first;
    if (!pools[slot].asynchronous || !bytes || (!final && bytes<XV_VERTEX_WORKER_BATCH)) return;
    unsigned dirty=pools[slot].dirty_end-pools[slot].dirty_first;
    gpu_clean_bytes+=bytes-dirty;
    if (!dirty) {
        /* A clean tail must not erase an earlier still-pending copy ticket.
         * Retired storage already matches; no new GPU stores need a barrier. */
        gpu_clean_batches++;
        pools[slot].dispatched=pools[slot].used;
        return;
    }
    first=pools[slot].dirty_first;
    bytes=dirty;
    gpu_overcopy_bytes+=bytes-pools[slot].dirty_bytes;
    uint64_t profile = xv_vertex_work_begin();
    uint8_t *dst=pools[slot].gpu+first;
    const uint8_t *src=pools[slot].cpu+first;
    uint32_t ticket;
    if (bytes>=4096 && xv_upload_worker_submit(dst,src,bytes,&ticket)) {
        pools[slot].ticket=ticket; pools[slot].has_ticket=1;
        gpu_queued_batches++; gpu_queued_bytes+=bytes;
    } else {
        memcpy(dst,src,bytes);
        xv_gpu_flush(dst,bytes);
        gpu_caller_batches++; gpu_caller_bytes+=bytes;
    }
    pools[slot].dispatched=pools[slot].used;
    pools[slot].dirty_first=pools[slot].dirty_end=pools[slot].dirty_bytes=0;
    xv_vertex_work_end(XV_VERTEX_DISPATCH,bytes,profile);
}

void xv_vertex_upload_seal(unsigned slot)
{ if (slot<XV_FRAME_SLOTS) dispatch(slot,1); }
void xv_vertex_upload_wait(unsigned slot)
{
    if (slot<XV_FRAME_SLOTS && pools[slot].has_ticket)
        xv_upload_worker_wait(pools[slot].ticket);
}
const void *xv_vertex_upload_readback(unsigned slot, const void *ptr)
{
    if (slot<XV_FRAME_SLOTS && pools[slot].gpu) {
        uintptr_t offset=(uintptr_t)ptr-(uintptr_t)pools[slot].gpu;
        if (offset<pools[slot].used) return pools[slot].cpu+offset;
    }
    return ptr; /* immediate vertices are already complete */
}

void xv_vertex_references_override(int enabled)
{ references_override = enabled < 0 ? -1 : !!enabled; }
int xv_vertex_references_enabled(void)
{
    static int configured = -1;
    if (configured < 0) {
        const char *e = getenv("XV_VERTEX_REFERENCES");
        configured = e && atoi(e) != 0;
    }
    return references_override < 0 ? configured : references_override;
}

void xv_vertex_copy_override(int enabled)
{ copy_override = enabled < 0 ? -1 : !!enabled; }
static int copy_enabled(void)
{
    static int configured = -1;
    if (configured < 0) {
        const char *e = getenv("XV_VERTEX_COPY_NEON");
        configured = e && atoi(e) != 0; /* opt-in until hardware comparison */
    }
    return copy_override < 0 ? configured : copy_override;
}

void xv_vertex_compare_override(int enabled)
{ compare_override = enabled < 0 ? -1 : !!enabled; }
static int compare_enabled(void)
{
    static int configured, enabled;
    if (!configured) {
        const char *e = getenv("XV_VERTEX_COMPARE_NEON");
        enabled = !e || atoi(e) != 0; configured = 1;
    }
    return compare_override < 0 ? enabled : compare_override;
}
void xv_vertex_blocks_override(int enabled)
{ blocks_override = enabled < 0 ? -1 : !!enabled; }
int xv_vertex_blocks_enabled(void)
{
    static int configured = -1;
    if (configured < 0)
        configured = xv_quality_int("XV_VERTEX_BLOCK_LOADS",XV_VERTEX_BLOCK_LOADS_DEFAULT,0,1);
    return blocks_override < 0 ? configured : blocks_override;
}
int xv_vertex_blocks_available(void)
{
#if defined(__ARM_NEON)
    return compare_enabled();
#else
    return 0;
#endif
}
static int vertex_equal(const void *a, const void *b, unsigned bytes)
{
    /* Small or frequently changed spans retain libc's short comparison path. */
    uint64_t profile = xv_vertex_work_begin();
    int equal;
    if (bytes >= 64 && compare_enabled()) {
        if (xv_vertex_blocks_enabled()) {
            block_checks++; block_bytes += bytes;
            equal = xv_bytes_equal_blocks(a,b,bytes);
        } else equal = xv_bytes_equal(a,b,bytes);
    } else equal = !memcmp(a,b,bytes);
    xv_vertex_work_end(equal ? XV_VERTEX_EQUAL : XV_VERTEX_DIFFERENT,bytes,profile);
    return equal;
}

void xv_vertex_upload_override(int enabled)
{ resident_override = enabled < 0 ? -1 : !!enabled; }
static int resident_enabled(void)
{
    static int configured, enabled;
    if (!configured) {
        const char *e = getenv("XV_VERTEX_RESIDENT");
        /* Preserve explicit-value parsing, including empty, malformed and
         * negative strings. Only an absent value uses the build default. */
        enabled = e ? atoi(e) != 0 : XV_VERTEX_RESIDENT_DEFAULT;
        configured = 1;
        xv_logf("[vertex-resident] startup mode %d (build default %d); exact retired-slot reuse\n",
            resident_override < 0 ? enabled : resident_override,
            XV_VERTEX_RESIDENT_DEFAULT);
    }
    return resident_override < 0 ? enabled : resident_override;
}

static int allocate(unsigned slot)
{
    SceUID gpu = sceKernelAllocMemBlock("xv_vertices_gpu",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, XV_VERTEX_UPLOAD_BYTES, NULL);
    if (gpu < 0) return 0;
    SceUID cpu = sceKernelAllocMemBlock("xv_vertices_cpu",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, XV_VERTEX_UPLOAD_BYTES, NULL);
    if (cpu < 0) { sceKernelFreeMemBlock(gpu); return 0; }
    void *g = NULL, *c = NULL;
    if (sceKernelGetMemBlockBase(gpu, &g) < 0 || sceKernelGetMemBlockBase(cpu, &c) < 0 ||
        sceGxmMapMemory(g, XV_VERTEX_UPLOAD_BYTES, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        sceKernelFreeMemBlock(gpu); sceKernelFreeMemBlock(cpu); return 0;
    }
    pools[slot].gpu_uid = gpu; pools[slot].cpu_uid = cpu;
    pools[slot].gpu = g; pools[slot].cpu = c;
    return 1;
}
static const void *upload(unsigned slot, const void *source, unsigned bytes,
                           unsigned stride, const xv_vertex_refs *refs
#if XV_PACKED_VERTEX_LAYOUT
                           ,unsigned layout
#endif
                           )
{
    if (slot >= XV_FRAME_SLOTS || !source || !bytes || bytes > XV_VERTEX_UPLOAD_BYTES) goto fail;
    if (!pools[slot].started) {
        pools[slot].asynchronous=xv_vertex_worker_enabled() && xv_upload_worker_submit &&
            xv_upload_worker_wait && xv_upload_worker_shutdown;
        pools[slot].started=1; /* latch for this slot generation, including benchmark transitions */
    }
    unsigned hash = ((uintptr_t)source >> 4) & (UPLOAD_BUCKETS - 1);
    for (unsigned n = pools[slot].bucket[hash]; n; n = pools[slot].entries[n-1].next) {
        upload_entry *e = &pools[slot].entries[n-1];
        if (e->source != source || e->bytes < bytes) continue;
#if XV_PACKED_VERTEX_LAYOUT
        if(e->layout!=layout)continue;
#endif
        int match;
#if XV_PACKED_VERTEX_LAYOUT
        if(layout) {
            compared_bytes+=bytes;
            match=packed_equal(source,pools[slot].cpu+e->offset,bytes/16);
        } else
#endif
        if (xv_vertex_refs_sparse(refs, bytes, stride)) {
            uint64_t checked = 0;
            reference_checks++; reference_requested += bytes;
            match = xv_vertex_refs_equal(refs, stride, source, pools[slot].cpu + e->offset,
                                         vertex_equal, &checked, &reference_runs);
            reference_compared += checked; compared_bytes += checked;
            reference_hits += !!match;
        } else {
            compared_bytes += bytes;
            match = vertex_equal(source, pools[slot].cpu + e->offset, bytes);
        }
        if (match) {
            reused++; return pools[slot].gpu + e->offset;
        }
    }
    unsigned aligned = (bytes + 15u) & ~15u;
    if (aligned > XV_VERTEX_UPLOAD_BYTES - pools[slot].used ||
        pools[slot].count == UPLOAD_ENTRIES) goto fail;
    if (!pools[slot].gpu && !allocate(slot)) goto fail;
    unsigned off = pools[slot].used, n = pools[slot].count++;
    /* The owner acquired this slot after its GPU notification completed. The
     * old cached mirror and GPU bytes still agree, including initialized
     * alignment gaps. Compare the destination range, not the source address:
     * reordering draws or reusing guest memory cannot create a false hit.
     * Current-frame versions still append and can never overwrite one another. */
    int resident = 0;
    if (resident_enabled() && off + bytes <= pools[slot].valid_bytes) {
        resident_checks++;
        uint64_t checked = bytes;
#if XV_PACKED_VERTEX_LAYOUT
        if (layout) resident = packed_equal(source,pools[slot].cpu+off,bytes/16);
        else
#endif
        if (resident_references_enabled() && xv_vertex_refs_sparse(refs, bytes, stride)) {
            /* The mask belongs to this draw's retained indices. Unfetched
             * records may remain old in BOTH copies; every later draw still
             * validates its own mask/full span. Never update only the mirror.
             * Slot retirement, initialized extent and append order are unchanged. */
            checked = 0;
            resident_reference_checks++; resident_reference_requested += bytes;
            resident = xv_vertex_refs_equal(refs, stride, source, pools[slot].cpu + off,
                vertex_equal, &checked, &resident_reference_runs);
            resident_reference_hits += !!resident;
            resident_reference_compared += checked;
        } else resident = vertex_equal(source, pools[slot].cpu + off, bytes);
        resident_compared += checked; compared_bytes += checked;
    }
    if (resident) {
        resident_hits++; resident_bytes += bytes;
    } else {
        uint64_t profile = xv_vertex_work_begin();
#if XV_PACKED_VERTEX_LAYOUT
        if(layout) {
            xv_packed_copy(pools[slot].cpu+off,source,bytes/16);
            if(!pools[slot].asynchronous)
                memcpy(pools[slot].gpu+off,pools[slot].cpu+off,bytes);
        } else
#endif
        if (pools[slot].asynchronous) {
            /* Guest memory can change immediately after this call. A shared
             * initial copy joins its source loan here; later asynchronous GPU
             * copies read only this append-only cached mirror. */
            if (!snapshot_worker_enabled() || !xv_upload_worker_snapshot ||
                !xv_upload_worker_snapshot(pools[slot].cpu + off,source,bytes))
                memcpy(pools[slot].cpu + off, source, bytes);
        } else if (copy_enabled()) {
            xv_snapshot_copy(pools[slot].cpu + off, pools[slot].gpu + off, source, bytes);
            fused_copies++; fused_bytes += bytes;
        } else {
            memcpy(pools[slot].cpu + off, source, bytes);
            memcpy(pools[slot].gpu + off, pools[slot].cpu + off, bytes);
        }
        copies++; copied_bytes += bytes;
        if (pools[slot].asynchronous) {
            /* Appends are disjoint. One envelope preserves the old batching
             * cadence and never submits more jobs than the full-prefix path.
             * Include alignment so residency OFF keeps its original ranges. */
            if (!pools[slot].dirty_end) pools[slot].dirty_first=off;
            pools[slot].dirty_end=off+aligned;
            pools[slot].dirty_bytes+=bytes;
        } else gpu_caller_bytes+=bytes;
        xv_gpu_flush(pools[slot].gpu + off, bytes);
        xv_vertex_work_end(XV_VERTEX_SNAPSHOT,bytes,profile);
    }
    if (off + aligned > pools[slot].valid_bytes) {
        /* Initialize newly exposed padding in both copies. Later frames may
         * consume it as vertex bytes after changing stream sizes or order. */
        memset(pools[slot].cpu + off + bytes, 0, aligned - bytes);
        if (!pools[slot].asynchronous) {
            memset(pools[slot].gpu + off + bytes, 0, aligned - bytes);
            gpu_caller_bytes+=aligned-bytes;
        } else pools[slot].dirty_bytes+=aligned-bytes;
        pools[slot].valid_bytes = off + aligned;
    }
    pools[slot].entries[n] = (upload_entry){.source=source,.bytes=bytes,.offset=off,.next=pools[slot].bucket[hash]};
#if XV_PACKED_VERTEX_LAYOUT
    pools[slot].entries[n].layout=layout;
#endif
    pools[slot].bucket[hash] = n + 1;
    pools[slot].used += aligned;
    dispatch(slot,0);
    if (pools[slot].used > high_water) high_water = pools[slot].used;
    return pools[slot].gpu + off;
fail:
    failures++; return NULL;
}
const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes)
{ return upload(slot, source, bytes, 0, NULL
#if XV_PACKED_VERTEX_LAYOUT
    ,0
#endif
    ); }
const void *xv_vertex_upload_referenced(unsigned slot, const void *source, unsigned bytes,
                                       unsigned stride, const xv_vertex_refs *refs)
{ return upload(slot, source, bytes, stride, refs
#if XV_PACKED_VERTEX_LAYOUT
    ,0
#endif
    ); }
#if XV_PACKED_VERTEX_LAYOUT
const void *xv_vertex_upload_packed(unsigned slot,const void *source,unsigned vertices)
{
    if(!vertices || vertices>XV_VERTEX_UPLOAD_BYTES/16 || vertices>UINT32_MAX/32) {
        failures++;return NULL;
    }
    packed_calls++;packed_vertices+=vertices;
    return upload(slot,source,vertices*16,0,NULL,XV_PACKED_PREFIX16);
}
#endif
void xv_vertex_upload_reset(unsigned slot)
{
    if (slot >= XV_FRAME_SLOTS) return;
    /* GPU retirement remains the caller's responsibility. Also join any
     * queued CPU copies before this mirror is reused, even for a dropped frame. */
    xv_vertex_upload_seal(slot);
    xv_vertex_upload_wait(slot);
    pools[slot].dispatched=0; pools[slot].has_ticket=0;
    pools[slot].dirty_first=pools[slot].dirty_end=pools[slot].dirty_bytes=0;
    pools[slot].started=pools[slot].asynchronous=0;
    /* Keep initialized matching bytes, but discard all current-frame source
     * identities. Reuse is always proved again by a full byte comparison. */
    pools[slot].used = pools[slot].count = 0;
    memset(pools[slot].bucket, 0, sizeof pools[slot].bucket);
}
void xv_vertex_upload_shutdown(void)
{
    for (unsigned s=0;s<XV_FRAME_SLOTS;s++) xv_vertex_upload_seal(s);
    if (xv_upload_worker_shutdown) xv_upload_worker_shutdown();
    for (unsigned s = 0; s < XV_FRAME_SLOTS; ++s) {
        if (pools[s].gpu) {
            sceGxmUnmapMemory(pools[s].gpu);
            sceKernelFreeMemBlock(pools[s].gpu_uid);
            sceKernelFreeMemBlock(pools[s].cpu_uid);
        }
        memset(&pools[s], 0, sizeof pools[s]);
    }
}
void xv_vertex_upload_report(unsigned frames)
{
#if XV_PACKED_VERTEX_LAYOUT
    xv_logf("[vertex-packed] %u frames: %u requests source %llu KiB / payload %llu KiB; exact prefix16, requested spans not GPU writes\n",
        frames,packed_calls,(unsigned long long)(packed_vertices*32>>10),(unsigned long long)(packed_vertices*16>>10));
    packed_calls=0;packed_vertices=0;
#endif
    xv_vertex_work_report(frames);
    if (xv_upload_worker_report) xv_upload_worker_report(frames);
    xv_logf("[vertex-block-loads] %u frames: enabled %d checks %u requested-KiB %llu; exact 64-byte groups and original tails\n",
        frames,xv_vertex_blocks_enabled(),block_checks,(unsigned long long)(block_bytes>>10));
    block_checks=0;block_bytes=0;
    xv_logf("[vertex-references] %u frames: enabled %d; %u checks / %u hits / %u runs; requested %llu KiB compared %llu KiB; indexed records checked, owned uploads retained\n",
        frames, xv_vertex_references_enabled(), reference_checks, reference_hits, reference_runs,
        (unsigned long long)(reference_requested >> 10), (unsigned long long)(reference_compared >> 10));
    reference_checks = reference_hits = reference_runs = 0;
    reference_requested = reference_compared = 0;
    xv_logf("[vertex-copy] %u frames: enabled %d; %u fused copies / %llu KiB; both owned snapshots retained\n",
        frames, copy_enabled(), fused_copies, (unsigned long long)(fused_bytes >> 10));
    fused_copies = 0; fused_bytes = 0;
    xv_logf("[vertex-upload] %u frames: %u copies %u reused %u failures; copied %llu KiB compared %llu KiB; max slot %u/%u KiB\n",
        frames, copies, reused, failures, (unsigned long long)(copied_bytes >> 10),
        (unsigned long long)(compared_bytes >> 10), high_water >> 10, XV_VERTEX_UPLOAD_BYTES >> 10);
    xv_logf("[vertex-resident] %u frames: enabled %d; %u checks %u hits; compared %llu KiB avoided-snapshot %llu KiB; validated retired snapshot data\n",
        frames, resident_enabled(), resident_checks, resident_hits,
        (unsigned long long)(resident_compared >> 10), (unsigned long long)(resident_bytes >> 10));
    xv_logf("[vertex-resident-references] %u frames: enabled %d; %u checks / %u hits / %u runs; requested %llu KiB compared %llu KiB; raw fetched groups, later draws revalidate\n",
        frames,resident_references_enabled(),resident_reference_checks,resident_reference_hits,
        resident_reference_runs,(unsigned long long)(resident_reference_requested>>10),
        (unsigned long long)(resident_reference_compared>>10));
    resident_reference_checks=resident_reference_hits=resident_reference_runs=0;
    resident_reference_requested=resident_reference_compared=0;
    xv_logf("[vertex-transfer] %u frames: GPU queued %u batches %llu KiB / caller %llu KiB (%u async fallbacks/tails); clean %u batches %llu KiB omitted / internal-gap-padding %llu KiB recopied; producer ranges, completion totals overlap\n",
        frames,gpu_queued_batches,(unsigned long long)(gpu_queued_bytes>>10),
        (unsigned long long)(gpu_caller_bytes>>10),gpu_caller_batches,gpu_clean_batches,
        (unsigned long long)(gpu_clean_bytes>>10),(unsigned long long)(gpu_overcopy_bytes>>10));
    gpu_queued_batches=gpu_caller_batches=gpu_clean_batches=0;
    gpu_queued_bytes=gpu_caller_bytes=gpu_clean_bytes=gpu_overcopy_bytes=0;
    xv_logf("[vertex-compare] %u frames: enabled %d; exact vector equality for cached spans >=64 bytes; upload residency unchanged\n",
        frames, compare_enabled());
    copies = reused = failures = high_water = 0; copied_bytes = compared_bytes = 0;
    resident_checks = resident_hits = 0; resident_compared = resident_bytes = 0;
}
