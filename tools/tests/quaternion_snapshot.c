#include "kernel/xk_quaternion_snapshot.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
void f_000B5F60(xctx *);
int xv_math_quaternion_matrix(xctx *);
void xk_os_log(const char *fmt,...) {(void)fmt;}
static unsigned rng=77193;
static unsigned next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void context_equal(xctx a,xctx b)
{
    for(unsigned i=0;i<8;i++)if(isnan(a.st[i])&&isnan(b.st[i]))a.st[i]=b.st[i]=0;
    assert(!memcmp(&a,&b,sizeof a));
}
static void floats_equal(const float *a,const float *b,unsigned n)
{
    for(unsigned i=0;i<n;i++)assert(!memcmp(a+i,b+i,4)||(isnan(a[i])&&isnan(b[i])));
}
int main(void)
{
    enum { BYTES=4<<20, IN=0x11000, OUT=0x21000, SP=0x32000 };
    g_xram=calloc(1,BYTES);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    assert(g_xram&&g_xpt);
    for(unsigned i=0;i<BYTES/4096;i++)g_xpt[i]=i*4096;
    static const uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,
        0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234,0x3f800000};
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned k=0;k<4096;k++) {
        assert(!fesetround(modes[k%4]));
        float input[4],output[13],scratch[6],expected[13],spill[6];
        for(unsigned j=0;j<4;j++) {
            uint32_t word=k%3?next():edges[(k+j)%12];
            memcpy(input+j,&word,4);
        }
        uint32_t zero=0,two=0x40000000,one=0x3f800000;
        if(k%7==0) {zero=edges[k%12];two=edges[(k+3)%12];one=edges[(k+7)%12];}
        X_M32(0x1f0a68)=zero;X_M32(0x1f0b04)=two;X_M32(0x1f0a78)=one;
        memcpy(X_G(IN),input,16);
        xctx initial={0};
        for(unsigned j=0;j<8;j++){initial.r[j]=next();initial.st[j]=j+.375;}
        initial.r[1]=IN;initial.r[2]=OUT;initial.r[4]=SP;
        initial.fsp=k%8;initial.fsw=(uint16_t)next();initial.fcw=0x37f;
        initial.f_kind=XK_SUB;initial.f_bits=32;initial.f_op1=next();initial.f_op2=next();
        initial.f_res=next();initial.f_cf_override=initial.f_of_override=1;
        initial.f_cf=1;initial.f_of=k%2;
        unsigned fp=_mm_getcsr()&~63u;
        _mm_setcsr(fp);xctx reference=initial;f_000B5F60(&reference);
        memcpy(expected,X_G(OUT),52);memcpy(spill,X_G(SP-24),24);
        _mm_setcsr(fp);xctx wrapped=initial;assert(xv_math_quaternion_matrix(&wrapped));
        unsigned wrapped_fp=_mm_getcsr();
        context_equal(reference,wrapped);
        floats_equal(expected,X_G(OUT),13);floats_equal(spill,X_G(SP-24),6);
        float wrapped_output[13],wrapped_spill[6];
        memcpy(wrapped_output,X_G(OUT),52);memcpy(wrapped_spill,X_G(SP-24),24);
        /* The snapshot kernel must not access the guest arena or page table. */
        uint8_t *arena=g_xram;uint32_t *pages=g_xpt;
        g_xram=g_img_base=NULL;g_xpt=NULL;
        _mm_setcsr(fp);xctx local=initial;
        xv_quaternion_snapshot(&local,input,output,scratch,zero,two,one);
        local.r[4]+=4; /* wrapper owns the guest return, kernel does not */
        assert(_mm_getcsr()==wrapped_fp);
        assert(!memcmp(&local,&wrapped,sizeof local));
        assert(!memcmp(output,wrapped_output,52));assert(!memcmp(scratch,wrapped_spill,24));
        g_xram=g_img_base=arena;g_xpt=pages;
    }
    free(g_xpt);free(g_xram);
    puts("PASS: 4096 original/wrapper/snapshot cases; context, output, spills, native FP; no guest mapping in snapshot");
}
