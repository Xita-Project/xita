#include <assert.h>
#include "../kernel/xk_thread.c"
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static uint64_t clock_us;
static unsigned switches, destroyed;
static xk_thread *expected;
static int run_selected;
static xk_fiber *const root_fiber=(xk_fiber *)(uintptr_t)1;
uint64_t xk_os_monotonic_us(void) { return clock_us; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
xk_fiber *xk_os_fiber_main(void) { return root_fiber; }
void xk_os_fiber_switch(xk_fiber *f)
{
    switches++;
    if (!run_selected) { assert(f==root_fiber); return; }
    assert(expected && f==expected->fiber && xk_cur==expected);
    expected->state=3;
}
void xk_os_fiber_destroy(xk_fiber *f) { assert(f); destroyed++; }
void xk_os_scheduler_wait(uint64_t us) { (void)us; assert(0); }
int main(int argc,char **argv)
{
    int disabled=argc>1;
    if(disabled)setenv("XV_FAST_YIELD","0",1);else unsetenv("XV_FAST_YIELD");
    g_xram=calloc(1,1<<20);g_xpt=calloc(1<<20,sizeof *g_xpt);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<256;i++)g_xpt[i]=i*4096;
    xk_var_KeTickCount=0x100;
    xk_thread me={0},peer={0};g_threads=&me;me.next=&peer;peer.state=2;
    me.ctx.r[4]=0x8000;me.id=4;peer.id=8;peer.fiber=(xk_fiber *)(uintptr_t)2;
    xk_cur=&me;clock_us=10;
    xk_yield();assert(switches==(unsigned)disabled && !g_yield_next);
    /* A future sleep cannot make a useful recipient; a due sleep must run. */
    peer.state=1;peer.wait_until=1000;
    xk_yield();assert(switches==2u*disabled && peer.state==1);
    clock_us=100;xk_yield();
    if(!disabled)assert(peer.state==0 && g_yield_next==&peer);
    if(disabled) { /* Selection still belongs to the normal root scheduler. */
        assert(!g_yield_next);assert(pick_next(&me)==&peer);
    }
    g_yield_next=NULL;peer.state=2;
    /* A boost retains its ordering. */
    peer.state=0;xk_thread_kick(&peer);xk_yield();
    if(!disabled)assert(g_yield_next==&peer && !g_boost);
    g_yield_next=NULL;g_boost=NULL;
    /* Auto-reset event is consumed once, and the chosen thread is remembered. */
    xk_obj event={.type=XO_EVENT};event.u.event.signaled=1;
    peer.state=1;peer.wait_until=0;peer.wait_n=1;peer.wait_objs[0]=&event;
    xk_yield();
    if(!disabled)assert(g_yield_next==&peer && !event.u.event.signaled && peer.state==0);
    else assert(pick_next(&me)==&peer && !event.u.event.signaled);
    me.state=3;run_selected=1;expected=&peer;
    xk_run_until_idle();assert(peer.state==3 && destroyed==1 && !g_yield_next);
    /* Pending completion and actual sleeps always return to the scheduler. */
    run_selected=0;me.state=1;xk_cur=&me;unsigned before=switches;
    xk_yield();assert(switches==before+1 && !g_yield_next);
    /* Profiler only requests; the guest owns the eventual counter reset. */
    g_yield_calls=123;xk_wait_stats_request();assert(g_yield_calls==123);
    xk_yield();assert(!g_wait_dump_requested && g_yield_calls==1);
    free(g_xpt);free(g_xram);
    puts("PASS: same-thread yield bypass, due sleeps, boosts, single event consumption, preserved root selection, real waits and guest-owned statistics");
}
