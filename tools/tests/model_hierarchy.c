#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <xmmintrin.h>

enum { ARENA=4<<20,MODEL=0x12000,POSE=0x22000,NODES=0x32000,MATRICES=0x52000,SP=0x62000,OBJECT=0x72000 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static uint8_t *saved,*expected;
static unsigned yields,accepted,comparisons,rejections;
static uint32_t random_state=0x35476u;
void xk_os_log(const char *fmt,...) { (void)fmt; }
void original_hierarchy(xctx *),current_hierarchy(xctx *),candidate_hierarchy(xctx *);
int xv_math_model_hierarchy(xctx *);
void xv_model_hierarchy_override(int);
void __wrap_xv_preempt(xctx *c) { yields++;c->preempt=100; }
static uint32_t next(void) { uint32_t x=random_state;x^=x<<13;x^=x>>17;x^=x<<5;return random_state=x; }
static void word(uint32_t address,uint32_t v) { X_M32(address)=v; }
static xctx fixture(unsigned n,unsigned first,unsigned shape)
{
    memset(g_xram,0xa5,ARENA);
    for(unsigned i=0;i<ARENA/4096;i++)g_xpt[i]=i*4096;
    xctx c={0};
    for(unsigned i=0;i<8;i++) {
        c.r[i]=next();c.st[i]=i+.375;
        for(unsigned j=0;j<4;j++)c.xmm[i][j]=(float)(i*4+j)+.125f;
    }
    c.r[0]=first;c.r[4]=SP;c.r[5]=OBJECT;c.preempt=n+10;c.fsp=next()%8;c.fsw=(uint16_t)next();c.fcw=0x37f;
    c.f_kind=XK_SUB;c.f_bits=32;c.f_op1=next();c.f_op2=next();c.f_res=next();
    c.f_cf=1;c.f_of=1;c.f_cf_override=1;c.f_of_override=1;
    word(MODEL+0xb8,n);word(MODEL+0xbc,NODES);
    word(SP+0x24,MATRICES);word(SP+0x28,POSE);word(SP+0x2c,MODEL);word(SP+0x20,first);
    word(0x1f0a68,0);word(0x1f0a78,0x3f800000);word(0x1f0b04,0x40000000);
    /* Root uses the existing local-transform copy branch when first=0. */
    X_M8(SP+0x17)=1;
    unsigned order[64];for(unsigned i=0;i<n;i++)order[i]=i;
    if(shape==2)for(unsigned i=n-1;i>1;i--) {unsigned j=1+next()%i,t=order[i];order[i]=order[j];order[j]=t;}
    unsigned queued=shape==0?first+1:2*first+1;if(queued>n)queued=n;
    word(SP+0x10,queued);
    for(unsigned i=0;i<queued;i++)X_M16(SP+0x178+2*i)=(uint16_t)order[i];
    for(unsigned i=0;i<n;i++) {
        unsigned node=order[i];
        int a=shape==0?(int)i+1:(int)i*2+1,b=shape==0?-1:(int)i*2+2;
        X_M16(NODES+node*156+0x20)=a<(int)n?(uint16_t)order[a]:0xffff;
        X_M16(NODES+node*156+0x22)=b>=0&&b<(int)n?(uint16_t)order[b]:0xffff;
        X_M16(NODES+node*156+0x24)=i?(uint16_t)order[shape==0?i-1:(i-1)/2]:0xffff;
        float *p=X_G(POSE+node*32),*m=X_G(MATRICES+node*52);
        for(unsigned j=0;j<8;j++)p[j]=(float)((int)(next()%257)-128)/64.f;
        p[7]=1.0f;
        for(unsigned j=0;j<13;j++)m[j]=(float)((int)(next()%129)-64)/32.f;
        m[0]=1.0f;
    }
    return c;
}
static void equal(const xctx *a,const xctx *b,unsigned k,const char *which)
{
    if(memcmp(a,b,sizeof *a)) {
        for(unsigned i=0;i<sizeof *a;i++)if(((const uint8_t *)a)[i]!=((const uint8_t *)b)[i])
            fprintf(stderr,"case %u %s context byte %u expected %02x got %02x\n",k,which,i,((const uint8_t *)a)[i],((const uint8_t *)b)[i]);
        abort();
    }
    if(memcmp(expected,g_xram,ARENA)) {
        for(unsigned i=0;i<ARENA;i++)if(expected[i]!=g_xram[i]) {
            fprintf(stderr,"case %u %s memory %x expected %02x got %02x\n",k,which,i,expected[i],g_xram[i]);break;
        }
        abort();
    }
}
static void compare(xctx initial,unsigned k,int independent)
{
    memcpy(saved,g_xram,ARENA);xctx a=initial,b=initial;
    yields=0;unsigned fp=_mm_getcsr()&~63u;_mm_setcsr(fp);
    current_hierarchy(&a);unsigned end_fp=_mm_getcsr(),end_yields=yields;memcpy(expected,g_xram,ARENA);
    memcpy(g_xram,saved,ARENA);_mm_setcsr(fp);yields=0;
    candidate_hierarchy(&b);equal(&a,&b,k,"candidate");
    if(_mm_getcsr()!=end_fp) {fprintf(stderr,"case %u FP expected %x got %x\n",k,end_fp,_mm_getcsr());abort();}
    assert(yields==end_yields);
    if(independent) {
        memcpy(g_xram,saved,ARENA);b=initial;_mm_setcsr(fp);yields=0;
        original_hierarchy(&b);equal(&a,&b,k,"original");assert(yields==end_yields);
    }
    comparisons++;
}
static void reject(xctx c,unsigned k)
{
    memcpy(saved,g_xram,ARENA);xctx before=c;unsigned fp=_mm_getcsr();
    int result=xv_math_model_hierarchy(&c);
    if(result||memcmp(&c,&before,sizeof c)||memcmp(saved,g_xram,ARENA)||fp!=_mm_getcsr()) {
        fprintf(stderr,"decline case %u changed guest/FP state or accepted (%d)\n",k,result);abort();
    }
    rejections++;
}
int main(int argc,char **argv)
{
    assert(argc==2);int on=!strcmp(argv[1],"enabled");
    if(strcmp(argv[1],"unset"))setenv("XV_NATIVE_MODEL_HIERARCHY",on?"1":"0",1);
    if(!strcmp(argv[1],"math-disabled")) {setenv("XV_NATIVE_MODEL_HIERARCHY","1",1);setenv("XV_NATIVE_MATH","0",1);}
    g_xram=calloc(1,ARENA);g_img_base=g_xram;g_xpt=calloc(1,4<<20);saved=malloc(ARENA);expected=malloc(ARENA);
    assert(g_xram&&g_xpt&&saved&&expected);
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned k=0;k<192;k++) {
        assert(!fesetround(modes[k%4]));unsigned n=3+next()%62,first=k%5==0?0:1+next()%(n-2);
        xctx c=fixture(n,first,k%3);memcpy(saved,g_xram,ARENA);xctx probe=c;
        unsigned fp=_mm_getcsr();int took=xv_math_model_hierarchy(&probe);_mm_setcsr(fp);memcpy(g_xram,saved,ARENA);
        if(took) {assert(on);accepted++;assert(probe.r[0]==n-1&&probe.preempt==c.preempt-(int32_t)(n-first-1));}
        compare(c,k,1);
    }
    /* Uncomputed matrices are deliberately unusable as inputs. Every parent
     * must come from the completed prefix or an earlier freshly computed node,
     * including shuffled IDs and invocations with a consumed prefix. */
    for(unsigned shape=0;shape<3;shape++)for(unsigned first=1;first<=3;first++) {
        xctx c=fixture(32,first,shape);
        uint64_t prefix=0;
        for(unsigned i=0;i<first;i++)prefix|=1ull<<X_M16(SP+0x178+2*i);
        for(unsigned n=0;n<32;n++)if(!(prefix&(1ull<<n)))
            for(unsigned j=0;j<13;j++)word(MATRICES+n*52+j*4,0x7f800123u);
        compare(c,192+shape*3+first-1,1);
    }
    /* Only the last original node may use the widened normal-value interval.
     * Identity inputs keep earlier nodes inside the existing numeric domain,
     * so an unrelated rejection cannot hide whether the new gate admits. */
    for(unsigned field=0;field<8;field++) {
        xctx c=fixture(8,1,0);
        for(unsigned n=0;n<8;n++) {
            float *p=X_G(POSE+n*32),*m=X_G(MATRICES+n*52);
            memset(p,0,32);p[3]=p[7]=1.f;p[4]=1.f;p[5]=2.f;p[6]=3.f;
            memset(m,0,52);m[0]=m[1]=m[5]=m[9]=1.f;
        }
        word(POSE+7*32+field*4,0x2b800000u); /* 2^-40, normal */
        memcpy(saved,g_xram,ARENA);xctx probe=c;unsigned fp=_mm_getcsr();
        int took=xv_math_model_hierarchy(&probe);
#if XV_HIERARCHY_FINAL_NORMAL
        assert(took==on);
#else
        assert(!took);
#endif
        _mm_setcsr(fp);memcpy(g_xram,saved,ARENA);
        compare(c,300+field,1);
        /* Moving the same small normal into a skipped node still declines. */
        memcpy(g_xram,saved,ARENA);
        word(POSE+32+field*4,0x2b800000u);reject(c,320+field);
    }
    /* Each existing parent-matrix word can contain a small finite normal.
     * Identity child rotations preserve the value through real compositions. */
    for(unsigned field=0;field<13;field++) {
        xctx c=fixture(8,1,0);
        for(unsigned n=0;n<8;n++) {
            float *p=X_G(POSE+n*32),*m=X_G(MATRICES+n*52);
            memset(p,0,32);p[3]=p[7]=1.f;
            memset(m,0,52);m[0]=m[1]=m[5]=m[9]=1.f;
        }
        word(MATRICES+field*4,0x2b800000u);
        memcpy(saved,g_xram,ARENA);xctx probe=c;unsigned fp=_mm_getcsr();
        int took=xv_math_model_hierarchy(&probe);
#if XV_HIERARCHY_MATRIX_NORMAL
        assert(took==on);
#else
        assert(!took);
#endif
        _mm_setcsr(fp);memcpy(g_xram,saved,ARENA);
        compare(c,340+field,1);
        /* The new matrix domain still excludes subnormals and exceptions. */
        const uint32_t rejected[]={1u,0x007fffffu,0x7f800000u,0x7fc12345u,0x4e800001u};
        for(unsigned j=0;j<sizeof rejected/sizeof *rejected;j++) {
            memcpy(g_xram,saved,ARENA);
            word(MATRICES+field*4,rejected[j]);reject(c,400+field*5+j);
        }
    }
    for(unsigned k=0;k<12;k++) {
        xctx c=fixture(8,1,0);
        switch(k) {
        case 0:c.preempt=1;break;
        case 1:X_M16(NODES+156+0x20)=1;break;
        case 2:X_M16(NODES+156+0x24)=7;break;
        case 3:X_M16(NODES+156+0x20)=64;break;
        case 4:word(SP+0x24,POSE);break;
        case 5:word(SP+0x10,65);break;
        case 6:word(MODEL+0xb8,65);break;
        case 7:word(0x1f0b04,0x40400000);break;
        case 8:word(POSE+32,0x7fc12345);break;
        case 9:word(POSE+32,0x7f800000);break;
        case 10:word(POSE+32,1);break;
        case 11:word(POSE+32+28,0x4e800000);word(MATRICES,0x4e800000);break;
        }
        reject(c,k);
    }
    if(on)assert(accepted>50);else assert(!accepted);
    printf("PASS %s: %u full hierarchy comparisons, %u admitted random probes, %u unchanged declines\n",argv[1],comparisons,accepted,rejections);
    free(expected);free(saved);free(g_xpt);free(g_xram);return 0;
}
