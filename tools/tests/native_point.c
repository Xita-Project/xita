#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
void xk_os_log(const char *fmt,...) {(void)fmt;}
void f_000B5EA0(xctx *);
int xv_math_point_transform(xctx *);
void xv_point_math_override(int);
static unsigned rng=0x9b51f32;
static unsigned next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char **argv) {
    int disabled=argc>1;
    int global_disabled=disabled&&strcmp(argv[1],"point-disabled");
    setenv("XV_NATIVE_MATH",global_disabled?"0":"1",1);
    setenv("XV_NATIVE_POINT_MATH",disabled&&!global_disabled?"0":"1",1);
    enum {SIZE=2<<20,CASES=12288};
    g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *initial=malloc(SIZE),*expected=malloc(SIZE);
    assert(g_xram&&g_xpt&&initial&&expected);
    unsigned fast=0,declined=0;
    unsigned edges[]={0,0x80000000,1,0x807fffff,0x00800000,0x80800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0x3f800000,0xbf800000};
    for(unsigned k=0;k<CASES;k++) {
        for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=(i^1)*4096;
        memset(g_xram,0xa5,SIZE);
        unsigned m=0x11000,v=0x21000,o=0x31000,sp=0x41000;
        switch(k%16) {
        case 1:o=v;break;
        case 2:o=v+4;break;
        case 3:o=v-4;break;
        case 4:g_xpt[o>>12]=g_xpt[v>>12];break;
        case 5:v=m+16;break;
        case 6:o=m+48;break;
        case 7:g_xpt[o>>12]=g_xpt[m>>12];break;
        case 8:m+=4092;break;
        case 9:v+=4092;break;
        case 10:o+=4092;break;
        case 11:m++;break;
        case 12:v++;break;
        case 13:o++;break;
        case 14:sp=o;break;
        }
        for(unsigned i=0;i<13;i++) {
            unsigned word=next();
            if(k%3==0)word=(word&0x807fffff)|((90+(k+i)%70)<<23);
            if(k%3==1)word=edges[(k/3+i)%(sizeof edges/sizeof *edges)];
            x_guest_write(m+4*i,&word,4);
        }
        for(unsigned i=0;i<3;i++) {
            unsigned word=next();
            if(k%3==0)word=(word&0x807fffff)|((70+(k+i)%90)<<23);
            if(k%3==1)word=edges[(k/17+3*i)%(sizeof edges/sizeof *edges)];
            x_guest_write(v+4*i,&word,4);
        }
        if(k&1) {unsigned one=0x3f800000;x_guest_write(m,&one,4);}
        xctx c;
        for(unsigned i=0;i<sizeof c;i++)((uint8_t *)&c)[i]=(uint8_t)next();
        c.r[0]=o;c.r[1]=m;c.r[2]=v;c.r[4]=sp;c.fsp=(k/16)&7;
        if(k%16==15)c.fsp=8+(k&7);
        c.f_kind=XK_SUB;c.f_bits=32;c.preempt=1000;
        xctx before=c,got=c;
        memcpy(initial,g_xram,SIZE);
        const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
        assert(!fesetround(modes[(k/128)&3]));feclearexcept(FE_ALL_EXCEPT);
        f_000B5EA0(&c);int expected_fp=fetestexcept(FE_ALL_EXCEPT);
        memcpy(expected,g_xram,SIZE);memcpy(g_xram,initial,SIZE);
        feclearexcept(FE_ALL_EXCEPT);
        int used=xv_math_point_transform(&got);
        if(!used) {
            declined++;
            assert(!memcmp(&before,&got,sizeof got));assert(!memcmp(initial,g_xram,SIZE));
            assert(!fetestexcept(FE_ALL_EXCEPT));
            f_000B5EA0(&got);
        } else fast++;
        int actual_fp=fetestexcept(FE_ALL_EXCEPT);
        if(memcmp(&c,&got,sizeof c)||memcmp(expected,g_xram,SIZE)||actual_fp!=expected_fp) {
            fprintf(stderr,"Mismatch case %u used %d FP %x/%x context %d memory %d\n",k,used,expected_fp,actual_fp,memcmp(&c,&got,sizeof c),memcmp(expected,g_xram,SIZE));return 1;
        }
        if(disabled)assert(!used);
    }
    assert(disabled?!fast:fast>1000);
    /* Benchmark override cannot bypass the global math disable. */
    xv_point_math_override(1);
    xctx c={0};c.r[0]=0x31000;c.r[1]=0x11000;c.r[2]=0x21000;
    memset(g_xram,0,SIZE);
    for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=(i^1)*4096;
    assert(xv_math_point_transform(&c)==!global_disabled);
    xv_point_math_override(0);xctx before=c;
    assert(!xv_math_point_transform(&c)&&!memcmp(&c,&before,sizeof c));
    xv_point_math_override(-1);
    assert(xv_math_point_transform(&c)==!disabled);
#if defined(__x86_64__)
    /* Decline before arithmetic when any native exception trap is enabled. */
    if(!disabled) {
        unsigned saved=_mm_getcsr();
        for(unsigned bit=7;bit<=12;bit++) {
            _mm_setcsr((saved&~0x3fu)&~(1u<<bit));
            before=c;
            assert(!xv_math_point_transform(&c)&&!memcmp(&c,&before,sizeof c));
            assert((_mm_getcsr()&0x3fu)==0);
            _mm_setcsr(saved);
        }
    }
#endif
    printf("PASS: %u original point comparisons, %u fast/%u fallback; whole context/arena/native FP status, aliases, alignment, page splits, numeric guards, all TOPs/rounding modes and override/global-disable\n",CASES,fast,declined);
    free(initial);free(expected);free(g_xram);free(g_xpt);return 0;
}
