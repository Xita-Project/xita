#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations, writes[300];
static int overflow;
int xd3d_object_jobs_ready(void) {return 1;}
int xk_object_io_step(void) {assert(!"stack fixture has no I/O");return 0;}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int top)
{
    (void)a;(void)l;(void)h;(void)top;
    assert(n==XV_OBJECT_JOB_STACK_BYTES);
    return 0x100000+(allocations++)*XV_OBJECT_JOB_STACK_BYTES;
}
int xk_mem_free(uint32_t a)
{assert(a>=0x100000&&a<0x100000+3*XV_OBJECT_JOB_STACK_BYTES);return 1;}
void f_0001D130(xctx *);
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1],sp=c->r[4];assert(id<300);
    if(overflow) {
        c->r[0]=XV_OBJECT_JOB_STACK_BYTES;X_PUSH32(0x1234);
        f_0001D130(c);assert(!"oversized allocation must stop before writing");
    }
    /* The vehicle callback has a 0x3280 frame. Combine it with larger nested
     * frames to cross the old 64 KiB allocation and exercise the real helper. */
    unsigned sizes[]={0,0x3280,0xD000,0x18000},starts[4];
    for(unsigned i=0;i<4;i++) {
        unsigned before=c->r[4];c->r[0]=sizes[i];X_PUSH32(0x12340000+i);
        f_0001D130(c);
        assert(c->r[4]==before-sizes[i]);
        assert(c->r[0]==(sizes[i]?0x12340000+i:0));
        assert(c->r[1]==id);
        starts[i]=c->r[4];
        if(sizes[i]) {
            X_M32(starts[i])=0xA0000000+id*16+i;
            X_M32(starts[i]+sizes[i]-4)=0xB0000000+id*16+i;
        }
    }
    for(unsigned i=1;i<4;i++) {
        assert(X_M32(starts[i])==0xA0000000+id*16+i);
        assert(X_M32(starts[i]+sizes[i]-4)==0xB0000000+id*16+i);
    }
    writes[id]++;c->r[4]=sp+4;
}
int main(int argc,char **argv)
{
    (void)argv;overflow=argc>1;
    g_xram=calloc(1,2<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<512;i++)g_xpt[i]=i*4096;
    xctx c={0};c.r[4]=0x20000;xv_object_jobs_override(1);
    /* A normal owner context remains completely untouched by the probe. */
    c.r[0]=0xffffffff;xctx before=c;xv_object_job_stack_probe(&c);assert(!memcmp(&c,&before,sizeof c));
    assert(xv_object_jobs_begin(&c));
    for(unsigned id=0;id<300;id++) {
        c.r[1]=id;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;assert(xv_object_jobs_queue(&c));
    }
    xv_object_jobs_finish(&c);
    for(unsigned id=0;id<300;id++)assert(writes[id]==1);
    xv_object_jobs_report(1);xv_object_jobs_shutdown();free(g_xram);free(g_xpt);
    puts("PASS: 300 callbacks with original nested stack allocations above 64 KiB; distinct frame data, zero-size allocation and return convention preserved");
}
