#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void f_0005C300(xctx *);
int xv_math_bounds(xctx *);
void xv_native_bounds_override(int);
unsigned xv_math_bounds_calls(void);
enum { ARENA=2<<20, TRACES=32 };
static uint32_t seed=0x5c300, frame_address;
static unsigned trace_count, expected_count, total_yields, mutate, candidate;
static xctx traces[TRACES];
static uint32_t next(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static void same_context(xctx a,xctx b)
{
    for(unsigned i=0;i<8;i++) if(isnan(a.st[i])&&isnan(b.st[i]))a.st[i]=b.st[i]=0;
    assert(!memcmp(&a,&b,sizeof a));
}
void xd3d_lockstep_preempt(xctx *c)
{
    assert(trace_count<TRACES);
    if(candidate) { assert(trace_count<expected_count);same_context(*c,traces[trace_count]); }
    else traces[trace_count]=*c;
    trace_count++;total_yields++;
    c->preempt=mutate?1:3;
    if(mutate) {
        /* The optimized path must publish and reload the entire live state. */
        c->fsp=(c->fsp+3)&7u;c->st[(c->fsp+6)&7u]=42.125;
        c->fsw^=0x80;c->f_cf^=1;c->r[0]^=0x12340000;
        float v; x_guest_read(&v,frame_address+0x84,4);
        v+=0.03125f;x_guest_write(frame_address+0x84,&v,4);
    }
}
static void put_float(uint32_t a,float v) { x_guest_write(a,&v,4); }
static void put_word(uint32_t a,uint32_t v) { x_guest_write(a,&v,4); }
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
static xctx setup(unsigned k)
{
    memset(g_xram,0xa5,ARENA);
    const unsigned offsets[]={0,1,2,3,0xffc,0xffd,0xffe,0xfff};
    uint32_t f=0x18000+offsets[k%8],box=0x28000+offsets[(k/8)%8];
    uint32_t sp=0x61800+(k%4);
    if(k%19==0)sp=0x62004; /* scratch straddles remapped pages */
    if(k%23==0)box=f+0x128; /* read-only input overlap */
    if(k%29==0)box=0x38000+(f&0xfff); /* physical alias of the frustum */
    if(k%31==0)box=sp-0x60; /* output scratch aliases input: no snapshot shortcut */
    if(k%37==0)f=sp-0x100;
    frame_address=f;
    xctx c={0};
    for(unsigned i=0;i<8;i++){c.r[i]=next();c.st[i]=i+.125;for(unsigned j=0;j<4;j++)c.xmm[i][j]=i+j*.125f;c.mm[i]=next();}
    c.fs_base=next();c.df=k&1;c.fsp=k&7;c.fsw=(uint16_t)next();c.fcw=(uint16_t)(0x37f|((k%4)<<10));
    c.f_kind=XK_SUB;c.f_bits=32;c.f_op1=next();c.f_op2=next();c.f_res=c.f_op1-c.f_op2;
    c.f_cf_override=next()&1;c.f_cf=next()&1;c.f_of_override=next()&1;c.f_of=next()&1;
    c.preempt=k%3==0?1:1000;c.scratch=next();c.eip_hint=next();c.r[1]=f;c.r[7]=box;c.r[4]=sp;
    /* Four side planes, broad bounds and five frustum points. */
    const float planes[4][4]={{1,0,0,1},{-1,0,0,1},{0,1,0,1},{0,-1,0,1}};
    for(unsigned i=0;i<16;i++)put_float(f+0x78+i*4,planes[i/4][i%4]);
    const float corners[5][3]={{-1,-1,-1},{1,-1,1},{1,1,-1},{-1,1,1},{0,0,0}};
    for(unsigned i=0;i<15;i++)put_float(f+0xe0+i*4,corners[i/3][i%3]);
    for(unsigned i=0;i<6;i++)put_float(f+0x128+i*4,(i&1)?3:-3);
    for(unsigned i=0;i<6;i++)put_float(box+i*4,(i&1)?.5f:-.5f);
    unsigned shape=(k/64)%10;
    if(shape==1) {put_float(box,4);put_float(box+4,5);}
    if(shape==2) {put_float(box,1.5f);put_float(box+4,2);}
    if(shape>=3&&shape<=5) {put_float(box,.5f);put_float(box+4,1.5f);}
    if(shape==5)for(unsigned i=0;i<5;i++)put_float(f+0xe0+i*12,-1);
    if(shape==6)for(unsigned i=0;i<16;i++)put_float(f+0x78+i*4,(int)(next()%65)/16.f-2);
    if(shape>=7) {
        const uint32_t edge[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234,0x3f800000};
        for(unsigned i=0;i<16;i++)put_word(f+0x78+i*4,shape==7?edge[(k+i)%12]:next());
        if(shape==9)for(unsigned i=0;i<6;i++)put_word(box+i*4,edge[(k+3*i)%12]);
    }
    put_float(0x1f0a68,0);put_word(sp,0x12345678);put_word(sp+4,shape==3?0:1);
    return c;
}
int main(int argc,char **argv)
{
    (void)argv;int disabled=argc>1;
    setenv("XV_NATIVE_BOUNDS",disabled?"0":"1",1);
    g_xram=malloc(ARENA);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *before=malloc(ARENA),*expected=malloc(ARENA);assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<ARENA/4096;i++)g_xpt[i]=(i^1)*4096;
    g_xpt[0x38]=g_xpt[0x18];g_xpt[0x39]=g_xpt[0x19];
    const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    unsigned results[3]={0},calls=0;
    for(unsigned k=0;k<10240;k++) {
        if(k%2560==0)assert(!fesetround(rounds[k/2560]));
        xctx a=setup(k),b=a,initial=a;memcpy(before,g_xram,ARENA);
        candidate=0;trace_count=0;mutate=k%7==0;
        f_0005C300(&a);expected_count=trace_count;memcpy(expected,g_xram,ARENA);
        if((a.r[0]&0xffff)<3)results[a.r[0]&0xffff]++;
        memcpy(g_xram,before,ARENA);candidate=1;trace_count=0;
        int used=xv_math_bounds(&b);
        if(disabled) {assert(!used);same_context(initial,b);assert(!memcmp(before,g_xram,ARENA));f_0005C300(&b);}
        else {assert(used);calls++;}
        assert(trace_count==expected_count);same_context(a,b);
        if(memcmp(expected,g_xram,ARENA)) {
            /* Permit arithmetic NaN payload differences only in the float scratch area. */
            for(unsigned i=0;i<96;i+=4) {
                uint32_t address=initial.r[4]-96+i;
                uint8_t got[4],want[4];float x,y;
                x_guest_read(got,address,4);
                for(unsigned j=0;j<4;j++)want[j]=expected[g_xpt[(address+j)>>12]+((address+j)&4095)];
                memcpy(&x,got,4);memcpy(&y,want,4);
                if(isnan(x)&&isnan(y))x_guest_write(address,want,4);
            }
            assert(!memcmp(expected,g_xram,ARENA));
        }
    }
    assert(!fesetround(FE_TONEAREST));assert(results[0]&&results[1]&&results[2]);
    assert(xv_math_bounds_calls()==calls);assert(!xv_math_bounds_calls());
    /* Explicit comparison overrides win, then restore the configured value. */
    xctx c=setup(300),saved=c;memcpy(before,g_xram,ARENA);
    xv_native_bounds_override(0);assert(!xv_math_bounds(&c));same_context(c,saved);assert(!memcmp(before,g_xram,ARENA));
    xv_native_bounds_override(1);candidate=0;trace_count=0;assert(xv_math_bounds(&c));
    xv_native_bounds_override(-1);c=setup(300);candidate=0;trace_count=0;assert(xv_math_bounds(&c)==!disabled);
    printf("PASS: bounds %u/%u/%u outside/partial/inside, 10240 complete-memory/context comparisons; %u preemptions, all x87 TOPs, rounding, page splits, aliases and override restoration\n",results[0],results[1],results[2],total_yields);
    if(!disabled) {
        /* Finite inputs, no tracing/preemption: diagnostic host timing only. */
        mutate=0;candidate=0;trace_count=0;
        for(unsigned shape=0;shape<4;shape++) {
            xctx initial=setup(64*shape*1+1);initial.preempt=1000000;
            double times[2];unsigned checksum=0;
            for(unsigned native=0;native<2;native++) {
                double t=now();
                for(unsigned n=0;n<200000;n++){xctx c=initial;if(native)xv_math_bounds(&c);else f_0005C300(&c);checksum+=c.r[0]&0xffff;}
                times[native]=now()-t;
            }
            printf("Host case %u original %.1f ns native %.1f ns checksum %u (not Vita FPS)\n",shape,times[0]*5000,times[1]*5000,checksum);
        }
    }
    free(before);free(expected);free(g_xram);free(g_xpt);return 0;
}
