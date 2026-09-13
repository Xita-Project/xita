#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>

enum { ARENA=1<<20, OBJECT=0x21000, SP=0x51000 };
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
void xk_os_log(const char *format, ...) { (void)format; }
void original_basis(xctx *), candidate_basis(xctx *);
int xv_math_object_basis(xctx *);
static uint32_t rng=9876543;
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }

static void compare(xctx a, xctx b, uint8_t *expected, unsigned k)
{
    /* Arithmetic NaN payload choice can differ on the host compiler. Raw
     * object fields, every other byte and all non-NaN results must match. */
    for (unsigned n=1;n<=2;n++) {
        unsigned i=(a.fsp-n)&7u;
        if (isnan(a.st[i]) && isnan(b.st[i])) a.st[i]=b.st[i]=0;
    }
    for (unsigned n=0;n<3;n++) {
        unsigned offset=(uint8_t *)X_G(a.r[4]+0x50+n*4)-g_xram;
        float x,y; memcpy(&x,expected+offset,4); memcpy(&y,g_xram+offset,4);
        if (isnan(x) && isnan(y)) memcpy(expected+offset,g_xram+offset,4);
    }
    if (memcmp(&a,&b,sizeof a)) {
        for (unsigned i=0;i<sizeof a;i++) if (((uint8_t *)&a)[i]!=((uint8_t *)&b)[i])
            fprintf(stderr,"case %u context byte %u expected %02x got %02x\n",k,i,((uint8_t *)&a)[i],((uint8_t *)&b)[i]);
        abort();
    }
    if (memcmp(expected,g_xram,ARENA)) {
        for (unsigned i=0;i<ARENA;i++) if (expected[i]!=g_xram[i]) {
            fprintf(stderr,"case %u memory byte %u expected %02x got %02x\n",k,i,expected[i],g_xram[i]); break;
        }
        abort();
    }
}

static xctx fixture(unsigned k)
{
    for (unsigned i=0;i<ARENA/4096;i++) g_xpt[i]=(i^0x40)*4096;
    memset(g_xram,0xa5,ARENA);
    xctx c={0};
    for (unsigned i=0;i<8;i++) {
        c.r[i]=next(); c.st[i]=i+.375;
        for (unsigned j=0;j<4;j++) c.xmm[i][j]=i*4+j+.25f;
    }
    c.r[4]=SP+(k%256)*4; c.r[5]=OBJECT+(k%128)*4;
    c.fsp=k%8; c.fsw=next(); c.fcw=0x37f; c.preempt=(int)next();
    c.f_kind=XK_SUB; c.f_bits=32; c.f_op1=next(); c.f_op2=next(); c.f_res=next();
    c.f_cf=next(); c.f_of=next(); c.f_cf_override=c.f_of_override=1;
    const uint32_t edges[]={0,0x80000000,1,0x807fffff,0x800000,0x7f7fffff,0xff7fffff,
                           0x7f800000,0xff800000,0x7fc01234,0x7f801234,0x3f800000};
    for (unsigned i=0;i<14;i++) {
        uint32_t value=next();
        if (k%3==0) value=(value&0x807fffff)|((110+k%30)<<23);
        if (k%3==1) value=edges[(k+i)%(sizeof edges/sizeof *edges)];
        X_M32(c.r[5]+4+i*4)=value;
    }
    X_M32(c.r[5]+4)=(X_M32(c.r[5]+4)&~0x1000u)|((k&1)<<12);
    return c;
}

int main(int argc,char **argv)
{
    assert(argc==2);
    int enabled=!strcmp(argv[1],"enabled");
    unsetenv("XV_NATIVE_OBJECT_BASIS");
    setenv("XV_NATIVE_MATH",!strcmp(argv[1],"math-disabled")?"0":"1",1);
    if (strcmp(argv[1],"unset")) setenv("XV_NATIVE_OBJECT_BASIS",!strcmp(argv[1],"disabled")?"0":"1",1);
    g_xram=malloc(ARENA); g_img_base=g_xram; g_xpt=calloc(1<<20,4);
    uint8_t *before=malloc(ARENA),*expected=malloc(ARENA);
    assert(g_xram && g_xpt && before && expected);
    for (unsigned k=0;k<4096;k++) {
        const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
        if (!(k%1024)) assert(!fesetround(modes[k/1024]));
        xctx initial=fixture(k),a=initial,b=initial;
        memcpy(before,g_xram,ARENA);
        original_basis(&a); memcpy(expected,g_xram,ARENA); memcpy(g_xram,before,ARENA);
        int accepted=xv_math_object_basis(&b);
        assert(accepted==enabled);
        if (!accepted) {
            assert(!memcmp(&b,&initial,sizeof b)); assert(!memcmp(before,g_xram,ARENA));
            candidate_basis(&b);
        }
        compare(a,b,expected,k);
    }
    assert(!fesetround(FE_TONEAREST));
    for (unsigned k=0;k<8;k++) {
        xctx c=fixture(1);
        switch(k) {
        case 0:c.r[5]++;break;
        case 1:c.r[4]++;break;
        case 2:c.r[5]=UINT32_MAX-8;break;
        case 3:c.r[4]=UINT32_MAX-8;break;
        case 4:c.r[5]=c.r[4]+0x40;break;
        case 5:g_xpt[c.r[5]>>12]=g_xpt[c.r[4]>>12];c.r[5]=OBJECT+0x40;break;
        case 6:c.r[5]=OBJECT+0xfe0;g_xpt[0x22]=g_xpt[0x25];break;
        case 7:c.r[4]=SP+0xfa0;g_xpt[0x52]=g_xpt[0x55];break;
        }
        xctx initial=c; memcpy(before,g_xram,ARENA);
        assert(!xv_math_object_basis(&c));
        assert(!memcmp(&c,&initial,sizeof c)); assert(!memcmp(before,g_xram,ARENA));
    }
    for (unsigned trap=7;trap<=12;trap++) {
        xctx c=fixture(1),initial=c; memcpy(before,g_xram,ARENA);
        unsigned control=_mm_getcsr();
        _mm_setcsr((control&~0x3fu)&~(1u<<trap));
        assert(!xv_math_object_basis(&c));
        _mm_setcsr(control);
        assert(!memcmp(&c,&initial,sizeof c)); assert(!memcmp(before,g_xram,ARENA));
    }
    printf("PASS %s: 4096 full-context/arena comparisons, 8 layout and 6 FP guard rejections\n",argv[1]);
    free(before);free(expected);free(g_xpt);free(g_xram);
    return 0;
}
