#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include "../../runtime/xv_frame_events.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static unsigned bits, delays, waits, signals;
static int fail_create, fail_wait, timeout_wait;
SceUID sceKernelCreateEventFlag(const char *name,int attr,int initial,SceKernelEventFlagOptParam *opt)
{ assert(attr==SCE_EVENT_WAITMULTIPLE&&!initial&&!opt); (void)name;bits=0;return fail_create?-1:7; }
int sceKernelDeleteEventFlag(SceUID id) { assert(id==7);return 0; }
int sceKernelSetEventFlag(SceUID id,unsigned set)
{ assert(id==7);pthread_mutex_lock(&lock);signals++;bits|=set;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&lock);return 0; }
int sceKernelWaitEventFlag(SceUID id,unsigned wanted,unsigned mode,unsigned *out,SceUInt *timeout)
{
    assert(id==7&&*timeout==10000&&mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT));
    if(fail_wait)return -1;
    if(timeout_wait){*timeout=0;return -1;}
    pthread_mutex_lock(&lock);waits++;
    while(!(bits&wanted))pthread_cond_wait(&cond,&lock);
    *out=bits;bits&=~wanted;pthread_mutex_unlock(&lock);return 0;
}
int sceKernelDelayThread(SceUInt us) { assert(us==100||us==200);delays++;return 0; }
static xv_frame_events events;
static uint32_t requested,completed,slots[2];
#define COUNT 100000u
#define START 0xFFFF8000u
static void *consumer(void *unused)
{
    (void)unused;
    for(unsigned i=1;i<=COUNT;i++) {
        uint32_t target=START+i;
        while((int32_t)(__atomic_load_n(&requested,__ATOMIC_ACQUIRE)-target)<0)
            xv_frame_events_wait(&events,XV_FRAME_REQUESTED,200);
        unsigned slot=target&1u;assert(slots[slot]==i*37u);
        if(i%17==0)sched_yield();
        assert(slots[slot]==i*37u);
        __atomic_store_n(&completed,target,__ATOMIC_RELEASE);
        xv_frame_events_signal(&events,XV_FRAME_COMPLETED);
    }
    return NULL;
}
int main(void)
{
    xv_frame_events_init(&events,0);assert(events.id<0);xv_frame_events_wait(&events,XV_FRAME_REQUESTED,200);assert(delays==1);xv_frame_events_signal(&events,3);xv_frame_events_close(&events);
    fail_create=1;xv_frame_events_init(&events,1);assert(events.id<0);xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);assert(delays==2);fail_create=0;
    xv_frame_events_init(&events,1);assert(events.id==7);
    /* Signals before waiting stay pending; consuming one bit preserves the other. */
    xv_frame_events_signal(&events,3);xv_frame_events_wait(&events,XV_FRAME_REQUESTED,200);assert(bits==XV_FRAME_COMPLETED);xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);assert(!bits);
    fail_wait=1;xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);assert(delays==3);fail_wait=0;
    timeout_wait=1;xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);assert(delays==3);timeout_wait=0;
    requested=completed=START;pthread_t thread;assert(!pthread_create(&thread,NULL,consumer,NULL));
    for(unsigned i=1;i<=COUNT;i++) {
        uint32_t target=START+i;
        /* Two slots: reuse only after the reader has completed the prior owner. */
        while((int32_t)(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)-(target-2u))<0)
            xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);
        slots[target&1u]=i*37u;
        __atomic_store_n(&requested,target,__ATOMIC_RELEASE);
        xv_frame_events_signal(&events,XV_FRAME_REQUESTED);
        if(i%23==0)sched_yield();
    }
    while((int32_t)(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)-(START+COUNT))<0)
        xv_frame_events_wait(&events,XV_FRAME_COMPLETED,100);
    assert(!pthread_join(thread,NULL));assert(delays==3&&waits>0&&signals>=2*COUNT);
    xv_frame_events_close(&events);assert(events.id<0);
    puts("PASS: 100000 threaded frame notifications, wraparound, two-slot ownership, early/stale wakes, independent bits, timeout and API failure fallback");
}
