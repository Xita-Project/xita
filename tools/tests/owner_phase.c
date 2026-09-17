#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "../../recomp/kernel/xk_owner_phase.c"
const char xv_object_job_marker=0;
xk_thread *xk_cur;
static xk_fiber *fiber;
static uint64_t clock_value,reads;
static char output[8192];static unsigned used;
static _Thread_local int on_worker;
int xv_object_is_worker_thread(void) {return on_worker;}
xk_fiber *xk_os_fiber_current(void) {return fiber;}
uint64_t xk_os_monotonic_us(void) {reads++;return clock_value;}
void xk_os_log(const char *fmt,...)
{ va_list ap;va_start(ap,fmt);int n=vsnprintf(output+used,sizeof output-used,fmt,ap);va_end(ap);assert(n>=0&&(unsigned)n<sizeof output-used);used+=n; }
static void clear_log(void) {used=0;output[0]=0;}
static xk_thread first,second;
static void select_owner(xk_thread *t)
{ xk_cur=t;t->state=0;t->fiber=(xk_fiber*)t;fiber=t->fiber;xv_owner_phase_present(&t->ctx); }
static int early_return(unsigned recursive)
{
    XV_OWNER_PHASE_SCOPE(&first.ctx,XV_OWNER_TICK);
    if(recursive)return early_return(0);
    clock_value+=30;return 42;
}
static void *foreign_thread(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<1000;i++) {
        XV_OWNER_PHASE_SCOPE(&first.ctx,XV_OWNER_SCENE);
    }
    on_worker=1;
    for(unsigned i=0;i<1000;i++) {
        XV_OWNER_PHASE_SCOPE(&first.ctx,XV_OWNER_TICK);
        xv_owner_phase_present(&first.ctx);
    }
    return NULL;
}
int main(int argc,char **argv)
{
    assert(argc==2);int expected=atoi(argv[1]);
    memset(&first.ctx,0xa5,sizeof first.ctx);first.ctx.fiber=NULL;
    xv_owner_phase_configure();assert(xv_owner_phase_enabled==expected);
    xctx before=first.ctx;
    { XV_OWNER_PHASE_SCOPE(&first.ctx,XV_OWNER_TICK); }
    assert(reads==0);
    select_owner(&first);clear_log();
    if(!expected) {
        early_return(1);xv_owner_phase_report(60);assert(!reads&&!used);
        assert(!memcmp(&before,&first.ctx,sizeof before));puts("PASS disabled: no clock/state/context changes");return 0;
    }
    assert(warmup==1&&rebinds==1);
    assert(early_return(1)==42);
    assert(phases[0].entries==1&&phases[0].recursive==1&&phases[0].completed==1&&phases[0].elapsed==30&&reads==2);
    /* Different selected scopes overlap; same-scope recursion does not. */
    clock_value=100;
    xv_owner_phase_scope a={0},b={0};
    xv_owner_phase_begin(&a,&first.ctx,XV_OWNER_SCENE);
    clock_value=110;xv_owner_phase_begin(&b,&first.ctx,XV_OWNER_TICK);
    clock_value=120;xv_owner_phase_report(60);
    assert(strstr(output,"FA920: entries 2 completed 1 recursive 1 open 1 elapsed-us 40"));
    assert(strstr(output,"BCB30: entries 1 completed 0 recursive 0 open 1 elapsed-us 20"));
    clear_log();clock_value=130;xv_owner_phase_end(&b);
    clock_value=150;xv_owner_phase_end(&a);xv_owner_phase_report(60);
    assert(strstr(output,"FA920: entries 0 completed 1 recursive 0 open 0 elapsed-us 10"));
    assert(strstr(output,"BCB30: entries 0 completed 1 recursive 0 open 0 elapsed-us 30"));
    /* Reporting must not consume counters from a different current context. */
    clear_log();uint64_t saved_reads=reads;xk_cur=&second;
    xv_owner_phase_report(60);assert(!used&&reads==saved_reads);xk_cur=&first;
    /* Worker and native-foreign calls never touch guest-owner timing. */
    uint64_t old=reads;pthread_t thread;assert(!pthread_create(&thread,NULL,foreign_thread,NULL));
    for(unsigned i=0;i<1000;i++)early_return(0);
    assert(!pthread_join(thread,NULL));assert(reads==old+2000&&foreign==3000);
    assert(!memcmp(&before,&first.ctx,sizeof before));
    /* Marked/fake context, stale fiber and bad phase are rejected. */
    xv_owner_phase_scope denied={0};
    first.ctx.fiber=(void*)&xv_object_job_marker;xv_owner_phase_begin(&denied,&first.ctx,XV_OWNER_TICK);assert(!denied);first.ctx.fiber=NULL;
    fiber=NULL;xv_owner_phase_begin(&denied,&first.ctx,XV_OWNER_TICK);assert(!denied);fiber=first.fiber;
    xv_owner_phase_begin(&denied,&first.ctx,2);assert(!denied);
    xv_owner_phase_begin(&denied,&second.ctx,XV_OWNER_TICK);assert(!denied);
    /* Monotonic failure drops only the negative interval, no underflow. */
    clock_value=100;xv_owner_phase_begin(&a,&first.ctx,XV_OWNER_TICK);
    old=phases[0].elapsed;clock_value=90;xv_owner_phase_end(&a);assert(phases[0].elapsed==old);
    /* Owner handoff abandons open scopes; stale cleanup cannot close new ones. */
    xv_owner_phase_begin(&a,&first.ctx,XV_OWNER_TICK);select_owner(&second);assert(abandoned==1);
    xv_owner_phase_begin(&b,&second.ctx,XV_OWNER_TICK);xv_owner_phase_end(&a);assert(stale==1&&phases[0].depth==1);
    clock_value=110;xv_owner_phase_end(&b);assert(!phases[0].depth);
    xv_owner_phase_report(0);xv_owner_phase_report(60);
    assert(strstr(output,"abandoned 1 stale 1"));
    /* A retained generation-one token must not revive on generation wrap. */
    generation=UINT32_MAX;select_owner(&first);assert(generation_exhausted&&!owner_valid);
    a=((uint64_t)1<<32)|1u;xv_owner_phase_end(&a);
    select_owner(&second);xv_owner_phase_begin(&a,&second.ctx,XV_OWNER_TICK);
    assert(!a&&!owner_valid&&generation==UINT32_MAX);
    assert(!memcmp(&before,&first.ctx,sizeof before));
    printf("PASS owner scopes: outer recursion, early cleanup, split-open reports, nested elapsed, native/worker/context declines, concurrent foreign calls, owner handoff, backwards clock, generation exhaustion; %llu clocks\n",(unsigned long long)reads);
}
