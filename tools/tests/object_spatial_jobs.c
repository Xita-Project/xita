/* Exercise original cluster-list insertion/removal on the real job pool. */
#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
static int gameplay_ready=1;
int xd3d_object_jobs_ready(void) {return gameplay_ready;}
static unsigned allocations;
enum { N=300,TABLE=0x23000,CLUSTERS=0x24000,HEADS=0x50000,LOCATION=0x60000 };
void f_000565E0(xctx *);void f_00056670(xctx *);
void f_00052240(xctx *c) {(void)c;assert(!"radius-zero fixture must not enter BSP traversal");}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int t)
{(void)a;(void)l;(void)h;(void)t;assert(n==65536);return 0x100000+(allocations++)*65536;}
int xk_mem_free(uint32_t a) {assert(a>=0x100000&&a<0x130000);return 1;}
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1],sp=c->r[4];assert(id<N&&xv_is_object_job(c));
    for(unsigned k=0;k<12;k++) {
        c->r[3]=TABLE;X_PUSH32(HEADS+id*4);X_PUSH32(id);X_PUSH32(0x8CA3F);
        f_000565E0(c);assert(c->r[4]==sp);
        c->r[0]=LOCATION;c->r[7]=TABLE;
        X_PUSH32(0);X_PUSH32(0);X_PUSH32(HEADS+id*4);X_PUSH32(id);X_PUSH32(0x8EAB4);
        f_00056670(c);assert(c->r[4]==sp);
    }
    c->r[4]+=4;
}
static void pool(unsigned p,unsigned data)
{
    X_M16(p+0x20)=512;X_M16(p+0x22)=12;X_M16(p+0x32)=0x8000;X_M32(p+0x34)=data;
}
static void validate(void)
{
    unsigned seen[N]={0},n=0,t=X_M32(CLUSTERS);
    while(t!=0xffffffffu) {
        unsigned p=0x30000+(t&65535)*12;assert(++n<=N);
        assert(X_M16(p)==(t>>16));unsigned id=X_M32(p+4);assert(id<N&&!seen[id]++);
        t=X_M32(p+8);
    }
    assert(n==N&&X_M16(0x21000+0x30)==N&&X_M16(0x22000+0x30)==N);
    for(unsigned i=0;i<N;i++) {
        t=X_M32(HEADS+4*i);assert(t!=0xffffffffu);unsigned p=0x40000+(t&65535)*12;
        assert(X_M16(p)==(t>>16)&&X_M32(p+4)==0&&X_M32(p+8)==0xffffffffu);
    }
}
int main(void)
{
    g_xram=calloc(1,2<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<512;i++)g_xpt[i]=i*4096;
    pool(0x21000,0x30000);pool(0x22000,0x40000);
    X_M32(TABLE)=CLUSTERS;X_M32(TABLE+4)=0x21000;X_M32(TABLE+8)=0x22000;
    X_M32(CLUSTERS)=0xffffffffu;
    for(unsigned i=0;i<N;i++)X_M32(HEADS+4*i)=0xffffffffu;
    xctx c={0};c.fcw=0x37f;c.r[4]=0x70000;xv_object_jobs_override(1);
    for(unsigned round=0;round<8;round++) {
        assert(xv_object_jobs_begin(&c));
        for(unsigned i=0;i<N;i++) {
            c.r[1]=i;c.r[4]=0x70000;X_M32(c.r[4])=0x90299;assert(xv_object_jobs_queue(&c));
        }
        xv_object_jobs_finish(&c);validate();
    }
    xv_object_jobs_report(8);xv_object_jobs_shutdown();free(g_xram);free(g_xpt);
    puts("PASS: 28,800 original cluster-list remove/insert pairs across production workers; unique membership, no cycles, matching datum counts and private stacks");
}
