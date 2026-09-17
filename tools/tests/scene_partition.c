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
static char output[16384];static unsigned used;
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
static void route(int short_path,int recursive,int early_return)
{
    uint64_t scope __attribute__((cleanup(xv_scene_partition_end)))=0;
    xv_scene_partition_begin(&scope,&first.ctx);
    clock_value+=10;
    if(early_return)return;
    if(recursive)route(0,0,0);
    for(unsigned i=short_path?5:1;i<6;i++) {
        xv_scene_partition_step(&scope,&first.ctx,i);clock_value+=10;
    }
}
static void *foreign_thread(void *unused)
{
    (void)unused;
    for(unsigned worker_lane=0;worker_lane<2;worker_lane++) {
        on_worker=worker_lane;
        for(unsigned i=0;i<1000;i++) {
            uint64_t token=0;xv_scene_partition_begin(&token,&first.ctx);
            assert(!token);xv_scene_partition_step(&token,&first.ctx,3);
            xv_scene_partition_end(&token);
        }
    }
    return NULL;
}
int main(int argc,char **argv)
{
    assert(argc==2);int enabled=atoi(argv[1]);
    memset(&first.ctx,0xa5,sizeof first.ctx);first.ctx.fiber=NULL;
    xctx before=first.ctx;xv_owner_phase_configure();clear_log();
    uint64_t token=0;xv_scene_partition_begin(&token,&first.ctx);assert(!token&&!reads);
    select_owner(&first);clear_log();route(0,0,0);
    if(!enabled) {
        xv_owner_phase_report(60);assert(!reads&&!used&&!scene.token);
        assert(!memcmp(&before,&first.ctx,sizeof before));puts("PASS disabled");return 0;
    }
    assert(reads==7 && scene.completed==1 && !scene.token);
    for(unsigned i=0;i<6;i++)assert(scene.entries[i]==1 && scene.elapsed[i]==10);
    uint64_t old=reads;route(1,0,0);assert(reads==old+3);
    assert(scene.entries[0]==2 && scene.entries[1]==1 && scene.entries[5]==2);
    old=reads;route(0,1,0);assert(reads==old+7 && scene.recursive==1);
    old=reads;route(0,0,1);assert(reads==old+2 && scene.completed==4);
    xv_owner_phase_report(60);clear_log();
    /* Report splits one open scene bucket and both outer scopes with one clock. */
    uint64_t tick=0,whole=0;
    clock_value=1000;xv_owner_phase_begin(&tick,&first.ctx,XV_OWNER_TICK);
    xv_owner_phase_begin(&whole,&first.ctx,XV_OWNER_SCENE);
    xv_scene_partition_begin(&token,&first.ctx);
    clock_value=1010;old=reads;xv_owner_phase_report(60);assert(reads==old+1);
    assert(strstr(output,"entries 1/0/0/0/0/0 elapsed-us 10/0/0/0/0/0"));
    clear_log();clock_value=1015;xv_scene_partition_step(&token,&first.ctx,5);
    clock_value=1020;xv_scene_partition_end(&token);
    xv_owner_phase_end(&tick);xv_owner_phase_end(&whole);xv_owner_phase_report(60);
    assert(strstr(output,"entries 0/0/0/0/0/1 elapsed-us 5/0/0/0/0/5"));
    clear_log();old=reads;
    pthread_t thread;assert(!pthread_create(&thread,NULL,foreign_thread,NULL));
    for(unsigned i=0;i<1000;i++)route(0,0,0);
    assert(!pthread_join(thread,NULL));assert(reads==old+7000);
    /* Context, fiber, state and marker rejection before any timing. */
    old=reads;first.ctx.fiber=(void*)&xv_object_job_marker;
    xv_scene_partition_begin(&token,&first.ctx);assert(!token);first.ctx.fiber=NULL;
    fiber=NULL;xv_scene_partition_begin(&token,&first.ctx);assert(!token);fiber=first.fiber;
    first.state=1;xv_scene_partition_begin(&token,&first.ctx);assert(!token);first.state=0;
    xv_scene_partition_begin(&token,&second.ctx);assert(!token && reads==old);
    xv_scene_partition_begin(&token,&first.ctx);old=reads;
    xv_scene_partition_step(&token,&first.ctx,6);xv_scene_partition_step(&token,&first.ctx,0);
    assert(reads==old && scene.invalid==2);
    xk_cur=&second;xv_scene_partition_step(&token,&first.ctx,1);assert(reads==old);xk_cur=&first;
    clock_value-=1;xv_scene_partition_step(&token,&first.ctx,1);assert(scene.invalid==3);
    /* Owner rebind invalidates suspended tokens, even when context later returns. */
    uint64_t stale_token=token;select_owner(&second);select_owner(&first);
    token=0;xv_scene_partition_begin(&token,&first.ctx);uint64_t live_token=scene.token;
    old=reads;xv_scene_partition_step(&stale_token,&first.ctx,5);xv_scene_partition_end(&stale_token);
    assert(scene.token==live_token && reads==old && scene.stale==2);
    xv_scene_partition_end(&token);
    /* Scope serial exhaustion declines rather than reviving an ancient token. */
    scene.serial=UINT32_MAX;old=reads;xv_scene_partition_begin(&token,&first.ctx);
    assert(!token && scene.exhausted && reads==old);
    assert(!memcmp(&before,&first.ctx,sizeof before));
    puts("PASS six scene buckets: 7 normal / 3 skipped clocks, recursion decline, cleanup, split reports, foreign workers, live context, invalid order, clock reversal, rebound generations, serial exhaustion");
    return 0;
}
