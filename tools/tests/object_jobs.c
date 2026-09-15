#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
int xv_phase_enabled;
static int gameplay_ready=1;
int xd3d_object_jobs_ready(void) {return gameplay_ready;}
static unsigned writes[300],active,peak,allocations;
static unsigned event_calls,event_value;
static unsigned event_seen[1200];
static pthread_t guest_owner;
static unsigned io_calls, io_seen[300];
/* Read callback outputs without the math lock: the service protocol must have
 * parked every lane, including one waiting for another worker's held mutex. */
int xk_object_io_step(void)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    for(unsigned i=0;i<300;i++)io_seen[i]=writes[i];
    io_calls++;return 1;
}
static void submit_yield(xctx *c)
{
    uint32_t sp=c->r[4];X_PUSH32(0x32B60u);X_PUSH32(0x12AA9u);
    extern void should_not_call_yield(xctx *);
    xv_object_job_hle(c,0x1D6640u,should_not_call_yield);
    assert(c->r[0]==0&&c->r[4]==sp-4);c->r[4]+=4;
}
void should_not_call_yield(xctx *c) {(void)c;assert(0);}

static void event_service(xctx *c)
{
    assert(pthread_equal(pthread_self(),guest_owner));
    assert(X_M32(c->r[4]+4)==0xfaceu);
    X_M32(X_M32(c->r[4]+8))=event_value++;
    event_calls++;c->r[0]=0x12340000u;c->r[4]+=12;
}
static void submit_event(xctx *c,uint32_t output)
{
    uint32_t sp=c->r[4];X_PUSH32(output);X_PUSH32(0xfaceu);X_PUSH32(0x12ccfu);
    xv_object_job_hle(c,0x1D665Cu,event_service);
    assert(c->r[4]==sp&&c->r[0]==0x12340000u);
    unsigned previous=X_M32(output);assert(previous<1200);
    __atomic_add_fetch(&event_seen[previous],1,__ATOMIC_RELAXED);
}
int xv_math_point_transform(xctx *c);
uint64_t xk_os_monotonic_us(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
void xk_os_log(const char *fmt,...)
{ va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a); }
uint32_t xk_mem_alloc(uint32_t size,uint32_t align,uint32_t low,uint32_t high,int top)
{ (void)align;(void)low;(void)high;(void)top;assert(size==65536);return 0x100000+(allocations++)*65536; }
int xk_mem_free(uint32_t a) {assert(a>=0x100000&&a<0x130000);return 1;}
void f_0008FB70(xctx *c)
{
    assert(xv_is_object_job(c));assert(c->r[1]<300);
    unsigned n=__atomic_add_fetch(&active,1,__ATOMIC_SEQ_CST),p=__atomic_load_n(&peak,__ATOMIC_SEQ_CST);
    while(n>p&&!__atomic_compare_exchange_n(&peak,&p,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {}
    /* Simulate substantial work while exercising nested native-helper locks. */
    struct timespec delay={0,1000000};nanosleep(&delay,NULL);
    unsigned id=c->r[1];
    {
        XV_OBJECT_MATH_GUARD();
        c->r[0]=0x60000+id*16;c->r[1]=0x30000;c->r[2]=0x50000+id*12;
        X_PUSH32(0x123456u);
        assert(xv_math_point_transform(c));
        assert(X_MF32(0x60000+id*16)==(float)id);
        assert(X_MF32(0x60004+id*16)==1.0f);
        assert(X_MF32(0x60008+id*16)==2.0f);
        { XV_OBJECT_MATH_GUARD(); writes[id]++; }
        /* Hold the shared callback lock while parking for a real owner service.
         * The owner must not run a job that blocks on this same mutex. */
        submit_event(c,0x70000+id*8);
        if(id%3==0)submit_yield(c);
    }
    /* Requests can also arrive simultaneously, outside the shared lock. */
    submit_event(c,0x70004+id*8);
    if(id%3==1)submit_yield(c);
    __atomic_sub_fetch(&active,1,__ATOMIC_SEQ_CST);c->r[4]+=4;
}
int main(int argc,char **argv)
{
    (void)argv;
    guest_owner=pthread_self();
    g_xram=calloc(1,2<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<512;i++)g_xpt[i]=i*4096;
    X_MF32(0x30000)=1;X_MF32(0x30004)=1;X_MF32(0x30014)=1;X_MF32(0x30024)=1;
    for(unsigned i=0;i<300;i++) {X_MF32(0x50000+i*12)=(float)i;X_MF32(0x50004+i*12)=1;X_MF32(0x50008+i*12)=2;}
    setenv("XV_EXPERIMENTAL_OBJECT_JOBS","0",1);
    xctx c={0};c.r[4]=0x20000;c.fs_base=0x10000;
    if(argc>1&&!strcmp(argv[1],"default-on")) {
        unsetenv("XV_EXPERIMENTAL_OBJECT_JOBS");
        assert(xv_object_jobs_begin(&c));xv_object_jobs_finish(&c);xv_object_jobs_shutdown();
        puts("PASS: dedicated experimental build starts object workers by default");
        free(g_xram);free(g_xpt);return 0;
    }
    assert(!xv_object_jobs_begin(&c));
    xv_object_jobs_override(0);assert(allocations==3);assert(!xv_object_jobs_begin(&c));
    xv_object_jobs_override(1);
    gameplay_ready=0;assert(!xv_object_jobs_begin(&c));gameplay_ready=1;
    xv_phase_enabled=1;assert(!xv_object_jobs_begin(&c));xv_phase_enabled=0;
    assert(xv_object_jobs_begin(&c));assert(!xv_object_jobs_begin(&c));
    X_M32(c.r[4])=0x902DF;assert(!xv_object_jobs_queue(&c));
    if(argc>1&&!strcmp(argv[1],"unsupported-yield")) {
        c.fiber=(void *)&xv_object_job_marker;X_M32(c.r[4])=0x12AA9;X_M32(c.r[4]+4)=0xDEADBEEF;
        xv_object_job_hle(&c,0x1D6640u,should_not_call_yield);assert(0);
    }
    if(argc>1) {c.fiber=(void *)&xv_object_job_marker;xv_object_job_hle(&c,0x1D66EC,NULL);assert(0);}
    for(unsigned round=0;round<2;round++) {
        for(unsigned i=0;i<300;i++) {
            c.r[1]=i;c.r[0]=0xabcdef88;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;
            assert(xv_object_jobs_queue(&c));assert(c.r[4]==0x20004&&c.r[0]==0xabcdef01);
        }
        xv_object_jobs_join();assert(!active);
        for(unsigned i=0;i<300;i++)assert(writes[i]==round+1);
    }
    xctx *scope=&c;xv_object_jobs_end(&scope);
    const char *workers=getenv("XV_OBJECT_JOB_WORKERS");
    unsigned expected=workers?(unsigned)atoi(workers):2u;if(!expected)expected=1;
    assert(peak==expected);
    assert(event_calls==1200&&event_value==1200);assert(io_calls>=200&&io_calls<=400);
    for(unsigned i=0;i<1200;i++)assert(event_seen[i]==1);
    xv_object_jobs_override(-1);assert(!xv_object_jobs_begin(&c));
    xv_object_jobs_report(2);xv_object_jobs_shutdown();
    printf("PASS: 600 callbacks and native point transforms exactly once; 1,200 owner-thread events and 400 cache yields with quiescent owner I/O reads, results and stack cleanup, with/without shared locks; peak %u jobs, overflow joins and restore\n",peak);
    free(g_xram);free(g_xpt);
}
