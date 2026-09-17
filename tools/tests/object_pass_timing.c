/* Production worker pool and passive observer; real pthread queue/join/STOP. */
#define _POSIX_C_SOURCE 200809L
#include "kernel/xk.h"
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;int xv_phase_enabled;
static unsigned allocation_fail,allocations,frees,ready=1,writes[300],budget_stop,scope=1,generation=1,live=1;
static xctx *live_context;static pthread_t live_thread;
static uint64_t clock_calls;
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);__atomic_add_fetch(&clock_calls,1,__ATOMIC_RELAXED);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t size,uint32_t a,uint32_t l,uint32_t h,int t)
{(void)a;(void)l;(void)h;(void)t;assert(size==XV_OBJECT_JOB_STACK_BYTES);if(allocation_fail){allocations++;return 0;}return 0x100000+allocations++*size;}
int xk_mem_free(uint32_t a){assert(a>=0x100000);frees++;return 1;}
int xd3d_object_jobs_ready(void){return ready;}
int xk_object_io_step(void){abort();}
#if !defined(OBJECT_PASS_MISSING_API) && !defined(OBJECT_PASS_LINKED_OWNER)
int xv_owner_phase_active(void *c,unsigned p,uint32_t *g)
{
    if(!pthread_equal(pthread_self(),live_thread)||!live||c!=live_context||p||!g||
       xv_object_is_worker_thread()||xv_is_object_job(c)||(*g&&*g!=generation))return -1;
    if(!*g)*g=generation;return (int)scope;
}
#endif
/* Including the unchanged production TU exposes the accounting records without
 * adding test exports or controls to runtime code. */
#include "../../recomp/kernel/xk_object_jobs.c"
void f_0008FB70(xctx *c)
{
    assert(xv_is_object_job(c));assert(c->r[1]<300);
    if(budget_stop){c->preempt=0;xv_preempt(c);assert(0);}
    int lock=xv_object_math_lock();
    __atomic_add_fetch(&writes[c->r[1]],1,__ATOMIC_RELAXED);
    xv_object_math_unlock(&lock);c->r[4]+=4;
}
static void enqueue(xctx *c,unsigned n)
{
    for(unsigned i=0;i<n;i++){
        c->r[1]=i;c->r[0]=0x12345678;c->r[4]=0x20000;X_M32(c->r[4])=0x90299;
        assert(xv_object_jobs_queue(c));assert(c->r[4]==0x20004&&c->r[0]==0x12345601);
    }
}
#ifdef OBJECT_PASS_LINKED_OWNER
#include "kernel/xk_owner_phase.h"
xk_thread *xk_cur;
static xk_fiber *current_fiber;
xk_fiber *xk_os_fiber_current(void) {return current_fiber;}
static void bind_owner(xk_thread *t)
{
    xk_cur=t;t->state=0;t->fiber=(xk_fiber *)t;current_fiber=t->fiber;
    xv_owner_phase_present(&t->ctx);
}
static int linked_owner(void)
{
    g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    xk_thread first={0},second={0};xctx *c=&first.ctx;
    xv_owner_phase_configure();bind_owner(&first);
    xv_owner_phase_scope tick=0;xv_owner_phase_begin(&tick,c,XV_OWNER_TICK);assert(tick);
    assert(xv_object_jobs_begin(c));enqueue(c,257);xv_object_jobs_finish(c);
    assert(pass_timing.scope[1].count==1&&pass_timing.scope[1].joined==batch_us);
    xv_owner_phase_end(&tick);
    assert(xv_object_jobs_begin(c));xv_object_jobs_finish(c);assert(pass_timing.scope[0].count==1);
    uint32_t first_generation=pass_timing.generation;
    assert(xv_object_jobs_begin(c));bind_owner(&second);xv_object_jobs_finish(c);
    assert(pass_timing.interrupted==1&&!pass_timing.open);
    bind_owner(&first);assert(xv_object_jobs_begin(c));xv_object_jobs_finish(c);
    assert(pass_timing.generation!=first_generation&&pass_timing.scope[0].count==2);
    xv_object_jobs_report(60);xv_owner_phase_report(60);
    assert(!pass_timing.completed&&!pass_timing.interrupted);
    xv_object_jobs_shutdown();free(g_xram);free(g_xpt);
    puts("PASS actual owner API plus worker-pool generation/ancestry integration");return 0;
}
#endif
int main(int argc,char **argv)
{
#ifdef OBJECT_PASS_LINKED_OWNER
    (void)argc;(void)argv;return linked_owner();
#else
    g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    xctx c={0},other={0};live_context=&c;live_thread=pthread_self();c.r[4]=0x20000;
    if(argc>1&&!strcmp(argv[1],"init-fail")) {
        allocation_fail=1;assert(!xv_object_jobs_begin(&c));assert(initialized==-1&&allocations==1);
        assert(!xv_object_jobs_begin(&c));xv_object_jobs_report(60);xv_object_jobs_shutdown();
        assert(allocations==1&&!frees&&!clock_calls);
        free(g_xpt);free(g_xram);puts("PASS unavailable initialization stays disabled");return 0;
    }
    ready=0;assert(!xv_object_jobs_begin(&c));ready=1;
    xv_phase_enabled=1;assert(!xv_object_jobs_begin(&c));xv_phase_enabled=0;
    c.fiber=(void *)&xv_object_job_marker;assert(!xv_object_jobs_begin(&c));c.fiber=0;
    assert(!clock_calls&&allocations==0);
    xv_object_jobs_override(0);assert(!xv_object_jobs_begin(&c));xv_object_jobs_override(1);
    assert(allocations==3&&clock_calls==0);
    assert(xv_object_jobs_begin(&c));assert(!xv_object_jobs_begin(&c));
    assert(!xv_object_jobs_queue(&other));X_M32(c.r[4])=0;assert(!xv_object_jobs_queue(&c));
    if(argc>1&&!strcmp(argv[1],"budget")){budget_stop=1;enqueue(&c,1);xv_object_jobs_finish(&c);assert(0);}
    enqueue(&c,257);assert(batches==2);xv_object_jobs_join();assert(batches==3);
    for(unsigned i=0;i<257;i++)assert(writes[i]==1);
    xv_object_jobs_report(999);assert(batches==3&&owner==&c); /* no reset/open flush */
    xv_object_jobs_finish(&other);assert(owner==&c);
    xv_object_jobs_finish(&c);assert(!owner&&passes==1&&submitted==257);
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
#ifndef OBJECT_PASS_MISSING_API
    assert(pass_timing.begun==1&&pass_timing.completed==1&&!pass_timing.open);
    assert(pass_timing.scope[1].count==1&&pass_timing.scope[1].joined==batch_us);
    assert(pass_timing.scope[1].elapsed>=batch_us&&pass_timing.clocks==2);
    scope=0;assert(xv_object_jobs_begin(&c));xv_object_jobs_finish(&c);
    assert(pass_timing.scope[0].count==1&&!pass_timing.scope[0].joined);
    /* Loss of generation/live ancestry discards the entire span, never converts
       it to a zero-cost completed pass. The next admitted begin rebinds. */
    scope=1;assert(xv_object_jobs_begin(&c));generation++;xv_object_jobs_finish(&c);
    assert(pass_timing.interrupted==1&&pass_timing.completed==2&&!pass_timing.open);
    assert(xv_object_jobs_begin(&c));scope=0;xv_object_jobs_finish(&c);
    assert(pass_timing.interrupted==2&&pass_timing.invalid==2);
    live=0;assert(xv_object_jobs_begin(&c));xv_object_jobs_finish(&c);live=1;
    assert(pass_timing.unknown==1&&pass_timing.begun==4);
    /* Deliberately regress counters/clocks to verify fail-closed arithmetic. */
    assert(xv_object_jobs_begin(&c));pass_timing.start=UINT64_MAX;xv_object_jobs_finish(&c);
    assert(xv_object_jobs_begin(&c));pass_timing.batch_start=batch_us+1;xv_object_jobs_finish(&c);
    assert(pass_timing.interrupted==4&&pass_timing.invalid==4);
    /* A missed cleanup is recognized at the next valid begin, without changing
       original worker controls or inventing a successful completed interval. */
    assert(xv_object_jobs_begin(&c));live=0;xv_object_jobs_finish(&c);live=1;
    assert(!pass_timing.open&&pass_timing.interrupted==5);
    assert(xv_object_jobs_begin(&c));xv_object_jobs_finish(&c);
    assert(pass_timing.begun==8&&pass_timing.completed==3);
    xv_object_jobs_report(60);assert(!pass_timing.begun&&!pass_timing.completed&&!batch_us);
    xv_object_jobs_report(60);assert(!pass_timing.begun);
    /* Real production raw-clock fold: zero is a valid baseline, all regressions
       and failed/populated-empty samples invalidate deltas; never unsigned wrap. */
    pass_clock_record(0,11,0,1,0,100,60);assert(pass_clocks[0].valid);
    pass_clock_record(0,11,0,1,7,130,60);
    pass_clock_record(0,11,-5,1,8,140,60);assert(!pass_clocks[0].valid&&pass_clocks[0].errors==1);
    pass_clock_record(0,11,0,1,20,150,60);
    pass_clock_record(0,12,0,1,30,160,60);
    pass_clock_record(0,12,0,1,1,170,60);
    pass_clock_record(0,12,0,1,3,169,60);
    pass_clock_record(0,12,0,0,3,180,60);assert(!pass_clocks[0].valid&&pass_clocks[0].errors==2);
    pass_clock_record(1,21,0,1,UINT64_MAX,200,60);
    pass_clock_record(1,21,0,1,0,210,60); /* wrap/reset, not huge delta */
#else
    assert(pass_timing.unknown==1&&!pass_timing.begun&&!pass_timing.open);
    xv_object_jobs_report(60);assert(!pass_timing.unknown);
#endif
#endif
    assert(xv_object_jobs_begin(&c));enqueue(&c,1);
    xv_object_jobs_shutdown();assert(writes[0]==2&&frees==3&&!owner&&initialized==-1);
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
#ifndef OBJECT_PASS_MISSING_API
    typeof(pass_timing) cleared={0};
    assert(!memcmp(&pass_timing,&cleared,sizeof cleared));
    assert(!pass_clocks[0].valid&&!pass_clocks[1].valid);
#endif
#endif
    assert(!xv_object_jobs_begin(&c));xv_object_jobs_shutdown();assert(frees==3);
    free(g_xpt);free(g_xram);puts("PASS actual queue/pass lifecycle");return 0;
#endif
}
