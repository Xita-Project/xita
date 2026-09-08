#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
int xv_watch_n=1;
static unsigned watch_returns;
void original_probe(xctx *);
void f_0001D130(xctx *c) { original_probe(c); xv_cur_fn=0x1D130; }
void xv_watch_leave(uint32_t fn,uint32_t back,xctx *c)
{ (void)c;assert(fn==0x1D130&&back==0xB71C0);watch_returns++; }
void f_000B71C0(xctx *);
int xv_math_polygon_clip(xctx *);
void xv_clip_registers_override(int);
unsigned xv_math_clip_register_calls(void);
static uint32_t rng=89517;
static unsigned observing, observed;
static xctx snapshots[1024];
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void same_context(xctx a,xctx b)
{
    for(unsigned i=0;i<8;i++)if(isnan(a.st[i])&&isnan(b.st[i]))a.st[i]=b.st[i]=0;
    if(memcmp(&a,&b,sizeof a)) {
        for(unsigned i=0;i<sizeof a;i++)if(((unsigned char *)&a)[i]!=((unsigned char *)&b)[i])
            fprintf(stderr,"context byte %u: %02x != %02x\n",i,((unsigned char *)&a)[i],((unsigned char *)&b)[i]);
        abort();
    }
}
/* Inspect the exact context presented to a preemption hook, then simulate a
 * hook changing FP state. The optimized routine must save and reload it. */
void xd3d_lockstep_preempt(xctx *c)
{
    assert(xv_cur_fn==0xB71C0);
    assert(observed<1024);
    if(observing)same_context(snapshots[observed],*c);else snapshots[observed]=*c;
    observed++;
    c->preempt=19;
    c->fsp=(c->fsp+1)&7;
    for(unsigned i=0;i<8;i++)c->st[i]=i+.75;
    /* Redirect the output after a yield too: integer locals must reload the
     * resumed context just as FP locals do. Both destinations are test RAM. */
    c->r[2]=c->r[2]==0x71000 ? 0x81000 : 0x71000;
}
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
int main(int argc,char **argv)
{
    int disabled=argc>1;(void)argv;setenv("XV_NATIVE_CLIP",disabled?"0":"1",1);
    enum { BYTES=2<<20, CASES=12000 };
    g_xram=malloc(BYTES);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *before=malloc(BYTES),*expected=malloc(BYTES);
    assert(g_xram&&g_xpt&&before&&expected);
    for(unsigned i=0;i<512;i++)g_xpt[i]=(i^1)*4096;
    memset(g_xram,0xa5,BYTES);
    const uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x3f800000};
    unsigned preempts=0;
    for(unsigned k=0;k<CASES;k++) {
        if(k%3000==0){const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};assert(!fesetround(modes[k/3000]));}
        unsigned n=k%257+1,capacity=k%4 ? n+2 : k%19;
        if(k%100==0)n=512;
        uint32_t in=0x11000+(k%32)*4,out=0x21000+(k%32)*4,plane=0x31ffc,sp=0x62000+(k%4)*4;
        if(k%6==0)out=in;
        if(k%6==1)out=in+4;
        if(k%6==2)out=in-4;
        if(k%6==3){g_xpt[out>>12]=g_xpt[in>>12];g_xpt[(out>>12)+1]=g_xpt[(in>>12)+1];}
        X_MF32(0x1F0A68)=0;X_MF32(0x1F0A78)=1;
        for(unsigned i=0;i<n*2;i++) {
            float v=((int)(next()%2000)-1000)*.01f;
            if(k%4==0)v=(i%2 ? sinf(i*.2f) : cosf(i*.2f))*30;
            X_MF32(in+i*4)=v;
            if(k%11==0)X_M32(in+i*4)=edges[(k+i)%11];
            if(k%17==0&&i>=2)X_M32(in+i*4)=X_M32(in+(i%2)*4);
        }
        X_MF32(plane)=k%3==0?0:1;X_MF32(plane+4)=k%3==1?0:1;X_MF32(plane+8)=((int)(k%31)-15)*.25f;
        X_M32(sp)=0x12345678;X_M32(sp+4)=n;X_M32(sp+8)=in;X_M32(sp+12)=plane;
        X_M32(sp+16)=capacity;X_M32(sp+20)=k%5?0x43000:0;X_M32(sp+24)=k%7?0x44000:0;
        X_MF32(sp+28)=k%3==0 ? 0 : k%3==1 ? .001f : -.001f;
        xctx initial={0};
        for(unsigned i=0;i<8;i++){initial.r[i]=next();initial.st[i]=i+.375;}
        initial.r[4]=sp;initial.r[2]=out;initial.fsp=k&7;initial.fsw=next();initial.fcw=0x37f;
        initial.preempt=k%4?10000:0;initial.f_kind=XK_SUB;initial.f_bits=32;
        initial.f_op1=next();initial.f_op2=next();initial.f_res=initial.f_op1-initial.f_op2;
        memcpy(before,g_xram,BYTES);
        xctx reference=initial,candidate=initial;
        observing=observed=0;f_000B71C0(&reference);unsigned count=observed;preempts+=count;
        memcpy(expected,g_xram,BYTES);memcpy(g_xram,before,BYTES);
        observing=1;observed=0;
        int override=(int)(k%3)-1;
        xv_clip_registers_override(override);
        xv_math_clip_register_calls();
        xv_cur_fn=0xB71C0;unsigned previous_watch_returns=watch_returns;
        int used=xv_math_polygon_clip(&candidate);assert(used==!disabled);
        int expected_registers=!disabled && (override>0 || (override<0 && atoi(getenv("XV_CLIP_REGISTERS"))!=0));
        assert(xv_math_clip_register_calls()==(unsigned)expected_registers);
        assert(xv_cur_fn==0xB71C0 && watch_returns==previous_watch_returns+(unsigned)!disabled);
        if(!used){same_context(initial,candidate);assert(!memcmp(before,g_xram,BYTES));f_000B71C0(&candidate);}
        assert(count==observed);same_context(reference,candidate);
        /* C does not specify which NaN payload an arithmetic operation keeps.
         * Permit that difference only in output floating-point words. */
        const uint32_t output_buffers[]={out,0x71000,0x81000};
        for(unsigned buffer=0;buffer<(count?3u:1u);buffer++)for(unsigned i=0;i<(n+2)*2;i++) {
            unsigned off=(unsigned)((uint8_t *)X_G(output_buffers[buffer]+i*4)-g_xram);
            float a,b;memcpy(&a,expected+off,4);memcpy(&b,g_xram+off,4);
            if(isnan(a)&&isnan(b))memcpy(expected+off,g_xram+off,4);
        }
        if(memcmp(expected,g_xram,BYTES)) {
            fprintf(stderr,"memory mismatch case %u\n",k);
            for(unsigned i=0;i<BYTES;i++)if(expected[i]!=g_xram[i]){fprintf(stderr,"first byte %x: %x != %x\n",i,expected[i],g_xram[i]);break;}
            abort();
        }
        g_xpt[0x21]=0x20000;g_xpt[0x22]=0x23000;
    }
    assert(!fesetround(FE_TONEAREST));
    xv_clip_registers_override(-1);
    printf("PASS: %u native clip comparisons, all x87 TOPs/four rounding modes, full context and 2 MB guest memory, 1..512 vertices, aliases/page boundaries, nonfinite/duplicate vertices, capacity limits, %u observed preemptions, disabled=%d registers=%s\n",CASES,preempts,disabled,getenv("XV_CLIP_REGISTERS"));
    if(!disabled) {
        uint32_t sp=0x62000,in=0x11000,out=0x21000,plane=0x31000;
        X_M32(sp+4)=16;X_M32(sp+8)=in;X_M32(sp+12)=plane;X_M32(sp+16)=32;X_M32(sp+20)=0;X_M32(sp+24)=0;X_MF32(sp+28)=.001f;
        X_MF32(plane)=1;X_MF32(plane+4)=0;X_MF32(plane+8)=0;
        for(unsigned i=0;i<16;i++){X_MF32(in+i*8)=cosf(i*.4f);X_MF32(in+i*8+4)=sinf(i*.4f);}
        double times[2];
        for(unsigned native=0;native<2;native++) {
            double start=now();
            for(unsigned i=0;i<200000;i++){xctx c={0};c.r[4]=sp;c.r[2]=out;c.preempt=10000;xv_cur_fn=0xB71C0;if(native)xv_math_polygon_clip(&c);else f_000B71C0(&c);}
            times[native]=now()-start;
        }
        printf("Host polygon clip microbenchmark: original %.1f ns, native %.1f ns (not Vita frame time)\n",times[0]*5000,times[1]*5000);
    }
    free(expected);free(before);free(g_xram);free(g_xpt);
}
