/* Exact production scan, retirement, history guard and result publisher.
 * Only GPU readiness, clock, diagnostics and list storage are supplied here. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/gxm.h>
#include "runtime/xv_frame_slots.h"
#include "runtime/xv_frame_events.h"
#include "runtime/xv_packet_timing.h"
#include "runtime/xv_visibility.h"
#define XV_LOG(...) ((void)0)
#define XV_VP_COMPLETE(l,f) ((void)0)
#define XV_NUM_LISTS XV_FRAME_SLOTS
#define XV_VISIBILITY_GPU_CORES 4u
#define XV_VISIBILITY_WORDS (4u*XV_VISIBILITY_PER_FRAME)
typedef struct {
    unsigned nvisibility,visibility_gpu_ready;
    struct { uint32_t serial,result_slot,guest_area,render_area; } visibility[XV_VISIBILITY_PER_FRAME];
} cmdlist_t;
static cmdlist_t lists[3],*g_lists[3]={lists,lists+1,lists+2};
static uint32_t memory[3*XV_VISIBILITY_WORDS],*g_visibility_memory=memory;
static xv_visibility_result g_visibility_results[XV_VISIBILITY_IDS];
static volatile uint32_t g_frame_completed;
static uint32_t g_frame_submitted;
static struct { int hle_ready; } g_gfx;
static xv_frame_events g_frame_events={-1};
static uint64_t now;
static unsigned notifications,checked,publications[3],publication_order[3],order_count;
static uint32_t base,words[8],frame_ticket[3];
static xv_slot_owner owners[3];
static unsigned tests;
uint64_t sceKernelGetProcessTimeWide(void) { return ++now; }
uint64_t xk_os_monotonic_us(void) { return ++now; }
void xv_logf(const char *fmt,...) { (void)fmt; }
void xk_os_scheduler_notify(void) { notifications++; }
int sceKernelSetEventFlag(SceUID id,unsigned bits) { assert(0);return 0; }
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout)
{ assert(0);return 0; }
#include "packets.inc"
#include "complete.inc"
int xv_d3d_has_visibility(uint32_t frame) { return lists[frame%3u].nvisibility!=0; }
void xv_d3d_visibility_complete(uint32_t frame)
{
    unsigned slot=frame%3u;
    assert(++publications[slot]==1);
    assert(xv_slot_busy(&owners[slot],g_frame_completed));
    if(lists[slot].nvisibility)publication_order[order_count++]=frame;
    complete_actual(frame);
}
void xv_d3d_check_geometry(uint32_t frame)
{
    unsigned q=frame_ticket[frame%3u]&3u;
    assert(g_packets[q].failed || words[q]==frame_ticket[frame%3u]);
    assert(publications[frame%3u]==1);
    assert(xv_slot_busy(&owners[frame%3u],g_frame_completed));
    checked++;
}
void xv_d3d_query_boundary_report(void) {}
#include "retire.inc"
static void scan(void)
{
#if XV_QUERY_PREFIX_PUBLISH
    xv_pump_query_prefixes();
#endif
}
static void setup(uint32_t first,unsigned count)
{
    memset(lists,0,sizeof lists);memset(memory,0,sizeof memory);
    memset(g_visibility_results,0,sizeof g_visibility_results);
    memset(g_packets,0,sizeof g_packets);memset(publications,0,sizeof publications);
    memset(owners,0,sizeof owners);memset(words,0,sizeof words);
    memset(publication_order,0,sizeof publication_order);
    notifications=checked=order_count=0;now=100;base=first;g_gfx.hle_ready=1;
    g_frame_completed=base;g_frame_submitted=base+count;
    g_boundary_queries=g_boundary_fallbacks=g_boundary_before_final=0;
    g_boundary_query_us=g_boundary_tail_us=0;
    g_retired_count=g_max_pending=g_early_visibility_count=0;
    g_completion_us=g_early_visibility_us=g_visibility_tail_us=0;
#if XV_QUERY_PREFIX_PUBLISH
    g_query_prefix_published=g_query_prefix_pending_tail=g_query_prefix_history_blocked=0;
#endif
#if XV_GPU_PACKET_TIMING
    memset(&g_packet_timing,0,sizeof g_packet_timing);
#endif
    for(unsigned i=0;i<count;i++) {
        uint32_t ticket=base+i+1u;unsigned q=ticket&3u;
        frame_ticket[i]=ticket;owners[i]=(xv_slot_owner){ticket,1};
        g_packets[q].mesh=i;g_packets[q].ui=i;
        g_packets[q].fence=(SceGxmNotification){words+q,ticket};
        g_packets[q].visibility_fence=(SceGxmNotification){words+4+q,ticket};
        g_packets[q].query_boundary=1;
        g_packets[q].started_us=now;
        words[q]=words[4+q]=~ticket;
#if XV_GPU_PACKET_TIMING
        xv_packet_timing_begin(&g_packets[q].timing,ticket,now,0);
        xv_packet_timing_end(&g_packets[q].timing,now+1,0);
#endif
    }
}
static uint32_t add(unsigned frame,unsigned id,uint32_t value)
{
    cmdlist_t *l=&lists[frame];unsigned i=l->nvisibility++;
    uint16_t slot;uint32_t serial;
    assert(!xv_visibility_issue(g_visibility_results,id,&slot,&serial));
    l->visibility[i].result_slot=slot;l->visibility[i].serial=serial;
    l->visibility_gpu_ready=1;
    memory[frame*XV_VISIBILITY_WORDS+i]=value;
    xv_visibility_submit(g_visibility_results,slot,serial,(uint32_t)now);
    return serial;
}
static void prefix(unsigned i) { uint32_t t=frame_ticket[i];words[4+(t&3u)]=t; }
static void final(unsigned i) { uint32_t t=frame_ticket[i];words[t&3u]=t; }
static void read_exact(unsigned id,uint32_t serial,uint32_t expected)
{
    uint32_t value=0xabcdef;
    assert(!xv_visibility_read_generation(g_visibility_results,id,serial,&value));
    assert(value==expected);
}
static void pending_exact(unsigned id,uint32_t serial)
{
    uint32_t value=0xabcdef;
    assert(xv_visibility_read_generation(g_visibility_results,id,serial,&value)==XV_VISIBILITY_INCOMPLETE);
    assert(value==0xabcdef);
}
static void finish(unsigned count)
{
    for(unsigned i=0;i<count;i++)final(i);
    while(xv_pump_retire()) {}
    assert(g_frame_completed==base+count && checked==count);
    for(unsigned i=0;i<count;i++)assert(publications[i]==1 && !xv_slot_busy(&owners[i],g_frame_completed));
    unsigned before=order_count;scan();assert(order_count==before);
    tests++;
}
static void ordinary(uint32_t first)
{
    setup(first,3);
    for(unsigned i=0;i<3;i++) {assert(add(i,5,10+i)==i+1);prefix(i);}
    assert(!xv_pump_retire());assert(publications[0]==1 && !publications[1]);
    scan();
    assert(publications[1]==XV_QUERY_PREFIX_PUBLISH && publications[2]==XV_QUERY_PREFIX_PUBLISH);
    assert(g_frame_completed==base);
    for(unsigned i=0;i<3;i++)assert(xv_slot_busy(&owners[i],g_frame_completed));
    read_exact(5,1,10);
#if XV_QUERY_PREFIX_PUBLISH
    read_exact(5,2,11);read_exact(5,3,12);
    assert(order_count==3 && publication_order[0]==0 && publication_order[1]==1 && publication_order[2]==2);
    assert(g_query_prefix_published==2 && g_query_prefix_pending_tail==2);
#else
    pending_exact(5,2);
#endif
    scan();assert(publications[1]==XV_QUERY_PREFIX_PUBLISH);
    finish(3);
    for(unsigned i=0;i<3;i++)read_exact(5,i+1,10+i);
}
static void ordered_readiness(void)
{
    setup(0,3);for(unsigned i=0;i<3;i++)add(i,77,20+i);
    prefix(2);scan();assert(!order_count);
    prefix(0);assert(!xv_pump_retire());scan();assert(order_count==1);
    prefix(1);scan();assert(order_count==(XV_QUERY_PREFIX_PUBLISH?3u:1u));
    finish(3);for(unsigned i=0;i<3;i++)read_exact(77,i+1,20+i);
}
static void history_collision(int wrapped)
{
    setup(wrapped?UINT32_MAX-1u:0,3);
    if(wrapped) {
        uint16_t slot;uint32_t serial;
        assert(!xv_visibility_issue(g_visibility_results,5,&slot,&serial));
        g_visibility_results[slot].issued=UINT32_MAX-1u;
    }
    uint32_t old=add(0,5,30);prefix(0);
    uint32_t younger=add(1,5,31);prefix(1);
    if(wrapped)assert(old==UINT32_MAX && younger==1);
    while(add(2,5,32)%4u!=old%4u) {}
    prefix(2);assert(!xv_pump_retire());scan();
    assert(publications[1]==XV_QUERY_PREFIX_PUBLISH && !publications[2]);
    read_exact(5,old,30);
#if XV_QUERY_PREFIX_PUBLISH
    assert(g_query_prefix_history_blocked==1);scan();assert(g_query_prefix_history_blocked==1);
    uint32_t done=g_frame_completed;final(0);assert(xv_pump_retire());
    assert(g_frame_completed==done+1u);scan();
    /* Reconsider after A retires. B is still retained and must remain readable. */
    read_exact(5,younger,31);
#endif
    finish(3);
}
static void fallback_and_error(unsigned which)
{
    setup(0,3);for(unsigned i=0;i<3;i++){add(i,5,40+i);prefix(i);}
    unsigned q=frame_ticket[1]&3u;
    if(which==0)g_packets[q].visibility_fence.address=NULL;
    if(which==1)g_packets[q].query_boundary=0;
    if(which==2)g_packets[q].failed=1;
    assert(!xv_pump_retire());scan();assert(order_count==1);
    finish(3);
}
static void queryless(void)
{
    setup(0,3);
    g_packets[frame_ticket[0]&3u].query_boundary=0;
    g_packets[frame_ticket[0]&3u].visibility_fence.address=NULL;
    add(1,5,51);add(2,5,52);prefix(1);prefix(2);
    assert(!xv_pump_retire());scan();
    assert(!publications[0] && publications[1]==XV_QUERY_PREFIX_PUBLISH);
    finish(3);read_exact(5,1,51);read_exact(5,2,52);
}
static void rejected_owners(void)
{
    for(unsigned variant=0;variant<5;variant++) {
        setup(0,2);add(0,5,61);add(1,5,62);prefix(0);prefix(1);
        assert(!xv_pump_retire());
        unsigned q=frame_ticket[1]&3u;
        if(variant==0)g_packets[q].fence.value++;
        if(variant==1)g_packets[q].visibility_fence.value++;
        if(variant==2)lists[1].nvisibility=XV_VISIBILITY_PER_FRAME+1;
        if(variant==3)lists[1].visibility[0].result_slot=XV_VISIBILITY_IDS;
        if(variant==4)g_frame_submitted=base+XV_FRAME_TICKETS;
        scan();assert(publications[0]==1 && !publications[1]);tests++;
    }
}
static void maximum_history_slots(void)
{
    setup(0,2);
    for(unsigned i=0;i<XV_VISIBILITY_IDS;i++)assert(add(0,i,i+70)==1);
    for(unsigned i=0;i<XV_VISIBILITY_IDS;i++)assert(add(1,i,i+90)==2);
    prefix(0);prefix(1);assert(!xv_pump_retire());scan();
    for(unsigned i=0;i<XV_VISIBILITY_IDS;i++) {
        read_exact(i,1,i+70);
#if XV_QUERY_PREFIX_PUBLISH
        read_exact(i,2,i+90);
#endif
    }
    finish(2);
}
static void reset_submission(void)
{
    setup(0,2);unsigned q=frame_ticket[1]&3u;
    g_packets[q].visibility_completed=1;
#if XV_QUERY_PREFIX_PUBLISH
    g_packets[q].prefix_history_blocked=1;
    g_packets[q].prefix_history_done=g_frame_completed;
#endif
    /* Unedited production submission reset; prior blocked-ticket state must
     * not suppress a fresh ticket even when its completion counter matches. */
#include "initialize_prefix.inc"
    assert(!g_packets[q].visibility_completed && !g_packets[q].visibility_fence.address);
#if XV_QUERY_PREFIX_PUBLISH
    assert(!g_packets[q].prefix_history_blocked);
#endif
    tests++;
}
int main(void)
{
    ordinary(0);ordinary(UINT32_MAX-1u);
    ordered_readiness();history_collision(0);history_collision(1);
    for(unsigned i=0;i<3;i++)fallback_and_error(i);
    queryless();rejected_owners();
    /* Reuse all packet slots after a prior test; stale ready words cannot confer
     * a new ticket's ownership. The ordinary case resets initialized submission. */
    ordinary(4);
    maximum_history_slots();reset_submission();
    printf("PASS: %u production publication/retirement scenarios (enabled=%d timing=%d), exact delayed generations and unchanged final ownership\n",
        tests,XV_QUERY_PREFIX_PUBLISH,XV_GPU_PACKET_TIMING);
    return 0;
}
