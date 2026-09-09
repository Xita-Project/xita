#include "xv_vertex_upload.h"
#include "xv_frame_slots.h"
#include "xv_gpu_upload.h"
#include "xv_log.h"
#include "xv_bytes_equal.h"
#include "xv_snapshot_copy.h"
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <stdlib.h>
#include <string.h>

/* Each slot owns an uncached GPU snapshot plus a cached comparison mirror.
 * Repeated passes reuse byte-identical data; a same-frame rewrite appends a new
 * version. The source address alone is never an immutability proof. Allocated
 * lazily and retained between frames; no mapping/allocation on cache hits. */
#ifndef XV_VERTEX_UPLOAD_BYTES
#define XV_VERTEX_UPLOAD_BYTES (8u * 1024 * 1024)
#endif
#define UPLOAD_ENTRIES 2048u
#define UPLOAD_BUCKETS 256u
typedef struct {
    const void *source;
    unsigned bytes, offset, next;
} upload_entry;
static struct {
    SceUID gpu_uid, cpu_uid;
    uint8_t *gpu, *cpu;
    unsigned used, count, valid_bytes, bucket[UPLOAD_BUCKETS];
    upload_entry entries[UPLOAD_ENTRIES];
} pools[XV_FRAME_SLOTS];
static unsigned copies, reused, failures, high_water;
static uint64_t copied_bytes, compared_bytes;
static unsigned resident_checks, resident_hits;
static uint64_t resident_compared, resident_bytes;
static int resident_override = -1;
static int compare_override = -1;
static int copy_override = -1;
static unsigned fused_copies;
static uint64_t fused_bytes;
static int references_override = -1;
static unsigned reference_checks, reference_hits, reference_runs;
static uint64_t reference_requested, reference_compared;

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
static int vertex_equal(const void *a, const void *b, unsigned bytes)
{
    /* Small or frequently changed spans retain libc's short comparison path. */
    return bytes >= 64 && compare_enabled() ? xv_bytes_equal(a,b,bytes) : !memcmp(a,b,bytes);
}

void xv_vertex_upload_override(int enabled)
{ resident_override = enabled < 0 ? -1 : !!enabled; }
static int resident_enabled(void)
{
    static int configured, enabled;
    if (!configured) {
        const char *e = getenv("XV_VERTEX_RESIDENT");
        /* September 7 hardware: scalar residency raised stream preparation
         * from about 7.1 to 9.9 ms/frame. Revisit only in a separate comparison. */
        enabled = e && atoi(e) != 0; configured = 1;
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
                           unsigned stride, const xv_vertex_refs *refs)
{
    if (slot >= XV_FRAME_SLOTS || !source || !bytes || bytes > XV_VERTEX_UPLOAD_BYTES) goto fail;
    unsigned hash = ((uintptr_t)source >> 4) & (UPLOAD_BUCKETS - 1);
    for (unsigned n = pools[slot].bucket[hash]; n; n = pools[slot].entries[n-1].next) {
        upload_entry *e = &pools[slot].entries[n-1];
        if (e->source != source || e->bytes < bytes) continue;
        int match;
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
        resident_checks++; resident_compared += bytes; compared_bytes += bytes;
        resident = vertex_equal(source, pools[slot].cpu + off, bytes);
    }
    if (resident) {
        resident_hits++; resident_bytes += bytes;
    } else {
        if (copy_enabled()) {
            xv_snapshot_copy(pools[slot].cpu + off, pools[slot].gpu + off, source, bytes);
            fused_copies++; fused_bytes += bytes;
        } else {
            memcpy(pools[slot].cpu + off, source, bytes);
            memcpy(pools[slot].gpu + off, pools[slot].cpu + off, bytes);
        }
        copies++; copied_bytes += bytes;
        xv_gpu_flush(pools[slot].gpu + off, bytes);
    }
    if (off + aligned > pools[slot].valid_bytes) {
        /* Initialize newly exposed padding in both copies. Later frames may
         * consume it as vertex bytes after changing stream sizes or order. */
        memset(pools[slot].cpu + off + bytes, 0, aligned - bytes);
        memset(pools[slot].gpu + off + bytes, 0, aligned - bytes);
        pools[slot].valid_bytes = off + aligned;
    }
    pools[slot].entries[n] = (upload_entry){source, bytes, off, pools[slot].bucket[hash]};
    pools[slot].bucket[hash] = n + 1;
    pools[slot].used += aligned;
    if (pools[slot].used > high_water) high_water = pools[slot].used;
    return pools[slot].gpu + off;
fail:
    failures++; return NULL;
}
const void *xv_vertex_upload(unsigned slot, const void *source, unsigned bytes)
{ return upload(slot, source, bytes, 0, NULL); }
const void *xv_vertex_upload_referenced(unsigned slot, const void *source, unsigned bytes,
                                       unsigned stride, const xv_vertex_refs *refs)
{ return upload(slot, source, bytes, stride, refs); }
void xv_vertex_upload_reset(unsigned slot)
{
    if (slot >= XV_FRAME_SLOTS) return;
    /* Keep initialized matching bytes, but discard all current-frame source
     * identities. Reuse is always proved again by a full byte comparison. */
    pools[slot].used = pools[slot].count = 0;
    memset(pools[slot].bucket, 0, sizeof pools[slot].bucket);
}
void xv_vertex_upload_shutdown(void)
{
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
    xv_logf("[vertex-resident] %u frames: enabled %d; %u checks %u hits; compared %llu KiB skipped-upload %llu KiB; exact retired-slot bytes\n",
        frames, resident_enabled(), resident_checks, resident_hits,
        (unsigned long long)(resident_compared >> 10), (unsigned long long)(resident_bytes >> 10));
    xv_logf("[vertex-compare] %u frames: enabled %d; exact vector equality for cached spans >=64 bytes; upload residency unchanged\n",
        frames, compare_enabled());
    copies = reused = failures = high_water = 0; copied_bytes = compared_bytes = 0;
    resident_checks = resident_hits = 0; resident_compared = resident_bytes = 0;
}
