#include "kernel/xk_marker_snapshot.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
void original_marker(xctx *),current_marker(xctx *);
void xk_os_log(const char *fmt,...) {(void)fmt;}
static unsigned seed=659173;
static unsigned next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void normalize_context(xctx *c)
{
    for(unsigned i=0;i<8;i++) {
        if(isnan(c->st[i]))c->st[i]=0;
        for(unsigned j=0;j<4;j++)if(isnan(c->xmm[i][j]))c->xmm[i][j]=0;
    }
}
int main(void)
{
    enum { SIZE=4<<20,SOURCE=0x11000,DEST=0x21000,BASE=0x32000,SP=0x41000 };
    uint8_t *saved=malloc(SIZE),*expected=malloc(SIZE);
    g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    assert(saved&&expected&&g_xram&&g_xpt);
    for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=i*4096;
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    const uint32_t edge[]={0,0x80000000,1,0x00800000,0x7f800000,0xff800000,0x7fc01234,0x7f801234};
    for(unsigned k=0;k<1024;k++) {
        assert(!fesetround(modes[k%4]));memset(g_xram,0xa5,SIZE);
        xctx initial={0};
        for(unsigned j=0;j<8;j++){initial.r[j]=next();initial.st[j]=j+.25;}
        initial.r[0]=(next()&0xffff0000u)|(uint16_t)(int16_t)((int)(k%17)-8);
        initial.r[4]=SP;initial.r[6]=DEST;initial.r[7]=SOURCE;
        initial.fsp=k%8;initial.fsw=(uint16_t)next();initial.fcw=0x37f;
        initial.f_cf_override=initial.f_of_override=1;initial.f_cf=1;initial.f_of=k%2;
        uint32_t matrix=BASE+(uint32_t)(int32_t)(int16_t)initial.r[0]*52u;
        X_M32(SP+0x24)=BASE;
        xv_marker_input in={.matrix_base=BASE,.zero=0,.two=0x40000000,.one=0x3f800000};
        for(unsigned j=0;j<20;j++) {
            uint32_t word=k%3?((next()&0x807fffffu)|((115+k%20)<<23)):edge[(k+j)%8];
            if(j<4)memcpy(in.quaternion+j,&word,4);
            else if(j<7)memcpy(in.translation+j-4,&word,4);
            else memcpy(in.matrix+j-7,&word,4);
        }
        memcpy(X_G(SOURCE+16),in.quaternion,16);memcpy(X_G(SOURCE+4),in.translation,12);
        memcpy(X_G(matrix),in.matrix,52);
        X_M32(0x1f0a68)=in.zero;X_M32(0x1f0b04)=in.two;X_M32(0x1f0a78)=in.one;
        memcpy(saved,g_xram,SIZE);
        unsigned fp=_mm_getcsr()&~63u;
        _mm_setcsr(fp);xctx original=initial;original_marker(&original);
        float original_output[26];memcpy(original_output,X_G(DEST+4),104);
        memcpy(g_xram,saved,SIZE);_mm_setcsr(fp);
        xctx current=initial;current_marker(&current);unsigned final_fp=_mm_getcsr();
        memcpy(expected,g_xram,SIZE);
        xctx a=original,b=current;normalize_context(&a);normalize_context(&b);
        assert(!memcmp(&a,&b,sizeof a));
        for(unsigned j=0;j<26;j++) {
            float actual;memcpy(&actual,g_xram+DEST+4+j*4,4);
            assert(!memcmp(&actual,original_output+j,4)||(isnan(actual)&&isnan(original_output[j])));
        }
        memcpy(g_xram,saved,SIZE);_mm_setcsr(fp);
        uint8_t *arena=g_xram;uint32_t *pages=g_xpt;
        g_xram=g_img_base=NULL;g_xpt=NULL;
        xctx candidate=initial;xv_marker_result result;
        xv_marker_snapshot(&candidate,&in,&result);
        assert(_mm_getcsr()==final_fp);
        g_xram=g_img_base=arena;g_xpt=pages;
        memcpy(X_G(DEST),&result.node,2);memcpy(X_G(DEST+4),result.local,52);
        memcpy(X_G(DEST+56),result.world,52);memcpy(X_G(SP-32),result.spills,32);
        if(memcmp(&candidate,&current,sizeof current)) {
            for(unsigned j=0;j<sizeof current;j++)if(((uint8_t*)&candidate)[j]!=((uint8_t*)&current)[j])
                fprintf(stderr,"case %u context byte %u actual %02x expected %02x\n",k,j,((uint8_t*)&candidate)[j],((uint8_t*)&current)[j]);
            abort();
        }
        assert(!memcmp(g_xram,expected,SIZE));
    }
    free(g_xram);free(g_xpt);free(saved);free(expected);
    puts("PASS: 1024 marker snapshots; full native context/memory/FP equality; independent original context/output");
}
