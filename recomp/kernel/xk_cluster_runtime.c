/* Experimental guarded typed query. No worker unlock, asynchronous publication
 * or persistent map cache. The ordinary build does not compile this unit. */
#if defined(XV_TYPED_CLUSTER_QUERY) && defined(XV_WORKER_QUERY)
#include "xk.h"
#include "xk_worker_query.h"
#include "xk_cluster_runtime.h"
#include "xk_cluster_snapshot.h"
#include <fenv.h>
#include <limits.h>

enum { T_LANES=2,T_MAPS=12 };
enum { T_LAYOUT,T_INPUT,T_NUMERIC,T_CHANGED,T_REASONS };
typedef struct {uint32_t address,bytes;unsigned image;unsigned char *pointer;} Span;
typedef struct __attribute__((aligned(64))) {
    XvClusterReplay replay;
    XvClusterResult result;
    xctx entry;
    uint32_t args[5],visited[256];
    unsigned char start[6],marker;
    float center[3];
    Span maps[T_MAPS];unsigned maps_count;
    fenv_t entry_fp,result_fp;
    uint64_t attempts,applied,declines[T_REASONS],dirty_bytes,backedges;
} Lane;
static Lane lanes[T_LANES];
static struct {
    XvClusterSnapshot *snapshot;
    unsigned valid,invalidations;
    uint32_t bsp,projection,arena,image_lo,image_hi;
    unsigned char *ram,*image;uint32_t *pages;
    Span visited[2],epoch,marker;unsigned visited_spans;
    uint64_t builds,failed,build_us,owned_bytes;
    unsigned last_invalidation;
} batch;

static unsigned char *pointer(uint32_t address,unsigned bytes,unsigned image)
{
    if(!bytes||address>UINT32_MAX-(bytes-1))return NULL;
    if(image){
        if(!batch.image||address<batch.image_lo||address>=batch.image_hi||bytes>batch.image_hi-address)return NULL;
        return batch.image+address;
    }
    if(!batch.ram||!batch.pages||bytes>4096-(address&4095))return NULL;
    uint32_t offset=batch.pages[address>>12];
    if((offset&4095)||batch.arena<4096||offset>=batch.arena-4096||bytes>batch.arena-offset-(address&4095))return NULL;
    return batch.ram+offset+(address&4095);
}
static int overlaps(const void *a,unsigned an,const void *b,unsigned bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+bn&&y<x+an;}
static int source_read(void *unused,uint32_t address,void *out,size_t bytes)
{
    (void)unused;
    if(bytes>UINT_MAX||address>UINT32_MAX-bytes)return 0;
    for(unsigned done=0;done<bytes;){
        uint32_t a=address+done;unsigned n=4096-(a&4095);if(n>bytes-done)n=(unsigned)bytes-done;
        unsigned char *p=pointer(a,n,0);
        if(!p||!xv_object_query_source_allowed((uintptr_t)p,n)||
           overlaps(p,n,batch.epoch.pointer,4)||overlaps(p,n,batch.marker.pointer,1))return 0;
        for(unsigned i=0;i<batch.visited_spans;i++)
            if(overlaps(p,n,batch.visited[i].pointer,batch.visited[i].bytes))return 0;
        memcpy((unsigned char*)out+done,p,n);done+=n;
    }
    return 1;
}
static int image_word(uint32_t address,uint32_t *out)
{unsigned char *p=pointer(address,4,1);if(!p)return 0;memcpy(out,p,4);return 1;}
static int globals_current(void)
{
    uint32_t bsp,projection;
    return __atomic_load_n(&batch.valid,__ATOMIC_ACQUIRE)&&batch.snapshot&&
        g_xram==batch.ram&&g_img_base==batch.image&&g_xpt==batch.pages&&
        xk_mem_arena_size()==batch.arena&&xk_mem_image_lo()==batch.image_lo&&xk_mem_image_hi()==batch.image_hi&&
        image_word(0x39be58,&bsp)&&image_word(0x39be50,&projection)&&
        bsp==batch.bsp&&projection==batch.projection;
}
void xv_cluster_runtime_end(void)
{
    __atomic_store_n(&batch.valid,0,__ATOMIC_RELEASE);
    xv_cluster_snapshot_release(batch.snapshot);batch.snapshot=NULL;
}
void xv_cluster_runtime_invalidate(unsigned service)
{
    if(__atomic_exchange_n(&batch.valid,0,__ATOMIC_ACQ_REL)){
        __atomic_add_fetch(&batch.invalidations,1,__ATOMIC_RELAXED);
        __atomic_store_n(&batch.last_invalidation,service,__ATOMIC_RELAXED);
    }
}
void xv_cluster_runtime_begin(void)
{
    xv_cluster_runtime_end();batch.builds++;
    /* Geometry validation can inspect exceptional floats. Snapshot preparation
     * must not change the guest owner's native floating-point environment. */
    fenv_t owner_fp;fegetenv(&owner_fp);
    uint64_t begin=xk_os_monotonic_us();
    batch.ram=g_xram;batch.image=g_img_base;batch.pages=g_xpt;
    batch.arena=xk_mem_arena_size();batch.image_lo=xk_mem_image_lo();batch.image_hi=xk_mem_image_hi();
    batch.visited_spans=0;
    for(unsigned done=0;done<1024;){
        uint32_t address=0x2d2fb0+done;unsigned n=4096-(address&4095);if(n>1024-done)n=1024-done;
        unsigned char *p=pointer(address,n,0);if(!p)goto failed;
        batch.visited[batch.visited_spans++]=(Span){address,n,0,p};done+=n;
    }
    batch.epoch=(Span){0x2d2fac,4,1,pointer(0x2d2fac,4,1)};
    batch.marker=(Span){0x2d2fa9,1,1,pointer(0x2d2fa9,1,1)};
    if(!batch.epoch.pointer||!batch.marker.pointer||
       !image_word(0x39be58,&batch.bsp)||!image_word(0x39be50,&batch.projection))goto failed;
    XvClusterSource source={batch.bsp,batch.projection,0x1eaf30,0x1f0a68};
    batch.snapshot=xv_cluster_snapshot_build(source_read,NULL,&source,1u<<20);
    if(!batch.snapshot)goto failed;
    batch.owned_bytes+=xv_cluster_snapshot_bytes(batch.snapshot);
    batch.build_us+=xk_os_monotonic_us()-begin;
    fesetenv(&owner_fp);
    __atomic_store_n(&batch.valid,1,__ATOMIC_RELEASE);return;
failed:
    batch.failed++;batch.build_us+=xk_os_monotonic_us()-begin;
    fesetenv(&owner_fp);
}
static int add_span(Lane *v,uint32_t address,unsigned bytes,unsigned image)
{
    for(unsigned done=0;done<bytes;){
        uint32_t a=address+done;unsigned n=4096-(a&4095);if(n>bytes-done)n=bytes-done;
        unsigned char *p=pointer(a,n,image);if(!p||v->maps_count==T_MAPS)return 0;
        for(unsigned i=0;i<v->maps_count;i++)if(overlaps(p,n,v->maps[i].pointer,v->maps[i].bytes))return 0;
        v->maps[v->maps_count++]=(Span){a,n,image,p};done+=n;
    }
    return 1;
}
static int read_input(Lane *v,uint32_t address,void *out,unsigned bytes)
{
    if(address>UINT32_MAX-bytes)return 0;
    for(unsigned done=0;done<bytes;){
        uint32_t a=address+done;unsigned n=4096-(a&4095);if(n>bytes-done)n=bytes-done;
        unsigned char *p=pointer(a,n,0);if(!p)return 0;
        for(unsigned i=0;i<v->maps_count;i++)if(overlaps(p,n,v->maps[i].pointer,v->maps[i].bytes))return 0;
        memcpy((unsigned char*)out+done,p,n);done+=n;
    }
    return 1;
}
static void copy_visited(uint32_t out[256])
{unsigned done=0;for(unsigned i=0;i<batch.visited_spans;i++){
    memcpy((unsigned char*)out+done,batch.visited[i].pointer,batch.visited[i].bytes);done+=batch.visited[i].bytes;}}
static int visited_current(const Lane *v)
{
    unsigned done=0;
    for(unsigned i=0;i<batch.visited_spans;i++){
        const Span *s=&batch.visited[i];
        if(pointer(s->address,s->bytes,0)!=s->pointer||memcmp(s->pointer,(const unsigned char*)v->visited+done,s->bytes))return 0;
        done+=s->bytes;
    }
    return 1;
}
static unsigned char *captured_pointer(const Lane *v,uint32_t address,unsigned bytes,unsigned image)
{
    for(unsigned i=0;i<v->maps_count;i++){
        const Span *s=&v->maps[i];
        if(s->image==image&&address>=s->address&&address-s->address<=s->bytes&&bytes<=s->bytes-(address-s->address))
            return s->pointer+address-s->address;
    }
    return NULL;
}
static void publish(Lane *v,xctx *c,uint32_t entry_epoch)
{
    /* Every mapping has been checked. No callback, allocation, failure or lock
     * operation is permitted once the first live byte is written. */
    uint32_t base=v->entry.r[4]-XV_CLUSTER_SCRATCH;
    for(unsigned word=0;word<XV_CLUSTER_DIRTY_WORDS;word++){
        uint32_t bits=v->replay.dirty[word];
        while(bits){
            unsigned start=__builtin_ctz(bits),n=32-start;
            uint32_t rest=~(bits>>start);if(rest)n=__builtin_ctz(rest);
            unsigned offset=(word*32+start)*2,remaining=n*2;
            while(remaining){
                unsigned take=4096-((base+offset)&4095);if(take>remaining)take=remaining;
                memcpy(captured_pointer(v,base+offset,take,0),v->replay.scratch+offset,take);
                v->dirty_bytes+=take;offset+=take;remaining-=take;
            }
            bits&=~((n==32?UINT32_MAX:(1u<<n)-1)<<start);
        }
    }
    if(v->result.epoch!=entry_epoch){
        memcpy(batch.epoch.pointer,&v->result.epoch,4);*batch.marker.pointer=0;
        for(unsigned word=0;word<8;word++){
            uint32_t bits=v->result.changed[word];
            while(bits){unsigned bit=__builtin_ctz(bits);bits&=bits-1;
                memcpy(captured_pointer(v,0x2d2fb0+4*(word*32+bit),4,0),&v->result.epoch,4);}
        }
    }
    *c=v->replay.context;fesetenv(&v->result_fp);
}
int xv_worker_query(xctx *c,int guard)
{
    extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
    if((&xv_watch_n&&xv_watch_n)||(&xv_trace_funcs&&xv_trace_funcs))return 0;
    if(c->r[4]<XV_CLUSTER_SCRATCH||c->r[4]>UINT32_MAX-20||(c->r[4]&3)||c->fsp>7||!globals_current())return 0;
    int lane=xv_object_query_lane(c,guard,c->r[4]-XV_CLUSTER_SCRATCH,XV_CLUSTER_SCRATCH+20)-1;
    if(lane<0||lane>=T_LANES)return 0;
    Lane *v=&lanes[lane];v->attempts++;v->entry=*c;v->maps_count=0;
    unsigned reason=T_LAYOUT;fegetenv(&v->entry_fp);
    if(!add_span(v,c->r[4]-XV_CLUSTER_SCRATCH,XV_CLUSTER_SCRATCH+20,0)||
       !add_span(v,0x2d2fb0,1024,0)||!add_span(v,0x2d2fac,4,1)||!add_span(v,0x2d2fa9,1,1))goto decline;
    x_guest_read(v->args,c->r[4],20);
    if(v->args[0]!=0x925b0||c->r[0]<c->r[4]+20||v->args[3]<c->r[4]+20||
       !xv_object_query_private(c,guard,c->r[0],6)||!xv_object_query_private(c,guard,v->args[3],12))goto decline;
    reason=T_INPUT;
    if(!read_input(v,c->r[0],v->start,6)||!read_input(v,v->args[3],v->center,12))goto decline;
    for(unsigned i=0;i<batch.visited_spans;i++)
        if(pointer(batch.visited[i].address,batch.visited[i].bytes,0)!=batch.visited[i].pointer)goto decline;
    copy_visited(v->visited);
    XvClusterInput in={.budget=(unsigned)c->preempt,.visited=v->visited};
    memcpy(in.center,v->center,12);memcpy(&in.radius,&v->args[4],4);memcpy(&in.start,v->start+4,2);
    memcpy(&in.epoch,batch.epoch.pointer,4);
    v->marker=*batch.marker.pointer;
    XvClusterReplayLayout layout;
    if(!xv_cluster_snapshot_replay_layout(batch.snapshot,v->args[3],v->args[2],&layout))goto decline;
    reason=T_NUMERIC;
    if(!xv_cluster_query_replay(xv_cluster_snapshot_geometry(batch.snapshot),&in,&layout,c,&v->result,&v->replay))goto decline;
    fegetenv(&v->result_fp);fesetenv(&v->entry_fp);
#ifdef XV_WORKER_QUERY_TEST
    extern void xv_worker_query_test_ready(xctx *,unsigned);
    xv_worker_query_test_ready(c,(unsigned)lane);
#endif
    reason=T_CHANGED;
    if(!globals_current()||memcmp(c,&v->entry,sizeof *c)||
       xv_object_query_lane(c,guard,c->r[4]-XV_CLUSTER_SCRATCH,XV_CLUSTER_SCRATCH+20)!=lane+1)goto decline;
    for(unsigned i=0;i<v->maps_count;i++){
        Span *s=&v->maps[i];if(pointer(s->address,s->bytes,s->image)!=s->pointer)goto decline;
    }
    uint32_t epoch,args[5];unsigned char start[6];float center[3];
    memcpy(&epoch,batch.epoch.pointer,4);x_guest_read(args,c->r[4],20);
    if(epoch!=in.epoch||*batch.marker.pointer!=v->marker||!visited_current(v)||memcmp(args,v->args,20)||
       !xv_object_query_private(c,guard,c->r[0],6)||!xv_object_query_private(c,guard,v->args[3],12)||
       !read_input(v,c->r[0],start,6)||!read_input(v,v->args[3],center,12)||
       memcmp(start,v->start,6)||memcmp(center,v->center,12))goto decline;
    publish(v,c,in.epoch);v->applied++;v->backedges+=v->result.backedges;return 1;
decline:
    fesetenv(&v->entry_fp);v->declines[reason]++;return 0;
}
void xv_worker_query_report(void)
{
    for(unsigned i=0;i<T_LANES;i++){
        Lane *v=&lanes[i];
        XK_LOG("[typed-query] lane %u attempts %llu applied %llu dirty-bytes %llu backedges %llu declines layout/input/numeric/changed %llu/%llu/%llu/%llu\n",i,
            (unsigned long long)v->attempts,(unsigned long long)v->applied,(unsigned long long)v->dirty_bytes,(unsigned long long)v->backedges,
            (unsigned long long)v->declines[T_LAYOUT],(unsigned long long)v->declines[T_INPUT],
            (unsigned long long)v->declines[T_NUMERIC],(unsigned long long)v->declines[T_CHANGED]);
        v->attempts=v->applied=v->dirty_bytes=v->backedges=0;memset(v->declines,0,sizeof v->declines);
    }
    XK_LOG("[typed-query] snapshots %llu failed %llu build-us %llu owned-bytes-sum %llu invalidations %u last-service %08X; guard retained, batch lifetime only\n",
        (unsigned long long)batch.builds,(unsigned long long)batch.failed,(unsigned long long)batch.build_us,(unsigned long long)batch.owned_bytes,
        batch.invalidations,batch.last_invalidation);
    batch.builds=batch.failed=batch.build_us=batch.owned_bytes=0;batch.invalidations=batch.last_invalidation=0;
}
#endif
