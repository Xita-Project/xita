#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fenv.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
void f_000B5B40(xctx *), f_000B5F60(xctx *);
int xv_math_matrix_multiply(xctx *), xv_math_quaternion_matrix(xctx *);
static uint32_t rng=92351;
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static void same_float(const void *a,const void *b)
{
    float x,y; memcpy(&x,a,4);memcpy(&y,b,4);
    assert(!memcmp(a,b,4)||(isnan(x)&&isnan(y)));
}
static void same_context(xctx a,xctx b)
{
    for(unsigned i=0;i<8;i++) {
        if (isnan(a.st[i])&&isnan(b.st[i])) a.st[i]=b.st[i]=0;
        for(unsigned j=0;j<4;j++) if(isnan(a.xmm[i][j])&&isnan(b.xmm[i][j])) a.xmm[i][j]=b.xmm[i][j]=0;
    }
    assert(!memcmp(&a,&b,sizeof a));
}
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
int main(int argc,char **argv)
{
    int disabled=argc>1;(void)argv;
    setenv("XV_NATIVE_MATH",disabled?"0":"1",1);
    g_xram=calloc(1,8<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<2048;i++)g_xpt[i]=(i^1)*4096;
    X_MF32(0x1F0A68)=0;X_MF32(0x1F0A78)=1;X_MF32(0x1F0B04)=2;
    unsigned fast[2]={0},slow[2]={0};
    static const uint32_t edge[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x3f800000};
    for(unsigned fn=0;fn<2;fn++)for(unsigned k=0;k<60000;k++) {
        if(k%15000==0) {
            const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
            assert(!fesetround(modes[k/15000]));
        }
        uint32_t a=0x11000+(k%1000)*4,b=0x21000+(k%1000)*4,out=0x31000+(k%1000)*4,sp=0x41800;
        if(!fn) {
            if(k%5==1)out=a;
            if(k%5==2)out=b;
            if(k%5==3)out=b=a;
            if(k%5==4)g_xpt[out>>12]=g_xpt[a>>12];
        }
        xctx original={0};
        for(unsigned i=0;i<8;i++) {original.r[i]=next();original.st[i]=i+.375;for(unsigned j=0;j<4;j++)original.xmm[i][j]=i*4+j+.25f;}
        original.fsp=k&7;original.fsw=(uint16_t)next();original.fcw=0x37f;
        original.f_kind=XK_SUB;original.f_bits=32;original.f_op1=next();original.f_op2=next();original.f_res=original.f_op1-original.f_op2;
        original.r[4]=sp;original.r[1]=a;original.r[2]=out;
        for(unsigned j=0;j<13;j++)X_M32(out+4*j)=0xcccccccc;
        for(unsigned j=0;j<13;j++) {
            uint32_t x=next(),y=next();
            if(k%3==0){x=(x&0x807fffff)|((110+k%30)<<23);y=(y&0x807fffff)|((110+k%30)<<23);}
            if(k%3==1){x=edge[(k+j)%(sizeof edge/sizeof *edge)];y=edge[(k+3*j)%(sizeof edge/sizeof *edge)];}
            X_M32(a+4*j)=x;X_M32(b+4*j)=y;
        }
        for(unsigned j=0;j<48;j++)X_M8(sp-28+j)=0xa5;
        X_M32(sp)=0x12345678;X_M32(sp+4)=a;X_M32(sp+8)=b;X_M32(sp+12)=out;
        xctx candidate=original, initial=original;
        uint8_t before_a[52],before_b[52],before_out[52];
        x_guest_read(before_a,a,52);x_guest_read(before_b,b,52);x_guest_read(before_out,out,52);
        uint8_t before_stack[48],expected_stack[48],got_stack[48],expected[52],got[52];
        x_guest_read(before_stack,sp-28,sizeof before_stack);
        if(fn)f_000B5F60(&original);else f_000B5B40(&original);
        x_guest_read(expected,out,52);x_guest_read(expected_stack,sp-28,48);
        x_guest_write(sp-28,before_stack,48);x_guest_write(a,before_a,52);x_guest_write(b,before_b,52);x_guest_write(out,before_out,52);
        int used=fn?xv_math_quaternion_matrix(&candidate):xv_math_matrix_multiply(&candidate);
        if(used)fast[fn]++;else {
            slow[fn]++;assert(disabled);same_context(candidate,initial);
            x_guest_read(got_stack,sp-28,48);assert(!memcmp(got_stack,before_stack,48));
            x_guest_read(got,out,52);assert(!memcmp(got,before_out,52));
            if(fn)f_000B5F60(&candidate);else f_000B5B40(&candidate);
        }
        same_context(original,candidate);
        x_guest_read(got,out,52);x_guest_read(got_stack,sp-28,48);
        for(unsigned j=0;j<13;j++)same_float(expected+j*4,got+j*4);
        if(fn) {assert(!memcmp(expected_stack,got_stack,4));for(unsigned j=1;j<=6;j++)same_float(expected_stack+j*4,got_stack+j*4);assert(!memcmp(expected_stack+28,got_stack+28,20));}
        else assert(!memcmp(expected_stack,got_stack,48));
        g_xpt[0x31000>>12]=((0x31000>>12)^1)*4096;
    }
    assert(!fesetround(FE_TONEAREST));
    /* A declined fast path must leave context and guest memory untouched. */
    for(unsigned fn=0;fn<2;fn++)for(unsigned variant=0;variant<7;variant++) {
        uint32_t a=0x51000,b=0x61000,out=0x71000,sp=0x81800;
        if(variant==0)a++;
        if(variant==1)out++;
        if(variant==2)a+=4092;
        if(variant==3)out=a+(fn?0:4);
        if(variant==4)out=sp-16;
        if(variant==5){g_xpt[out>>12]=g_xpt[a>>12];if(!fn)out+=4;}
        if(variant==6)sp++;
        xctx c={0};c.r[4]=sp;c.r[1]=a;c.r[2]=out;
        X_M32(sp+4)=a;X_M32(sp+8)=b;X_M32(sp+12)=out;
        xctx before=c;uint8_t *memory=malloc(8<<20);assert(memory);memcpy(memory,g_xram,8<<20);
        assert(!(fn?xv_math_quaternion_matrix(&c):xv_math_matrix_multiply(&c)));
        assert(!memcmp(&before,&c,sizeof c)&&!memcmp(memory,g_xram,8<<20));free(memory);
        g_xpt[0x71000>>12]=((0x71000>>12)^1)*4096;
    }
    printf("PASS: native math %u/%u fast, %u/%u fallback; 120000 original comparisons, float edges, all x87 TOPs, full context, exact in-place/physical aliases, output/spill bytes and fallback guards\n",fast[0],fast[1],slow[0],slow[1]);
    if(!disabled)for(unsigned fn=0;fn<2;fn++) {
        xctx c={0};uint32_t a=0x11000,b=0x21000,out=0x31000,sp=0x41800;
        for(unsigned i=0;i<13;i++){X_MF32(a+4*i)=i*.04f;X_MF32(b+4*i)=i*.07f;}
        X_M32(sp+4)=a;X_M32(sp+8)=b;X_M32(sp+12)=out;
        double t[2];
        for(unsigned native=0;native<2;native++) {double start=now();for(unsigned n=0;n<200000;n++) {c.r[4]=sp;c.r[1]=a;c.r[2]=out;if(native){if(fn)xv_math_quaternion_matrix(&c);else xv_math_matrix_multiply(&c);}else {if(fn)f_000B5F60(&c);else f_000B5B40(&c);}}t[native]=now()-start;}
        printf("Host microbenchmark %s: original %.1f ns, native %.1f ns (not Vita frame time)\n",fn?"quaternion":"matrix",t[0]*5000,t[1]*5000);
    }
    free(g_xram);free(g_xpt);return 0;
}
