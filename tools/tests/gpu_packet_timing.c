#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/gxm.h>
#include "runtime/xv_frame_slots.h"
#include "runtime/xv_packet_timing.h"
#include "runtime/xv_frame_events.h"

static unsigned log_count;
static char last_log[2048];
void xv_logf(const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt);vsnprintf(last_log,sizeof last_log,fmt,ap);va_end(ap);log_count++;
}
#define XV_LOG(...) xv_logf(__VA_ARGS__)
static volatile uint32_t g_frame_completed;
static uint32_t g_frame_submitted;
static struct { int hle_ready; } g_gfx;
static xv_frame_events g_frame_events={-1};
int sceKernelSetEventFlag(SceUID id,unsigned bits) {(void)id;(void)bits;assert(0);return 0;}
void xv_d3d_visibility_complete(uint32_t mesh) {(void)mesh;assert(0);}
void xv_d3d_check_geometry(uint32_t mesh) {(void)mesh;assert(0);}
static uint64_t clock_value;
static unsigned clock_calls;
static int concurrent_clock;
static pthread_barrier_t barrier;
static volatile unsigned notification_words[8];
static unsigned concurrent_slot;
static uint32_t concurrent_ticket;
uint64_t sceKernelGetProcessTimeWide(void)
{
    clock_calls++;
    uint64_t result=clock_value;clock_value+=10;
    if(concurrent_clock && clock_calls==2) {
        pthread_barrier_wait(&barrier);pthread_barrier_wait(&barrier);
    }
    return result;
}
#include "packet_declarations.inc"
#include "packet_retire.inc"

static xv_packet_timing sample(uint64_t start)
{
    xv_packet_timing p;
    xv_packet_timing_begin(&p,0,start,0);
    xv_packet_timing_end(&p,start+20,0);
    xv_packet_timing_observe(&p,start+30,start+40,0);
    xv_packet_timing_observe(&p,start+45,start+50,1);
    return p;
}
static void accounting(void)
{
    for(unsigned wrap=0;wrap<2;wrap++) {
        xv_packet_timing p=sample(wrap?UINT64_MAX-25:0);
        xv_packet_timing_totals a={0};
        xv_packet_timing_fold(&a,&p);
        assert(a.valid==1 && !a.invalid && !a.no_negative && a.first_ticket==0);
        assert(a.sum[XV_PACKET_SUBMIT]==20 && a.sum[XV_PACKET_LOWER]==30);
        assert(a.sum[XV_PACKET_UPPER]==50 && a.sum[XV_PACKET_BRACKET]==20);
        assert(a.sum[XV_PACKET_TAIL_LOWER]==10 && a.sum[XV_PACKET_TAIL_UPPER]==30);
        assert(a.sum[XV_PACKET_POLLS]==2 && a.max[XV_PACKET_GAP]==40);
        xv_packet_timing_fold(&a,&p);assert(a.valid==1 && a.invalid==1);
        xv_packet_timing_report(&a);
        assert(strstr(last_log,"retired 2 valid 1 failed 0 invalid 1 no-negative 0"));
        assert(strstr(last_log,"completion-lo/hi 30/50"));
        xv_packet_timing_totals zero={0};assert(!memcmp(&a,&zero,sizeof a));
    }
    xv_packet_timing p;
    xv_packet_timing_totals a={0};
    xv_packet_timing_begin(&p,UINT32_MAX,100,0);xv_packet_timing_end(&p,200,0);
    xv_packet_timing_observe(&p,210,220,1);
    xv_packet_timing_observe(&p,500,510,1); /* first observation is immutable */
    assert(p.ready_after==220 && p.polls==1);
    xv_packet_timing_fold(&a,&p);
    assert(a.valid==1 && a.no_negative==1 && a.sum[XV_PACKET_LOWER]==0);
    assert(a.sum[XV_PACKET_UPPER]==120 && a.sum[XV_PACKET_TAIL_LOWER]==0 && a.sum[XV_PACKET_TAIL_UPPER]==20);
    assert(a.max[XV_PACKET_GAP]==120 && a.first_ticket==UINT32_MAX);
    for(unsigned bad=0;bad<8;bad++) {
        memset(&a,0,sizeof a);xv_packet_timing_begin(&p,bad,100,bad==0);
        xv_packet_timing_end(&p,bad==1?99:120,bad==2);
        if(bad==3)xv_packet_timing_observe(&p,119,130,0);
        if(bad==4)xv_packet_timing_observe(&p,130,129,0);
        if(bad==5) {xv_packet_timing_observe(&p,140,150,0);xv_packet_timing_observe(&p,149,155,0);}
        if(bad==6)xv_packet_timing_observe(&p,UINT64_C(1)<<63,(UINT64_C(1)<<63)+200,0);
        if(bad!=7)xv_packet_timing_observe(&p,160,170,1); /* missing observation is invalid too */
        xv_packet_timing_fold(&a,&p);
        assert(!a.valid && (bad==2?a.failed==1:a.invalid==1));
    }
    p=sample(100);xv_packet_timing_observe(&p,180,190,0);assert(p.invalid);
    p=sample(100);memset(&a,0,sizeof a);a.sum[XV_PACKET_UPPER]=UINT64_MAX-1;
    xv_packet_timing_fold(&a,&p);assert(!a.valid && a.invalid==1 && a.sum[XV_PACKET_SUBMIT]==0);
    p=sample(100);xv_packet_timing_begin(&p,5,0,0);assert(!p.ready && !p.polls && !p.folded && !p.invalid);
}

static void reset(uint32_t base,unsigned count)
{
    memset(g_packets,0,sizeof g_packets);memset(&g_packet_timing,0,sizeof g_packet_timing);
    g_frame_completed=base;g_frame_submitted=base+count;
    g_retired_count=g_max_pending=0;g_completion_us=0;clock_value=30;clock_calls=0;
    for(unsigned i=1;i<=count;i++) {
        uint32_t ticket=base+i;unsigned q=ticket&(XV_FRAME_TICKETS-1u);
        g_packets[q].fence=(SceGxmNotification){notification_words+q,ticket};
        __atomic_store_n(notification_words+q,ticket-4u,__ATOMIC_RELEASE);
        g_packets[q].started_us=0;
        xv_packet_timing_begin(&g_packets[q].timing,ticket,0,0);
        xv_packet_timing_end(&g_packets[q].timing,20,0);
    }
}
static void younger_and_retirement(void)
{
    reset(UINT32_MAX-1u,3);
    unsigned head=UINT32_MAX&3u;
    /* A later final notification is visible while the oldest stays pending.
     * The optional world notification is deliberately absent. */
    __atomic_store_n(notification_words,0,__ATOMIC_RELEASE);
    xv_pump_observe_younger();
    assert(g_packets[0].timing.ready && g_packets[0].timing.ready_after==40);
    assert(!xv_pump_retire() && g_frame_completed==UINT32_MAX-1u);
    clock_value=1000;
    __atomic_store_n(notification_words+head,UINT32_MAX,__ATOMIC_RELEASE);
    __atomic_store_n(notification_words+1,1,__ATOMIC_RELEASE);
    assert(xv_pump_retire() && g_frame_completed==UINT32_MAX);
    assert(xv_pump_retire() && g_frame_completed==0);
    assert(g_packets[0].timing.ready_after==40 && g_packets[0].timing.folded);
    assert(xv_pump_retire() && g_frame_completed==1 && !xv_pump_retire());
    assert(g_packet_timing.valid==3 && !g_packet_timing.invalid);
    assert(g_packet_timing.first_ticket==UINT32_MAX && g_packet_timing.last_ticket==1);
    /* Failure cleanup can retire, but must not contribute a GPU bound. */
    reset(0,1);g_packets[1].failed=1;g_packets[1].timing.failed=1;
    assert(xv_pump_retire());assert(g_packet_timing.failed==1 && !g_packet_timing.valid && clock_calls==1);
    reset(0,1);g_packets[1].timing.ticket=5; /* stale diagnostic generation */
    __atomic_store_n(notification_words+1,1,__ATOMIC_RELEASE);
    assert(xv_pump_retire() && g_packet_timing.invalid==1 && !g_packet_timing.valid);
    /* Aggregation/reporting shares the existing 60-retirement boundary. */
    reset(0,1);g_retired_count=59;
    __atomic_store_n(notification_words+1,1,__ATOMIC_RELEASE);
    unsigned logs=log_count;assert(xv_pump_retire());
    assert(log_count==logs+2 && strstr(last_log,"[gpu-packet]") && !g_packet_timing.retired);
}
static void *gpu_writer(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<500;i++) {
        pthread_barrier_wait(&barrier);
        __atomic_store_n(notification_words+concurrent_slot,concurrent_ticket,__ATOMIC_RELEASE);
        pthread_barrier_wait(&barrier);
    }
    return NULL;
}
static void concurrent_notification(void)
{
    assert(!pthread_barrier_init(&barrier,NULL,2));pthread_t gpu;
    assert(!pthread_create(&gpu,NULL,gpu_writer,NULL));
    for(unsigned i=0;i<500;i++) {
        concurrent_ticket=UINT32_MAX-250u+i;concurrent_slot=concurrent_ticket&3u;
        xv_packet_timing *p=&g_packets[concurrent_slot].timing;
        g_packets[concurrent_slot].fence=(SceGxmNotification){notification_words+concurrent_slot,concurrent_ticket};
        __atomic_store_n(notification_words+concurrent_slot,concurrent_ticket-4u,__ATOMIC_RELEASE);
        xv_packet_timing_begin(p,concurrent_ticket,0,0);xv_packet_timing_end(p,5,0);
        clock_calls=0;clock_value=10;concurrent_clock=1;
        assert(!xv_pump_observe(concurrent_slot)); /* write during trailing clock */
        assert(p->negative_before==10 && p->last_after==20 && !p->ready);
        assert(xv_pump_observe(concurrent_slot));assert(p->ready_after==40 && p->polls==2);
        concurrent_clock=0;
    }
    assert(!pthread_join(gpu,NULL));assert(!pthread_barrier_destroy(&barrier));
}
int main(void)
{
    accounting();younger_and_retirement();concurrent_notification();
    puts("PASS: packet bounds, wrap, failures, ordering, independent younger observations, reporting and 500 concurrent completion races");
}
