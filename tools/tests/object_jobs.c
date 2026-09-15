#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
int xv_phase_enabled;
static int gameplay_ready=1;
int xd3d_object_jobs_ready(void) {return gameplay_ready;}
static unsigned writes[300],active,peak,allocations;
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
    }
    __atomic_sub_fetch(&active,1,__ATOMIC_SEQ_CST);c->r[4]+=4;
}
int main(int argc,char **argv)
{
    (void)argv;
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
    unsigned expected=workers?1u+(unsigned)atoi(workers):3u;
    assert(peak==expected);
    xv_object_jobs_override(-1);assert(!xv_object_jobs_begin(&c));
    xv_object_jobs_report(2);xv_object_jobs_shutdown();
    printf("PASS: 600 callbacks and real native point transforms exactly once, three private guest stacks, peak %u simultaneous jobs, capacity overflow joins, native math lock recursion, capture exclusion, caller filtering and restore\n",peak);
    free(g_xram);free(g_xpt);
}
