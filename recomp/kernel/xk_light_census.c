/* Count original light-group execution; never gather or publish guest state.
 * All mutable group state is native-owner-only. Rejected native callers touch
 * only 32-bit atomic decline counters, never guest fields or xk_cur. */
#ifdef XV_LIGHT_QUERY_CENSUS
#include "xk.h"
#include "xk_light_census.h"
#include <limits.h>
unsigned xv_light_census_enabled;
static xv_light_census_stats totals;
static uint32_t declines[XV_LC_REASONS],admissions[3][XV_LC_GUARD+1];
static unsigned serial;
static struct {
    unsigned token, bad, query_count, traversals, removals, next_light;
    xctx *context;
    uint32_t sp, return_pc, id, object, object_record, tag, tags, lights;
    uint32_t count, ids[8], records[8], epoch, future, bsp, collision, clusters;
    uint32_t arena_bytes, image_lo, image_hi;
    uint8_t status[8];
    uint64_t start_reads, start_bytes;
} group;
static void decline(unsigned reason)
{if(reason&&reason<XV_LC_REASONS)__atomic_fetch_add(&declines[reason],1,__ATOMIC_RELAXED);}
static int admit(xctx *c,unsigned site)
{
    unsigned r=xv_object_census_admit(c);
    if(site<3&&r<=XV_LC_GUARD)__atomic_fetch_add(&admissions[site][r],1,__ATOMIC_RELAXED);
    if(r)decline(r);
    return !r;
}
static void bad(unsigned reason)
{if(!group.bad){group.bad=reason;decline(reason);}}
static unsigned bucket(unsigned n){return n<9?n:9;}
/* Reject trash/out-of-arena pages and wrapping spans before the first read.
 * These are bounds, not an immutable lifetime or alias proof. */
static int read_guest(uint32_t a,void *out,unsigned n)
{
    if(!n||a>UINT32_MAX-(n-1)||!g_xram||!g_xpt)return 0;
    for(unsigned i=0;i<n;){unsigned k=4096-((a+i)&4095);if(k>n-i)k=n-i;
        uint32_t off=g_xpt[(a+i)>>12];
        if((off&4095)||group.arena_bytes<4096||off>=group.arena_bytes-4096||
           off>group.arena_bytes-k-((a+i)&4095))return 0;
        i+=k;
    }
    for(unsigned i=0;i<n;){unsigned k=4096-((a+i)&4095);if(k>n-i)k=n-i;
        memcpy((uint8_t*)out+i,X_G(a+i),k);i+=k;}
    totals.guest_reads++;totals.guest_bytes+=n;return 1;
}
static uint32_t read_value(uint64_t address,unsigned size)
{
    uint32_t v=0;
    if(address>UINT32_MAX||!read_guest((uint32_t)address,&v,size))bad(XV_LC_MEMORY);
    return v;
}
#define U32(a) read_value((uint64_t)(a),4)
#define U16(a) read_value((uint64_t)(a),2)
#define U8(a) read_value((uint64_t)(a),1)
static uint32_t image32(uint32_t a)
{
    if(!g_img_base||a<group.image_lo||a>group.image_hi||group.image_hi-a<4){bad(XV_LC_MEMORY);return 0;}
    uint32_t v;memcpy(&v,g_img_base+a,4);totals.guest_reads++;totals.guest_bytes+=4;return v;
}
static int identity(uint32_t table,uint32_t id,unsigned stride,uint32_t *record)
{
    unsigned limit=U16((uint64_t)table+0x20),actual=U16((uint64_t)table+0x22);
    uint32_t records=U32((uint64_t)table+0x34);
    if(group.bad)return 0;
    if(!(id>>16)||(id&65535)>=limit||(id&65535)>=32768||actual!=stride)return 0;
    uint64_t a=(uint64_t)records+(id&65535)*stride;
    if(a>UINT32_MAX-(stride-1))return 0;
    *record=(uint32_t)a;return U16(a)==(id>>16)&&!group.bad;
}
static unsigned entry_done(void)
{
    uint64_t bytes=totals.guest_bytes-group.start_bytes;
    totals.entry_bytes+=bytes;totals.entry_reads+=totals.guest_reads-group.start_reads;
    if(bytes>totals.max_entry_bytes)totals.max_entry_bytes=bytes;
    return group.token;
}
static void group_done(void)
{
    uint64_t bytes=totals.guest_bytes-group.start_bytes;
    if(bytes>totals.max_group_bytes)totals.max_group_bytes=bytes;
}
static void close_cancel(unsigned reason)
{
    if(!group.token)return;
    bad(reason);totals.cancelled++;group_done();memset(&group,0,sizeof group);
}
int xv_light_census_control(xctx *c,int enabled,int reset)
{
    if(xv_object_census_boundary(c)||group.token)return 0;
    if(reset){memset(&totals,0,sizeof totals);
        for(unsigned i=0;i<XV_LC_REASONS;i++)__atomic_store_n(&declines[i],0,__ATOMIC_RELAXED);
        for(unsigned site=0;site<3;site++)for(unsigned i=0;i<=XV_LC_GUARD;i++)__atomic_store_n(&admissions[site][i],0,__ATOMIC_RELAXED);
    }
    __atomic_store_n(&xv_light_census_enabled,enabled>0,__ATOMIC_RELEASE);return 1;
}
int xv_light_census_take(xctx *c,xv_light_census_stats *out,int reset)
{
    if(!out||xv_object_census_boundary(c)||group.token)return 0;
    *out=totals;out->group_storage_bytes=sizeof group;out->stats_storage_bytes=sizeof totals+sizeof declines+sizeof admissions;
    for(unsigned i=0;i<XV_LC_REASONS;i++)out->declined[i]=reset?
        __atomic_exchange_n(&declines[i],0,__ATOMIC_ACQ_REL):__atomic_load_n(&declines[i],__ATOMIC_RELAXED);
    for(unsigned site=0;site<3;site++)for(unsigned i=0;i<=XV_LC_GUARD;i++)out->admission[site][i]=reset?
        __atomic_exchange_n(&admissions[site][i],0,__ATOMIC_ACQ_REL):__atomic_load_n(&admissions[site][i],__ATOMIC_RELAXED);
    if(reset)memset(&totals,0,sizeof totals);
    return 1;
}
unsigned xv_light_census_begin(xctx *c)
{
    if(!XV_LIGHT_CENSUS_ON()||!admit(c,0))return 0;
    totals.entries++;
    if(group.token){close_cancel(XV_LC_NESTED);return 0;}
    memset(&group,0,sizeof group);if(!++serial)++serial;
    group.token=serial;group.context=c;group.sp=c->r[4];group.id=c->r[0];
    group.start_reads=totals.guest_reads;group.start_bytes=totals.guest_bytes;
    group.arena_bytes=xk_mem_arena_size();group.image_lo=xk_mem_image_lo();group.image_hi=xk_mem_image_hi();
    totals.opened++;totals.entry_budget_nonpositive+=c->preempt<=0;
    group.return_pc=U32(group.sp);
    unsigned mode=(!!U8((uint64_t)group.sp+4))|((!!U8((uint64_t)group.sp+8))<<1);totals.mode[mode]++;
    unsigned site;for(site=0;site<XV_LC_SOURCES;site++)if(!totals.sources[site].groups||totals.sources[site].pc==group.return_pc)break;
    if(site<XV_LC_SOURCES)totals.sources[site].pc=group.return_pc;
    totals.sources[site].groups++;
    if(group.bad)return entry_done();
    if(image32(0x270824))bad(XV_LC_LOCALE);
    uint32_t table=image32(0x2fc6ac);
    if(!identity(table,group.id,12,&group.object_record)){bad(XV_LC_OBJECT);return entry_done();}
    group.object=U32((uint64_t)group.object_record+8);
    if(!(U32((uint64_t)group.object+4)&0x100)){totals.tag_count[0]++;return entry_done();}
    group.tags=image32(0x39ce24);
    group.tag=U32((uint64_t)group.tags+(U32(group.object)&65535)*32+0x14);
    group.count=U32((uint64_t)group.tag+0x140);totals.tag_count[bucket(group.count)]++;
    if(group.count>8){bad(XV_LC_COUNT);return entry_done();}
    group.lights=image32(0x2fc67c);
    for(unsigned i=0;i<group.count;i++){
        group.status[i]=U8((uint64_t)group.object+0xf4+i);
        group.ids[i]=U32((uint64_t)group.object+0xfc+4*i);
        if(group.status[i]||group.ids[i]==UINT32_MAX)continue;
        for(unsigned j=0;j<i;j++)if(!group.status[j]&&group.ids[j]==group.ids[i])bad(XV_LC_DUPLICATE);
        if(!identity(group.lights,group.ids[i],124,&group.records[i]))bad(XV_LC_LIGHT);
    }
    group.bsp=image32(0x39be58);group.collision=image32(0x39be50);
    group.clusters=U32((uint64_t)group.bsp+0x134);
    if(!group.clusters||group.clusters>256){bad(XV_LC_GEOMETRY);return entry_done();}
    group.epoch=image32(0x2d2fac);group.future=9;
    /* Keep only the nearest future stamp, no per-query/page/visited copy. */
    for(unsigned i=0;i<group.clusters;i++){
        uint32_t stamp=U32(0x2d2fb0u+4*i);
        if(stamp>group.epoch&&stamp-group.epoch<group.future)group.future=stamp-group.epoch;
    }
    return entry_done();
}
static int current(xctx *c,unsigned site)
{
    if(!XV_LIGHT_CENSUS_ON()||!admit(c,site))return 0;
    if(!group.token)return 0;
    if(group.context!=c){close_cancel(XV_LC_CONTEXT);return 0;}
    return 1;
}
void xv_light_census_query(xctx *c)
{
    if(!current(c,1)){
        if(XV_LIGHT_CENSUS_ON()&&xv_object_census_admit(c)==XV_LC_OK)totals.orphan_queries++;
        return;
    }
    group.query_count++;totals.queries++;
    uint32_t sp=c->r[4],id=U32((uint64_t)sp+4),head=U32((uint64_t)sp+8);
    uint32_t bits=U32((uint64_t)sp+16),cluster=U16((uint64_t)c->r[0]+4);
    unsigned positive=!(bits&0x80000000u)&&(bits&0x7fffffffu)&&((bits&0x7fffffffu)<=0x7f800000u);
    unsigned traverses=positive&&cluster!=65535;
    if(U32(sp)!=0x925b0||c->r[7]!=0x2fc670)bad(XV_LC_ORDER);
    unsigned i=group.next_light;
    while(i<group.count&&i<8&&(group.status[i]||group.ids[i]!=id))i++;
    if(i>=group.count||i>=8||group.ids[i]!=id||head!=group.records[i]+0x10)bad(XV_LC_ORDER);
    else {if(U16(group.records[i])!=(id>>16))bad(XV_LC_LIGHT);group.next_light=i+1;}
    if(image32(0x2d2fac)!=group.epoch+group.traversals)bad(XV_LC_EPOCH);
    if(traverses&&cluster>=group.clusters)bad(XV_LC_GEOMETRY);
    group.traversals+=traverses;totals.traversals+=traverses;
    if(group.query_count>8)bad(XV_LC_COUNT);
}
void xv_light_census_remove(xctx *c)
{
    if(!current(c,2)){
        if(XV_LIGHT_CENSUS_ON()&&xv_object_census_admit(c)==XV_LC_OK)totals.orphan_removals++;
        return;
    }
    group.removals++;totals.removals++;
    if(U32(c->r[4])!=0x8d7ff)bad(XV_LC_ORDER);
}
void xv_light_census_end(xctx *c,unsigned token)
{
    if(!current(c,3)||group.token!=token)return;
    if(c->r[4]!=group.sp||U32(c->r[4])!=group.return_pc)bad(XV_LC_EXIT);
    if(group.count<=8&&group.tag){
        if(U16(group.object_record)!=(group.id>>16)||U32((uint64_t)group.object_record+8)!=group.object||
           U32((uint64_t)group.tag+0x140)!=group.count||image32(0x39ce24)!=group.tags||
           image32(0x2fc67c)!=group.lights)bad(XV_LC_LIGHT);
        for(unsigned i=0;i<group.count;i++)if(U8((uint64_t)group.object+0xf4+i)!=group.status[i]||
            U32((uint64_t)group.object+0xfc+4*i)!=group.ids[i])bad(XV_LC_ORDER);
        for(unsigned i=0;i<group.count;i++)if(group.records[i]&&U16(group.records[i])!=(group.ids[i]>>16))bad(XV_LC_LIGHT);
        if(image32(0x39be58)!=group.bsp||image32(0x39be50)!=group.collision)bad(XV_LC_GEOMETRY);
        if(image32(0x2d2fac)!=group.epoch+group.traversals)bad(XV_LC_EPOCH);
        if((uint64_t)group.epoch+group.traversals>UINT32_MAX)bad(XV_LC_WRAP);
        if(group.future<=group.traversals)bad(XV_LC_FUTURE_STAMP);
    }
    totals.completed++;totals.query_count[bucket(group.query_count)]++;
    totals.traversal_count[bucket(group.traversals)]++;
    if(group.traversals>=2){totals.multi_groups++;totals.multi_traversals+=group.traversals;}
    if(!group.bad){totals.candidates++;totals.candidate_traversal_count[bucket(group.traversals)]++;
        if(group.traversals>=2){totals.candidate_multi_groups++;totals.candidate_multi_traversals+=group.traversals;}}
    group_done();memset(&group,0,sizeof group);
}
void xv_light_census_suffix(xctx *c)
{if(XV_LIGHT_CENSUS_ON()&&admit(c,0)){decline(XV_LC_SUFFIX);if(group.token)close_cancel(XV_LC_NESTED);}}
void xv_light_census_cancel(xctx *c,unsigned reason)
{
    if(!XV_LIGHT_CENSUS_ON()||!xv_object_census_is_owner())return;
    if(group.token&&(!c||group.context==c))close_cancel(reason);
}
void xv_light_census_cleanup(unsigned *token)
{
    if(!*token||!XV_LIGHT_CENSUS_ON()||!xv_object_census_is_owner())return;
    if(group.token==*token)close_cancel(XV_LC_EXIT);
    *token=0;
}
const char *xv_light_census_reason_name(unsigned r)
{
    static const char *const names[]={"ok","uninitialized","worker","native-owner","context","marked",
        "queue","guard","nested","suffix","memory","object","count","light","duplicate","locale",
        "geometry","order","epoch","wrap","future-stamp","exit","handoff","stop","bsp-switch","map-end","list-reset","light-delete"};
    return r<XV_LC_REASONS?names[r]:"unknown";
}
#endif
