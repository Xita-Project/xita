/* Original bitmap-cache transaction on the production object pool. File I/O
 * completes synthetically only at an acknowledged owner handoff. */
#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations,requests,completions,writes[300];
enum {TAGS=0x200000,TABLE=0x230000,REQUESTS=0x260000};
int xd3d_object_jobs_ready(void) {return 1;}
void xv_preempt(xctx *c) {(void)c;assert(!"bounded cache test must not exhaust worker budget");}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int top)
{(void)a;(void)l;(void)h;(void)top;assert(n==XV_OBJECT_JOB_STACK_BYTES);return 0x100000+(allocations++)*XV_OBJECT_JOB_STACK_BYTES;}
int xk_mem_free(uint32_t a) {assert(a>=0x100000&&a<0x100000+3*XV_OBJECT_JOB_STACK_BYTES);return 1;}
void f_000325C0(xctx *);
/* The test owns only eight tags; this producer makes one pending cache record
 * per tag. Concurrent callers must observe the same record after publication. */
void f_00032510(xctx *c)
{
    unsigned tag=c->r[7],id=(tag-TAGS)/64,entry=TABLE+id*32;
    assert(id<8&&X_M32(tag+0x24)==0xffffffffu);
    X_M16(entry+2)=id;X_M32(tag+0x24)=id;X_M8(REQUESTS+id*32+0x1D)=1;
    requests++;c->r[4]+=8;
}
void f_00013030(xctx *c) {c->r[0]=500;c->r[4]+=4;}
void f_000282B0(xctx *c) {(void)c;assert(!"fixture clock must not trigger loading UI");}
static void forbidden_yield(xctx *c) {(void)c;assert(!"global scheduler must not run");}
void f_00012AA3(xctx *c)
{
    uint32_t sp=c->r[4];X_PUSH32(0x12AA9u);
    xv_object_job_hle(c,0x1D6640u,forbidden_yield);
    assert(c->r[0]==0&&c->r[4]==sp);c->r[0]=1;c->r[4]+=4;
}
int xk_object_io_step(void)
{
    /* These non-atomic guest accesses are race-free only with every worker
     * stopped. The real cache wait must resume and observe completion. */
    for(unsigned id=0;id<8;id++)if(X_M8(REQUESTS+id*32+0x1D)) {
        assert(!X_M8(TABLE+id*32+4));
        X_M8(TABLE+id*32+4)=1;X_M8(REQUESTS+id*32+0x1D)=0;completions++;
    }
    return 1;
}
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1],tag=TAGS+(id%8)*64,sp=c->r[4];assert(id<300);
    c->r[0]=tag;X_PUSH32(1);X_PUSH32(1);X_PUSH32(0xC3A30u);
    f_000325C0(c);
    assert(c->r[4]==sp&&c->r[0]==TABLE+(id%8)*32+12);
    assert(X_M8(TABLE+(id%8)*32+4)&&X_M8(TABLE+(id%8)*32+5));
    writes[id]++;c->r[4]+=4;
}
int main(void)
{
    g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
    X_M32(0x2E2D30)=0x210000;X_M32(0x210034)=TABLE;
    X_M32(0x2E2D38)=0x220000;X_M32(0x22003C)=0x240000;
    X_M32(0x240034)=0x250000;X_M32(0x220030)=42;
    X_M32(0x2E2D10)=REQUESTS;X_M32(0x2E300C)=500;
    xctx c={0};c.r[4]=0x20000;c.fs_base=0x10000;xv_object_jobs_override(1);
    for(unsigned round=0;round<4;round++) {
        for(unsigned id=0;id<8;id++) {
            X_M8(TAGS+id*64+14)=0x80;X_M32(TAGS+id*64+0x24)=0xffffffffu;
            X_M8(TABLE+id*32+4)=X_M8(TABLE+id*32+5)=0;
        }
        assert(xv_object_jobs_begin(&c));
        for(unsigned id=0;id<300;id++) {
            c.r[1]=id;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;assert(xv_object_jobs_queue(&c));
        }
        xv_object_jobs_finish(&c);
        assert(requests==8*(round+1)&&completions==requests);
        for(unsigned id=0;id<300;id++)assert(writes[id]==round+1);
    }
    xv_object_jobs_report(4);xv_object_jobs_shutdown();free(g_xram);free(g_xpt);
    puts("PASS: 1,200 original bitmap-cache calls; 32 unique publications and owner completions, shared tag reuse, returned headers and initialized flags, no recursive scheduler");
}
