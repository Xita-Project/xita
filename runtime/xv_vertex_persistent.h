#ifndef XV_VERTEX_PERSISTENT_H
#define XV_VERTEX_PERSISTENT_H
/* Private to the capture FIFO. Metadata and cached mirrors have one recording
 * owner. The worker only reads mirrors and writes newly allocated GPU ranges.
 * A slot bit is released only AFTER that slot's previous GPU use retires and
 * the CPU FIFO drains. Neither a CPU drain nor a map change retires GPU users. */
#include <psp2/gxm.h>
#ifndef XV_VERTEX_PERSISTENT_BYTES
#define XV_VERTEX_PERSISTENT_BYTES (2u*1024u*1024u)
#endif
#define VP_PAGE 4096u
#define VP_PAGES (XV_VERTEX_PERSISTENT_BYTES/VP_PAGE)
#ifndef VP_ENTRIES
#define VP_ENTRIES 256u
#endif
#define VP_BUCKETS 128u
_Static_assert(XV_VERTEX_PERSISTENT_BYTES>=VP_PAGE && !(XV_VERTEX_PERSISTENT_BYTES%VP_PAGE),"persistent vertex pages");
typedef struct {
    const void *identity;
    unsigned bytes,stride,first,pages,pins,next;
    uint64_t generation;
} vp_entry;
static vp_entry vp_entries[VP_ENTRIES];
static unsigned vp_pages[VP_PAGES],vp_buckets[VP_BUCKETS];
static SceUID vp_cpu_uid=-1,vp_gpu_uid=-1;
static uint8_t *vp_cpu,*vp_gpu;
static int vp_configured=-1,vp_unavailable;
static unsigned vp_hits,vp_created,vp_full;
static uint64_t vp_compared,vp_saved,vp_copied;
static unsigned vp_hash(const void *p,unsigned bytes,unsigned stride)
{ return (((uintptr_t)p>>4)^bytes^(stride<<3))&(VP_BUCKETS-1u); }
static void vp_release(void)
{
    if(vp_gpu)sceGxmUnmapMemory(vp_gpu);
    if(vp_gpu_uid>=0)sceKernelFreeMemBlock(vp_gpu_uid);
    if(vp_cpu_uid>=0)sceKernelFreeMemBlock(vp_cpu_uid);
    vp_cpu_uid=vp_gpu_uid=-1;vp_cpu=vp_gpu=NULL;
}
static int vp_start(void)
{
    if(vp_gpu)return 1;
    if(vp_unavailable)return 0;
    vp_cpu_uid=sceKernelAllocMemBlock("xv_persistent_cpu",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,XV_VERTEX_PERSISTENT_BYTES,NULL);
    if(vp_cpu_uid<0 || sceKernelGetMemBlockBase(vp_cpu_uid,(void **)&vp_cpu)<0 || !vp_cpu)goto fail;
    vp_gpu_uid=sceKernelAllocMemBlock("xv_persistent_gpu",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,XV_VERTEX_PERSISTENT_BYTES,NULL);
    void *gpu=NULL;
    if(vp_gpu_uid<0 || sceKernelGetMemBlockBase(vp_gpu_uid,&gpu)<0 || !gpu ||
       sceGxmMapMemory(gpu,XV_VERTEX_PERSISTENT_BYTES,SCE_GXM_MEMORY_ATTRIB_READ)<0)goto fail;
    vp_gpu=gpu;return 1;
fail:
    vp_release();vp_unavailable=1;return 0;
}
/* Generation handles cannot alias a recycled GPU allocation. Never wrap. */
static uint64_t vp_generation;
static unsigned vp_create(unsigned slot,const xv_vertex_prepare_stream *s,
                          const void *data,unsigned *copy)
{
    unsigned bucket=vp_hash(s->source,s->bytes,s->stride);
    if(vp_generation==UINT64_MAX || !vp_start())return 0;
    unsigned count=(s->bytes+VP_PAGE-1u)/VP_PAGE,run=0,first=VP_PAGES,id=0;
    for(unsigned i=0;i<VP_ENTRIES;i++)if(!vp_entries[i].pins) { id=i+1u;break; }
    if(!id) { vp_full++;return 0; }
    for(unsigned i=0;i<VP_PAGES;i++) {
        run=vp_pages[i]?0:run+1;
        if(run==count) { first=i+1u-count;break; }
    }
    if(first==VP_PAGES) { vp_full++;return 0; }
    for(unsigned i=0;i<count;i++)vp_pages[first+i]=id;
    vp_entries[id-1]=(vp_entry){s->source,s->bytes,s->stride,first,count,1u<<slot,vp_buckets[bucket],++vp_generation};
    vp_buckets[bucket]=id;
    memcpy(vp_cpu+first*VP_PAGE,data,s->bytes);
    *copy=1;vp_created++;return id;
}
/* Called only with an accepted FIFO job that will be published. Pending hits
 * are safe: the creating job precedes the hit in the same FIFO. Every creation
 * is copied in the worker's first pass, even if another stream later fails. */
static unsigned vp_capture(unsigned slot,const xv_vertex_prepare_stream *s,unsigned *copy)
{
    *copy=0;
    if(vp_configured<0)vp_configured=xv_quality_int("XV_VERTEX_PERSISTENT",1,0,1);
    if(!vp_configured || s->bytes<256u || s->bytes>256u*1024u ||
       xv_vertex_refs_sparse(s->refs,s->bytes,s->stride))return 0;
#if XV_PACKED_VERTEX_LAYOUT
    if(s->packed)return 0;
#endif
    unsigned bucket=vp_hash(s->source,s->bytes,s->stride);
    for(unsigned id=vp_buckets[bucket];id;id=vp_entries[id-1].next) {
        vp_entry *e=&vp_entries[id-1];
        if(e->identity!=s->source || e->bytes!=s->bytes || e->stride!=s->stride)continue;
        vp_compared+=s->bytes;
        if(xv_bytes_equal_blocks(s->source,vp_cpu+e->first*VP_PAGE,s->bytes)) {
            e->pins|=1u<<slot;vp_hits++;vp_saved+=s->bytes;return id;
        }
        /* A changed source gets a new immutable version. Older versions can
         * still be referenced by this or another frame; never overwrite them. */
        break;
    }
    return vp_create(slot,s,s->source,copy);
}

#if XV_VERTEX_CAPTURE_REUSE
typedef struct { unsigned id; uint64_t generation; } vp_snapshot_link;
static int vp_snapshot_configured=-1;
static unsigned vp_snapshot_hits;
static int vp_snapshot_enabled(void)
{
    if(vp_snapshot_configured<0)
        vp_snapshot_configured=xv_quality_int("XV_VERTEX_SNAPSHOT_GPU",0,0,1);
    return vp_snapshot_configured;
}
/* The owner has already proven this capture entry's bytes immutable and equal
 * to the requested input. Its link is cleared whenever that entry is reused.
 * Do not inspect guest memory or repeat a byte comparison here. */
static unsigned vp_from_snapshot(unsigned slot,const xv_vertex_prepare_stream *s,
    const void *identity,vp_snapshot_link *link,unsigned *copy)
{
    *copy=0;
    if(s->bytes<256u || s->bytes>256u*1024u ||
       xv_vertex_refs_sparse(s->refs,s->bytes,s->stride))return 0;
#if XV_PACKED_VERTEX_LAYOUT
    if(s->packed)return 0;
#endif
    if(link->id && link->id<=VP_ENTRIES) {
        vp_entry *e=&vp_entries[link->id-1];
        if(e->pins && e->generation==link->generation &&
           e->identity==identity && e->bytes==s->bytes && e->stride==s->stride) {
            e->pins|=1u<<slot;vp_hits++;vp_snapshot_hits++;vp_saved+=s->bytes;
            return link->id;
        }
    }
    xv_vertex_prepare_stream key=*s;key.source=identity;
    unsigned id=vp_create(slot,&key,s->source,copy);
    *link=(vp_snapshot_link){id,id?vp_entries[id-1].generation:0};
    return id;
}
#endif
static void vp_upload(unsigned id)
{
    const vp_entry *e=&vp_entries[id-1];
    memcpy(vp_gpu+e->first*VP_PAGE,vp_cpu+e->first*VP_PAGE,e->bytes);
    vp_copied+=e->bytes;
    /* The enclosing FIFO drains uncached writes before completion publication. */
}
static const void *vp_result(unsigned id)
{ return vp_gpu+vp_entries[id-1].first*VP_PAGE; }
static void vp_retire_slot(unsigned slot)
{
    assert(slot<XV_FRAME_SLOTS);
    for(unsigned i=0;i<VP_ENTRIES;i++) {
        vp_entry *e=&vp_entries[i];
        if(!e->pins)continue;
        e->pins&=~(1u<<slot);
        if(e->pins)continue;
        unsigned *link=&vp_buckets[vp_hash(e->identity,e->bytes,e->stride)];
        while(*link && *link!=i+1u)link=&vp_entries[*link-1].next;
        assert(*link==i+1u);*link=e->next;
        for(unsigned j=0;j<e->pages;j++) { assert(vp_pages[e->first+j]==i+1u);vp_pages[e->first+j]=0; }
        memset(e,0,sizeof *e);
    }
}
static void vp_shutdown(void)
{
    /* Same GPU-drained shutdown precondition as the ordinary upload pools. */
    vp_release();memset(vp_entries,0,sizeof vp_entries);memset(vp_pages,0,sizeof vp_pages);
    memset(vp_buckets,0,sizeof vp_buckets);vp_configured=-1;vp_unavailable=0;
    vp_hits=vp_created=vp_full=0;vp_compared=vp_saved=vp_copied=0;
    vp_generation=0;
#if XV_VERTEX_CAPTURE_REUSE
    vp_snapshot_configured=-1;vp_snapshot_hits=0;
#endif
}
static void vp_report(unsigned frames)
{
#if XV_VERTEX_CAPTURE_REUSE
    xv_logf("[vertex-snapshot-gpu] %u frames enabled %d hits %u; validated snapshot links, no second compare\n",
        frames,vp_snapshot_configured==1,vp_snapshot_hits);
    vp_snapshot_hits=0;
#endif
    xv_logf("[vertex-persistent] %u frames enabled %d hits %u created %u full %u; compared %llu KiB avoided-capture %llu KiB uploaded %llu KiB; immutable versions pinned to GPU slots\n",
        frames,vp_configured==1,vp_hits,vp_created,vp_full,
        (unsigned long long)(vp_compared>>10),(unsigned long long)(vp_saved>>10),(unsigned long long)(vp_copied>>10));
    vp_hits=vp_created=vp_full=0;vp_compared=vp_saved=vp_copied=0;
}
#endif
