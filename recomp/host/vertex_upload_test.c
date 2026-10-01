#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef XV_VERTEX_UPLOAD_BYTES
#define XV_VERTEX_UPLOAD_BYTES 256u
#endif
#include "../../runtime/xv_vertex_upload.c"
static struct { void *p; int mapped; } blocks[16];
static unsigned next_id=1, live, fail_at, calls, flushes;
void xv_logf(const char *fmt, ...) {}
SceUID sceKernelAllocMemBlock(const char *name,SceKernelMemBlockType type,SceSize size,SceKernelAllocMemBlockOpt *opt)
{
    if(++calls==fail_at)return -1;
    assert(size==XV_VERTEX_UPLOAD_BYTES);
    assert(type==(!strcmp(name,"xv_vertices_gpu")?SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE:SCE_KERNEL_MEMBLOCK_TYPE_USER_RW));
    assert(next_id<16);unsigned id=next_id++;
    blocks[id].p=aligned_alloc(16,size);assert(blocks[id].p);
    /* Distinct initial contents catch comparisons against unwritten padding. */
    memset(blocks[id].p,type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE?0xa5:0x5a,size);
    live++;return id;
}
int sceKernelGetMemBlockBase(SceUID id,void **p) { *p=blocks[id].p;return 0; }
int sceKernelFreeMemBlock(SceUID id) { assert(!blocks[id].mapped && blocks[id].p);free(blocks[id].p);blocks[id].p=NULL;live--;return 0; }
int sceGxmMapMemory(void *p,SceSize n,SceGxmMemoryAttribFlags a)
{
    if(++calls==fail_at)return -1;
    assert(n==XV_VERTEX_UPLOAD_BYTES && a==SCE_GXM_MEMORY_ATTRIB_READ);
    for(unsigned i=1;i<next_id;i++)if(blocks[i].p==p){blocks[i].mapped=1;return 0;}
    assert(0);return -1;
}
int sceGxmUnmapMemory(void *p)
{ for(unsigned i=1;i<next_id;i++)if(blocks[i].p==p){assert(blocks[i].mapped);blocks[i].mapped=0;return 0;}assert(0);return -1; }
void xv_gpu_flush(const void *p,uint32_t n) { assert(p && n);flushes++; }
static void resident_regressions(void)
{
    unsigned char a[256], b[256];
    for(unsigned i=0;i<256;i++)a[i]=b[i]=(unsigned char)(i*13+7);
    next_id=1;
    xv_vertex_upload_override(1);
    unsigned before=copies;
    const unsigned char *p=xv_vertex_upload(0,a,129);
    assert(p && copies==before+1);
    xv_vertex_upload_reset(0);
    before=copies;unsigned hits=resident_hits;
    /* Equal destination bytes, even from a different source allocation. */
    assert(xv_vertex_upload(0,b,129)==p && copies==before && resident_hits==hits+1);
    b[0]^=1;
    /* A current-frame mutation must append and preserve the captured version. */
    const unsigned char *q=xv_vertex_upload(0,b,16);
    assert(q && q!=p && p[0]==a[0] && q[0]==b[0]);
    xv_vertex_upload_reset(0);xv_vertex_upload_override(0);before=copies;
    assert(xv_vertex_upload(0,a,129)==p && copies==before+1);
    xv_vertex_upload_reset(0);xv_vertex_upload_override(1);before=copies;
    assert(xv_vertex_upload(0,a,129)==p && copies==before);

    /* Changed stream lengths cross old padding and old upload boundaries. The
     * GPU must match current input, regardless of what a previous frame wrote. */
    const unsigned lengths[]={1,2,3,7,15,16,17,31,32,33,63,64,65,127,128,129,255,256};
    for(unsigned i=0;i<sizeof lengths/sizeof *lengths;i++)
    for(unsigned j=0;j<sizeof lengths/sizeof *lengths;j++) {
        xv_vertex_copy_override((i + j) & 1);
        xv_vertex_upload_reset(0);
        p=xv_vertex_upload(0,a,lengths[i]);assert(p && !memcmp(p,a,lengths[i]));
        xv_vertex_upload_reset(0);
        memset(b,0,sizeof b);memcpy(b,a,lengths[i]);
        p=xv_vertex_upload(0,b,lengths[j]);assert(p && !memcmp(p,b,lengths[j]));
        assert(!memcmp(pools[0].cpu,pools[0].gpu,pools[0].valid_bytes));
    }
    xv_vertex_upload_shutdown();assert(!live);next_id=1;

    struct capture {const unsigned char *gpu;unsigned size;unsigned char bytes[32];} recorded[3][8]={0};
    unsigned counts[3]={0};unsigned char sources[4][32]={{0}};uint32_t rng=0x19072026;
    for(unsigned frame=0;frame<2000;frame++) {
        unsigned slot=frame%3;counts[slot]=0;xv_vertex_upload_reset(slot);
        xv_vertex_upload_override(frame%7?1:0);
        xv_vertex_compare_override(frame%5?1:0);
        xv_vertex_copy_override(frame%3?1:0);
        for(unsigned draw=0;draw<8;draw++) {
            rng=rng*1664525u+1013904223u;
            unsigned index=(rng>>5)%4,size=(rng>>16)%32+1;
            sources[index][(rng>>8)%size]^=(unsigned char)(rng>>24);
            struct capture *v=&recorded[slot][counts[slot]++];
            v->size=size;memcpy(v->bytes,sources[index],size);
            v->gpu=xv_vertex_upload(slot,sources[index],size);assert(v->gpu);
            /* All captured draws remain byte-identical until their own slot
             * retires, including draws in the other two in-flight slots. */
            for(unsigned s=0;s<3;s++)for(unsigned k=0;k<counts[s];k++)
                assert(!memcmp(recorded[s][k].gpu,recorded[s][k].bytes,recorded[s][k].size));
            assert(!memcmp(pools[slot].cpu,pools[slot].gpu,pools[slot].valid_bytes));
        }
    }
    assert(fused_copies && fused_bytes);
    xv_vertex_upload_shutdown();assert(!live);xv_vertex_upload_override(-1);xv_vertex_compare_override(-1);xv_vertex_copy_override(-1);
}
int main(void)
{
    assert(!resident_enabled()); /* Negative hardware result: opt in explicitly. */
    assert(!copy_enabled()); /* Fused copies need a separate hardware comparison. */
    unsigned char source[257];for(unsigned i=0;i<sizeof source;i++)source[i]=(unsigned char)i;
    assert(!xv_vertex_upload(3,source,1));assert(!xv_vertex_upload(0,source,257));
    for(unsigned fail=1;fail<=3;fail++) {
        calls=0;fail_at=fail;assert(!xv_vertex_upload(0,source,128));assert(!live);
    }
    fail_at=0;next_id=1;
    const unsigned char *old[3];
    for(unsigned s=0;s<3;s++) { old[s]=xv_vertex_upload(s,source+1,128);assert(old[s] && !((uintptr_t)old[s]&15));assert(!memcmp(old[s],source+1,128)); }
    unsigned before=flushes;
    assert(xv_vertex_upload(0,source+1,64)==old[0] && flushes==before);
    source[101]^=0xff;
    assert(xv_vertex_upload(0,source+1,64)==old[0]); /* mutation outside consumed prefix */
    const unsigned char *changed=xv_vertex_upload(0,source+1,128);
    assert(changed && changed!=old[0]);assert(changed[100]==source[101]);
    for(unsigned s=0;s<3;s++)assert(old[s][100]!=(unsigned char)source[101]);
    assert(!xv_vertex_upload(0,source,16)); /* bounded pool, never overwrite live data */
    xv_vertex_upload_reset(0);assert(xv_vertex_upload(0,source,16)==old[0]);
    assert(old[1][100]==101 && old[2][100]==101); /* other slots remain owned */
    xv_vertex_upload_shutdown();assert(!live);
    resident_regressions();
    xv_slot_owner owner={UINT32_MAX,1};
    assert(xv_slot_busy(&owner,UINT32_MAX-1));assert(!xv_slot_busy(&owner,0));
    owner=(xv_slot_owner){0,1};assert(xv_slot_busy(&owner,UINT32_MAX));assert(!xv_slot_busy(&owner,0));
    puts("PASS: immutable snapshots, retired-byte reuse and override, padding/length changes, 2000 mixed slot generations, bounds, allocation/map failures and ticket wrap");
    return 0;
}
