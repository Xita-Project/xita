/* Real HLE -> command recorder -> completed GPU result, with synthetic counters. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include "../../runtime/xv_d3d.c"
#include "../kernel/xk.h"
uint8_t *g_xram;
xk_thread *xk_cur;
static unsigned yields, sleeps, sleep_us, front, back, indices_set;
static void (*wait_hook)(void);
static uint64_t fake_us=100;
static unsigned word_waits, notifications;
uint64_t xk_os_monotonic_us(void) { return fake_us; }
void xk_os_scheduler_notify(void) { notifications++; }
int xk_wait_u32(const uint32_t *word,uint32_t value,uint64_t timeout)
{
    word_waits++;assert(timeout==100000);fake_us+=250;
    if(wait_hook)wait_hook();
    fake_us+=7;
    return __atomic_load_n(word,__ATOMIC_ACQUIRE)==value;
}
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void xv_logf(const char *fmt, ...) { (void)fmt; }
void xk_yield(void) { yields++; if(wait_hook) wait_hook(); }
void xk_sleep_us(uint64_t us) { sleeps++; sleep_us = us; if(wait_hook) wait_hook(); }
void sceGxmSetFrontVisibilityTestIndex(SceGxmContext *c, unsigned i) { indices_set++; }
void sceGxmSetBackVisibilityTestIndex(SceGxmContext *c, unsigned i) { indices_set++; }
void sceGxmSetFrontVisibilityTestEnable(SceGxmContext *c, SceGxmVisibilityTestMode e) { front=e; }
void sceGxmSetBackVisibilityTestEnable(SceGxmContext *c, SceGxmVisibilityTestMode e) { back=e; }
extern void xv_hle_D3DDevice_BeginVisibilityTest(xctx *);
extern void xv_hle_D3DDevice_EndVisibilityTest(xctx *);
extern void xv_hle_D3DDevice_GetVisibilityTestResult(xctx *);
static uint32_t stack;
static uint32_t call(void (*fn)(xctx *), unsigned n, uint32_t a, uint32_t b, uint32_t d)
{
    xctx c={0}; c.r[4]=stack;
    X_M32(stack)=0x123456; X_M32(stack+4)=a; X_M32(stack+8)=b; X_M32(stack+12)=d;
    fn(&c); assert(c.r[4]==stack+4*(n+1) && X_M32(stack)==0x123456);
    return c.r[0];
}
static void begin(void) { assert(!call(xv_hle_D3DDevice_BeginVisibilityTest,0,0,0,0)); }
static void end(unsigned id) { assert(!call(xv_hle_D3DDevice_EndVisibilityTest,1,id,0,0)); }
static uint32_t read_query(unsigned id,uint32_t p,uint32_t stamp)
{ return call(xv_hle_D3DDevice_GetVisibilityTestResult,3,id,p,stamp); }
static xv_visibility_result threaded[XV_VISIBILITY_IDS];
static void complete_during_wait(void) { xv_d3d_visibility_complete(g_build_frame); }
static uint32_t submitted, acknowledged;
static void check_id_lookup(unsigned permutation)
{
    xv_visibility_result pool[XV_VISIBILITY_IDS]={0};
    uint16_t slot; uint32_t serial, pixels;
    for (unsigned i=0;i<XV_VISIBILITY_IDS;i++) {
        unsigned id=permutation ? (i*73+19)%XV_VISIBILITY_IDS : i;
        assert(!xv_visibility_issue(pool,id,&slot,&serial));
        assert(slot==i && serial==1); /* Preserve first-empty allocation. */
        pixels=0xdeadbeef;
        assert(xv_visibility_read(pool,id,&pixels)==XV_VISIBILITY_INCOMPLETE);
        assert(pixels==0xdeadbeef);
        xv_visibility_publish(pool,slot,serial,id*31+7);
    }
    for (unsigned i=0;i<XV_VISIBILITY_IDS;i++) {
        unsigned id=permutation ? (i*73+19)%XV_VISIBILITY_IDS : i;
        assert(!xv_visibility_read(pool,id,&pixels) && pixels==id*31+7);
        assert(!xv_visibility_issue(pool,id,&slot,&serial) && slot==i && serial==2);
        assert(xv_visibility_read(pool,id,&pixels)==XV_VISIBILITY_INCOMPLETE);
        xv_visibility_publish(pool,slot,serial,id*13+5);
        assert(!xv_visibility_read(pool,id,&pixels) && pixels==id*13+5);
    }
    slot=0xbeef; serial=0xcafef00d; pixels=0xdeadbeef;
    assert(xv_visibility_issue(pool,UINT32_MAX,&slot,&serial)==XV_VISIBILITY_OUT_OF_MEMORY);
    assert(slot==0xbeef && serial==0xcafef00d);
    assert(xv_visibility_read(pool,UINT32_MAX,&pixels)==XV_VISIBILITY_INVALID_ARGUMENT);
    assert(pixels==0xdeadbeef);
}
static void *publish_thread(void *unused)
{
    for (uint32_t i=1;i<=100000;i++) {
        while (__atomic_load_n(&submitted,__ATOMIC_ACQUIRE)!=i) sched_yield();
        xv_visibility_publish(threaded,0,i,i*37);
        __atomic_store_n(&acknowledged,i,__ATOMIC_RELEASE);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    unsetenv("XV_VISIBILITY_POLL_US");
    setenv("XV_VISIBILITY_BACKOFF",argc>2?"0":"1",1);
    int expected_delay=100;
    if (argc>1) { setenv("XV_VISIBILITY_POLL_US",argv[1],1); expected_delay=atoi(argv[1]); }
    if (expected_delay<0) expected_delay=0;
    if (expected_delay>1000) expected_delay=1000;
    check_id_lookup(0); /* Consecutive IDs occupy matching slots. */
    check_id_lookup(1); /* Every direct slot belongs to a different ID. */
    assert(xv_visibility_scale(848*480,640*480,848*480)==640*480);
    assert(xv_visibility_scale(960*544,640*480,960*544)==640*480);
    assert(xv_visibility_scale(424*480,640*480,848*480)==320*480);
    assert(!xv_visibility_scale(0,640*480,848*480));
    xk_mem_setup(0x10000,0x400000); g_xram=calloc(1,xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena(); stack=xk_kalloc(64);
    for (unsigned i=0;i<XV_NUM_LISTS;i++) { g_lists[i]=calloc(1,sizeof *g_lists[i]); assert(g_lists[i]); }
    g_visibility_memory=calloc(XV_NUM_LISTS*XV_VISIBILITY_WORDS,sizeof(uint32_t)); assert(g_visibility_memory);
    uint32_t out=xk_kalloc(16384), pixels=99;
    assert(read_query(123,out,0)==XV_VISIBILITY_INVALID_ARGUMENT);
    begin(); end(123);
    X_M32(out)=0xabcdef01;
    assert(read_query(123,out,0)==XV_VISIBILITY_INCOMPLETE && X_M32(out)==0xabcdef01);
    assert(sleeps==(unsigned)(expected_delay!=0) && yields==(unsigned)(expected_delay==0));
    assert(sleep_us==(unsigned)expected_delay);
    g_lists[0]->visibility_gpu_ready=1;
    for (unsigned core=0;core<4;core++) g_visibility_memory[core*XV_VISIBILITY_PER_FRAME]=10+core;
    xv_d3d_visibility_complete(0);
    assert(!read_query(123,out,out+8) && X_M32(out)==46 && !X_M64(out+8));
    assert(sleeps+yields==1); /* Ready results return immediately. */
    /* Output writes may cross noncontiguous guest pages. */
    uint32_t boundary=(out+8191)&~4095u;
    uint32_t old=g_xpt[(boundary>>12)+1]; g_xpt[(boundary>>12)+1]=g_xpt[(boundary>>12)+2];
    for (unsigned n=1;n<8;n++) {
        uint32_t addr=boundary+4096-n;
        assert(!read_query(123,addr,0)); x_guest_read(&pixels,addr,4); assert(pixels==46);
        assert(!read_query(123,0,addr)); uint64_t ts=99; x_guest_read(&ts,addr,8); assert(!ts);
    }
    g_xpt[(boundary>>12)+1]=old;
    /* A newer issue cannot be satisfied by last frame's completion. */
    g_build_frame=1; begin(); end(123);
    xv_d3d_visibility_complete(0);
    assert(read_query(123,out,0)==XV_VISIBILITY_INCOMPLETE);
    g_lists[1]->visibility_gpu_ready=1;
    xv_d3d_visibility_complete(1); assert(!read_query(123,out,0) && !X_M32(out));
    begin(); end(456);
    for (unsigned core=0;core<4;core++) g_visibility_memory[XV_VISIBILITY_WORDS+core*XV_VISIBILITY_PER_FRAME+1]=UINT32_MAX;
    xv_d3d_visibility_complete(1); assert(!read_query(456,out,0) && X_M32(out)==UINT32_MAX);
    /* Visibility only affects recorded query draws, not clears/UI/other draws. */
    cmd_t c={.visibility=2}; cmdlist_t *l=g_lists[1]; l->visibility_draw_slot=UINT32_MAX;
    visibility_draw_state(NULL,l,NULL); assert(front==SCE_GXM_VISIBILITY_TEST_DISABLED && back==front);
    visibility_draw_state(NULL,l,&c); assert(front==SCE_GXM_VISIBILITY_TEST_ENABLED && back==front && indices_set==2);
    visibility_draw_state(NULL,l,&c); assert(indices_set==2);
    c.kind=1; visibility_draw_state(NULL,l,&c); assert(front==SCE_GXM_VISIBILITY_TEST_DISABLED);
    l->visibility_gpu_ready=0; c.kind=0; visibility_draw_state(NULL,l,&c); assert(front==SCE_GXM_VISIBILITY_TEST_DISABLED);
    l->nvisibility=XV_VISIBILITY_PER_FRAME; begin(); assert(!l->active_visibility);
    assert(call(xv_hle_D3DDevice_EndVisibilityTest,1,999,0,0)==XV_VISIBILITY_OUT_OF_MEMORY);
    /* Repeated pending reads back off, but never mutate the caller's output. */
    l->nvisibility=0; begin(); end(789); X_M32(out)=0x1234ABCD;
    unsigned expected=expected_delay;
    for(unsigned i=0;i<7;i++) {
        assert(read_query(789,out,0)==XV_VISIBILITY_INCOMPLETE&&X_M32(out)==0x1234ABCD);
        assert(sleep_us==expected || !expected_delay);
        if(i<4 && argc<=2) {expected*=2;if(expected>1000)expected=1000;}
    }
    /* Completion after yielding is returned in this call, with real GPU counts. */
    l->visibility_gpu_ready=1;
    for(unsigned core=0;core<4;core++)g_visibility_memory[XV_VISIBILITY_WORDS+core*XV_VISIBILITY_PER_FRAME]=20;
    wait_hook=complete_during_wait;
    assert(!read_query(789,out,0)&&X_M32(out)==80);
    wait_hook=NULL;
    xv_visibility_result pool[XV_VISIBILITY_IDS]={0}; uint16_t slot; uint32_t serial;
    for (unsigned i=0;i<XV_VISIBILITY_IDS;i++) assert(!xv_visibility_issue(pool,UINT32_MAX-i,&slot,&serial));
    assert(xv_visibility_issue(pool,0,&slot,&serial)==XV_VISIBILITY_OUT_OF_MEMORY);
    assert(!xv_visibility_issue(pool,UINT32_MAX,&slot,&serial) && serial==2);
    /* Exercise publication through both a matching slot and the fallback. */
    for (unsigned run=0;run<2;run++) {
        uint32_t id=run ? 77 : 0;
        memset(threaded,0,sizeof threaded); submitted=acknowledged=0;
        pthread_t thread; assert(!pthread_create(&thread,NULL,publish_thread,NULL));
        for (uint32_t i=1;i<=100000;i++) {
            assert(!xv_visibility_issue(threaded,id,&slot,&serial) && slot==0 && serial==i);
            pixels=0; assert(xv_visibility_read(threaded,id,&pixels)==XV_VISIBILITY_INCOMPLETE);
            __atomic_store_n(&submitted,i,__ATOMIC_RELEASE);
            while (xv_visibility_read(threaded,id,&pixels)==XV_VISIBILITY_INCOMPLETE) sched_yield();
            assert(pixels==i*37);
            while (__atomic_load_n(&acknowledged,__ATOMIC_ACQUIRE)!=i) sched_yield();
        }
        assert(!pthread_join(thread,NULL));
    }
    /* A recording/older generation never parks the engine before Present. */
    l->nvisibility=0;begin();end(321);wait_hook=NULL;
    uint32_t age=UINT32_MAX,render_us=0;unsigned before_words=word_waits;
    assert(!xd3d_r_visibility_wait(321,100000,&age,&render_us));
    slot=l->visibility[0].result_slot;serial=l->visibility[0].serial;
    xv_visibility_submit(g_visibility_results,slot,serial-1,(uint32_t)fake_us);
    assert(!xd3d_r_visibility_wait(321,100000,&age,&render_us)&&word_waits==before_words);
    xv_visibility_submit(g_visibility_results,slot,serial,(uint32_t)fake_us);
    assert(read_query(321,out,0)==XV_VISIBILITY_INCOMPLETE&&word_waits==before_words+1);
    wait_hook=complete_during_wait;
    assert(!read_query(321,out,0)&&word_waits==before_words+2&&notifications);
    assert(xd3d_r_visibility_wait(321,100000,&age,&render_us));
    assert(age==7&&render_us>=250); /* Publication precedes the resumed read. */
    wait_hook=NULL;
#ifdef XV_FLARE_QUERY_OVERLAP
    /* Retained serials use the real recorder/publisher and wait wrapper. A
     * replacement still being recorded must not change the retained read. */
    uint32_t retained=xd3d_r_visibility_generation(321), retained_pixels;
    assert(retained==serial);
    assert(!xd3d_r_visibility_result_generation(321,retained,&retained_pixels));
    begin();end(321);
    uint32_t replacement=xd3d_r_visibility_generation(321);
    assert(replacement!=retained);
    pixels=0x12345678;
    assert(xd3d_r_visibility_result(321,&pixels)==XV_VISIBILITY_INCOMPLETE&&pixels==0x12345678);
    assert(!xd3d_r_visibility_result_generation(321,retained,&pixels)&&pixels==retained_pixels);
    before_words=word_waits;
    assert(!xd3d_r_visibility_wait_generation(321,replacement,100000)&&word_waits==before_words);
    assert(xd3d_r_visibility_wait_generation(321,retained,100000)&&word_waits==before_words+1);
    assert(!xd3d_r_visibility_wait_generation(UINT32_MAX,retained,100000));
    assert(!xd3d_r_visibility_wait_generation(321,0,100000));
#endif
    for (unsigned i=0;i<XV_NUM_LISTS;i++) free(g_lists[i]);
    free(g_visibility_memory); free(g_xram); free(g_xpt);
    puts("PASS: visibility HLE ABI, pending output, four-core counts, generations, capacity, ID permutations, GXM state, and 200000 threaded handoffs");
}
