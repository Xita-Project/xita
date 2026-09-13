#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include "kernel/xk_quat_cache.h"
#include <assert.h>
#include <fenv.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>

#define ARENA (2u<<20)
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void original_quaternion(xctx *);
int current_quaternion(xctx *), xv_math_quaternion_matrix(xctx *);
void xv_quat_cache_report(unsigned);
static unsigned reported_hits, reported_misses;
void xk_os_log(const char *format, ...)
{
    if (!strstr(format,"[quat-cache]")) return;
    va_list ap; va_start(ap,format);
    (void)va_arg(ap,unsigned);
    reported_hits += va_arg(ap,unsigned);
    reported_misses += va_arg(ap,unsigned);
    va_end(ap);
}
static uint32_t rng=0x87654321;
static uint32_t next(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }
static unsigned case_number;
static void context_equal(xctx a,xctx b)
{
    for (unsigned i=0;i<8;i++)
        if (isnan(a.st[i]) && isnan(b.st[i])) a.st[i]=b.st[i]=0;
    if (memcmp(&a,&b,sizeof a)) {
        fprintf(stderr,"context mismatch case %u\n",case_number);
        for(unsigned i=0;i<sizeof a;i++)if(((uint8_t*)&a)[i]!=((uint8_t*)&b)[i]) {
            fprintf(stderr,"byte %u: %02x %02x\n",i,((uint8_t*)&a)[i],((uint8_t*)&b)[i]); break;
        }
        abort();
    }
}
static void memory_equal(const uint8_t *expected,uint32_t output,uint32_t sp)
{
    /* NaN payload selection can differ between the original and native C.
     * Normalize only arithmetic output/spill words, never pointers or guards. */
    for(unsigned region=0;region<2;region++) {
        unsigned address=region ? sp-24 : output, bytes=region ? 24 : 52;
        for(unsigned j=0;j<bytes;j+=4) {
            unsigned at=g_xpt[(address+j)>>12]+((address+j)&4095);
            float a,b;memcpy(&a,expected+at,4);memcpy(&b,g_xram+at,4);
            if(isnan(a)&&isnan(b))memcpy(g_xram+at,expected+at,4);
        }
    }
    if(memcmp(expected,g_xram,ARENA)) {
        fprintf(stderr,"arena mismatch case %u\n",case_number);abort();
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);
    int on=!strcmp(argv[1],"enabled"), math_off=!strcmp(argv[1],"math-disabled");
    if(!strcmp(argv[1],"unset"))unsetenv("XV_QUAT_CACHE");else setenv("XV_QUAT_CACHE",on?"1":"0",1);
    setenv("XV_NATIVE_MATH",math_off?"0":"1",1);
    g_xram=malloc(ARENA);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *before=malloc(ARENA),*expected=malloc(ARENA),*current_memory=malloc(ARENA);
    assert(g_xram&&g_xpt&&before&&expected&&current_memory);
    for(unsigned i=0;i<ARENA/4096;i++)g_xpt[i]=(i^0x40)*4096;
    const uint32_t edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,
                           0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f800001,0x3f800000};
    unsigned compared=0;
    for(unsigned k=0;k<1024;k++) {
        case_number=k;
        memset(g_xram,0xa5,ARENA);
        uint32_t a=0x11080,out=0x21080,sp=0x31800;
        X_MF32(0x1F0A68)=k%17==0 ? 1.0f : 0.0f;
        X_MF32(0x1F0A78)=k%19==0 ? .5f : 1.0f;
        X_MF32(0x1F0B04)=k%23==0 ? 1.0f : 2.0f;
        for(unsigned j=0;j<4;j++) {
            uint32_t bits=next();
            if(k%3==0)bits=(bits&0x807fffff)|((120+k%10)<<23);
            if(k%3==1)bits=edges[(k+j)%(sizeof edges/sizeof *edges)];
            X_M32(a+j*4)=bits;
        }
        xctx initial={0};
        for(unsigned i=0;i<8;i++) {
            initial.r[i]=next();initial.st[i]=i+.125;
            for(unsigned j=0;j<4;j++)initial.xmm[i][j]=i*4+j+.75f;
        }
        initial.r[1]=a;initial.r[2]=out;initial.r[4]=sp;
        initial.fsp=k&7;initial.fsw=next();initial.fcw=next();
        initial.f_kind=XK_SUB;initial.f_bits=32;initial.f_op1=next();initial.f_op2=next();
        initial.f_res=initial.f_op1-initial.f_op2;initial.f_cf=next();initial.f_of=next();
        memcpy(before,g_xram,ARENA);
        /* First/warm calls use different native sticky flags and guest TOP/
         * status words. None of those may leak from the cached caller. */
        for(unsigned warm=0;warm<3;warm++) {
            xctx base=initial;base.fsp=(k+warm*3)&7;base.fsw^=(uint16_t)(warm*0x5317);
            base.st[base.fsp]=123+warm;base.st[(base.fsp+1)&7]=456+warm;
            uint32_t fp=0x1f80|((k%4)<<13)|(warm==1?0x3f:0);
            memcpy(g_xram,before,ARENA);xctx original=base;
            _mm_setcsr(fp);original_quaternion(&original);memcpy(expected,g_xram,ARENA);
            memcpy(g_xram,before,ARENA);xctx current=base;_mm_setcsr(fp);
            if(!current_quaternion(&current))original_quaternion(&current);
            uint32_t current_fp=_mm_getcsr();
            memcpy(current_memory,g_xram,ARENA);
            context_equal(original,current);memory_equal(expected,out,sp);
            memcpy(g_xram,before,ARENA);xctx candidate=base;_mm_setcsr(fp);
            int used=xv_math_quaternion_matrix(&candidate);
            if(!used) {assert(math_off);context_equal(base,candidate);assert(!memcmp(before,g_xram,ARENA));original_quaternion(&candidate);}
            uint32_t candidate_fp=_mm_getcsr();
            assert(!memcmp(&current,&candidate,sizeof current));
            assert(!memcmp(current_memory,g_xram,ARENA));
            context_equal(original,candidate);memory_equal(expected,out,sp);
            assert(candidate_fp==current_fp);
            compared++;
        }
    }
    if(on) {
        /* A caller enabling native FP exceptions must retain the original
         * calculation path; cache lookup itself may not change its state. */
        for(unsigned bit=7;bit<=12;bit++) {
            xctx c={0},saved=c;
            xv_quat_cache_request request;
            uint32_t fp=0x1f80u&~(1u<<bit);
            memcpy(before,g_xram,ARENA);_mm_setcsr(fp);
            assert(!xv_quat_cache_restore(&c,X_G(0x11080),X_G(0x21080),
                                          X_G(0x317e8),X_G(0x1f0a68),&request));
            uint32_t after=_mm_getcsr();_mm_setcsr(0x1f80);
            assert(!request.active && after==fp && !memcmp(&c,&saved,sizeof c));
            assert(!memcmp(before,g_xram,ARENA));
        }
    }
    _mm_setcsr(0x1f80);
    xv_quat_cache_report(compared);
    assert(on ? reported_hits>=2048 && reported_misses>0 : reported_hits==0 && reported_misses==0);
    printf("PASS %s: %u original/current/cache comparisons; %u hits %u misses; native FP flags, rounding, constants, TOP/status and full arena\n",
           argv[1],compared,reported_hits,reported_misses);
    free(g_xram);free(g_xpt);free(before);free(expected);free(current_memory);
    return 0;
}
