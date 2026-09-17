#include "xv_x86rt.h"
#include "kernel/xk_collision_traversal.h"
#include <stddef.h>
#include <stdlib.h>
#ifndef TEST_ARM
#include <assert.h>
#include <stdio.h>
#include <fenv.h>
#endif
/* Synthetic fixture geometry only. Full arena/context comparisons include all
 * original stack writes. No saved guest body or map data is present here. */
enum { ARENA=8<<20, PAGES=512, Q=0x10000,W=0x11000,N=0x12000,P=0x13000,
       L=0x14000,R=0x15000,T=0x16000,S=0x17000,E=0x18000,V=0x1a000,
       C=0x1d000,O=0x20000,SP=0x90000 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static xctx context;
xctx *const arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),
 offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned ct_site,ct_yields,ct_events,ct_mutation,ct_entry,ct_seen;
unsigned ct_alias,ct_projection_checks,ct_projection_accepted,ct_projection_declined;
uint32_t ct_original_pages[PAGES],ct_alternate_pages[PAGES];
#define original_pages ct_original_pages
#define alternate_pages ct_alternate_pages
void original_00088110(xctx *);void candidate_00088110(xctx *);void raw_00088110(xctx *);
void original_00087EA0(xctx *);void candidate_00087EA0(xctx *);void raw_00087EA0(xctx *);
void original_00087E10(xctx *);void candidate_00087E10(xctx *);void raw_00087E10(xctx *);
#ifdef TEST_ARM
void abort(void){__builtin_trap();}
#endif
void x_guest_read_pages(void *dst,uint32_t a,size_t n)
{uint8_t *d=dst;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(d,X_G(a),k);d+=k;a+=(uint32_t)k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void *src,size_t n)
{const uint8_t *s=src;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),s,k);s+=k;a+=(uint32_t)k;n-=k;}}
static void put(unsigned a,unsigned v){X_M32(a)=v;}
static void flt(unsigned a,float v){X_MF32(a)=v;}
static void hash(const void *p,unsigned n)
{const uint8_t *b=p;for(unsigned i=0;i<n;i++)ct_events=(ct_events^b[i])*16777619u;}
void ct_observe_entry(xctx *c,unsigned address)
{hash(&address,sizeof address);hash(c,sizeof *c);}
void ct_projection_result(unsigned admitted)
{if(admitted)ct_projection_accepted++;else ct_projection_declined++;
 if(ct_alias){ct_projection_checks++;if(admitted)abort();}}
void __wrap_xv_preempt(xctx *c)
{
    if(++ct_yields>100000)abort();
    hash(c,sizeof *c);hash(&ct_site,sizeof ct_site);
    for(unsigned i=0;i<0x50;i++){uint8_t b=X_M8(c->r[4]+i);hash(&b,1);}
    switch(ct_site){case 0x87e7f:ct_seen|=1;break;case 0x87f18:ct_seen|=2;break;
      case 0x87f8d:ct_seen|=4;break;case 0x87f96:ct_seen|=8;break;case 0x880f6:ct_seen|=16;break;}
    c->preempt=3;
    if(ct_yields!=1)return;
    switch(ct_mutation){
    case 1:c->f_cf^=1;c->f_of^=1;c->fsw^=0x4100;break;
    case 2:memcpy(g_xram+0x300000,g_xram+g_xpt[P>>12],4096);g_xpt[P>>12]=0x300000;break;
    case 3:flt(P+12,.125f);flt(V,.625f);break;
    case 4:c->fsp=(c->fsp+3)&7;c->st[c->fsp]=.75;break;
    case 5:memcpy(alternate_pages,g_xpt,sizeof alternate_pages);g_xpt=alternate_pages;
        memcpy(g_xram+0x301000,g_xram+g_xpt[P>>12],4096);g_xpt[P>>12]=0x301000;
        if(ct_site==0x87f18){
            unsigned page=c->r[4]>>12;
            memcpy(g_xram+0x302000,g_xram+g_xpt[page],4096);g_xpt[page]=0x302000;
            /* Root tail descent has saved EBX at +12; outer 88110 does not
             * consume EBX, so the mutation remains safe and observable. */
            X_M32(c->r[4]+12)^=1;
        }break;
    case 6:if(ct_site==0x87f8d){c->r[0]=0;c->r[2]=0;}break;
    case 7:if(ct_site==0x87f96)c->r[0]^=1;break;
    }
}
void arm_prepare(unsigned depth,unsigned variant,unsigned budget)
{
    for(unsigned i=0;i<PAGES;i++)original_pages[i]=(i^1)*4096;
    memset(alternate_pages,0,sizeof alternate_pages);g_xpt=original_pages;
    memset(g_xram,0x33,ARENA);g_img_base=g_xram+(4<<20);
    memset(&context,0xa5,sizeof context);
    unsigned surfaces=1+(variant&3), mode=(variant>>2)&3;
    put(W+4,N);put(W+0x10,P);put(W+0x1c,L);put(W+0x28,R);put(W+0x34,T);
    put(W+0x40,S);put(W+0x4c,E);put(W+0x58,V);
    flt(C,mode==1?10.f:mode==2?-10.f:0.f);flt(C+4,0);flt(C+8,0);
    for(unsigned i=0;i<depth;i++){
        put(N+i*12,i);put(N+i*12+4,0x80000000u|i);
        put(N+i*12+8,i+1<depth?i+1:0xffffffffu);
        flt(P+i*16,1);flt(P+i*16+4,0);flt(P+i*16+8,0);flt(P+i*16+12,0);
        X_M16(L+i*8)=0;X_M16(L+i*8+2)=3;put(L+i*8+4,i*3);
        put(R+i*24,0x80000000u|i);put(R+i*24+4,0);
        put(R+i*24+8,i);put(R+i*24+12,0);
        put(R+i*24+16,0x60000000u|i);put(R+i*24+20,0);
    }
    unsigned nodes2=(variant&(1u<<25))?depth:surfaces;
    for(unsigned i=0;i<nodes2;i++){
        flt(T+i*20,1);flt(T+i*20+4,0);flt(T+i*20+8,0);
        put(T+i*20+12,0x80000000u|(i%surfaces));put(T+i*20+16,i+1<nodes2?i+1:0x80000000u);
    }
    for(unsigned i=0;i<surfaces;i++){
        put(S+i*12,0);put(S+i*12+4,i*4);put(S+i*12+8,0);
        for(unsigned j=0;j<4;j++){
            unsigned edge=i*4+j,next=i*4+(j+1)%4,prev=i*4+(j+3)%4;
            put(E+edge*24,edge);put(E+edge*24+4,next);put(E+edge*24+8,next);
            put(E+edge*24+12,prev);put(E+edge*24+16,i);put(E+edge*24+20,0xffffffffu);
            float size=mode==3?2.f:.25f;
            flt(V+edge*16,0);flt(V+edge*16+4,j<2?-size:size);flt(V+edge*16+8,(j==0||j==3)?-size:size);
        }
    }
    for(unsigned axis=0;axis<3;axis++)for(unsigned sign=0;sign<2;sign++){
        X_M16(0x1eaf30+(axis*2+sign)*4)=(axis+1)%3;
        X_M16(0x1eaf32+(axis*2+sign)*4)=(axis+2)%3;
        memcpy(g_img_base+0x1eaf30+(axis*2+sign)*4,X_G(0x1eaf30+(axis*2+sign)*4),4);
    }
    flt(0x1f0a68,0);
    put(Q,W);X_M16(Q+4)=0;put(Q+8,0);put(Q+0xc,C);flt(Q+0x10,.75f);put(Q+0x14,O);
    put(Q+0x18,depth);for(unsigned i=0;i<depth;i++)put(Q+0x1c+i*4,0x80000000u|i);
    X_M16(Q+0x21c)=0;X_M8(Q+0x21e)=0;flt(Q+0x220,mode==1?10.f:0.f);flt(Q+0x224,0);
    unsigned count=(variant&128)?256:((variant&64)?3:0);
    for(unsigned part=0;part<4;part++){
        put(O+part*0x404,count);for(unsigned i=0;i<256;i++)put(O+part*0x404+4+i*4,i+100);
    }
    for(unsigned i=0;i<8;i++){context.r[i]=0x12340000+i*0x101;context.st[i]=(double)i+.125;}
    context.r[4]=SP+(variant&1);context.r[0]=W;context.r[1]=0;context.r[2]=0;context.r[6]=O;
    put(context.r[4],0xdeadbeef);put(context.r[4]+4,C);flt(context.r[4]+8,.75f);
    context.f_kind=XK_SUB;context.f_bits=32;context.f_cf_override=0;context.f_of_override=0;
    context.f_cf=1;context.f_of=1;context.fcw=0x37f;context.fsw=(uint16_t)(variant*1777);
    context.fsp=(variant>>8)&7;context.preempt=budget;
    ct_entry=(variant>>11)&3;if(ct_entry==3)ct_entry=0;
    if(ct_entry){context.r[1]=Q;context.r[2]=0;}
    ct_mutation=(variant>>14)&7;ct_site=ct_yields=ct_seen=0;ct_events=2166136261u;
    ct_alias=(variant>>26)&3;ct_projection_checks=ct_projection_accepted=ct_projection_declined=0;
    if(ct_alias){
        ct_entry=1;ct_mutation=0;context.r[1]=Q;context.r[2]=0x80000000u;context.r[4]=SP;
        unsigned input=SP-16;
        if(ct_alias==2){original_pages[C>>12]=original_pages[input>>12];input=C+(input&4095u);}
        if(ct_alias==3)input=C+0xffe;
        const float values[3]={.125f,.25f,.5f};x_guest_write_pages(input,values,sizeof values);put(Q+0xc,input);
        put(SP,0xdeadbeef);
    }
    /* Independent exact bit-pattern axis/FP fixtures. Normally enter a real
     * leaf (and its real visitor); bit24 instead exercises full 88110. */
    unsigned profile=variant>>28;
    if(profile){
        static const unsigned normals[15][3]={
            {0x3f800000,0x40000000,0x3f000000}, /* Y */
            {0x3f800000,0x3f000000,0x40000000}, /* Z */
            {0x40000000,0x40000000,0x40000000}, /* ties Z */
            {0x40000000,0x40000000,0x3f000000}, /* tie Y */
            {0xc0000000,0x3f000000,0x3e800000}, /* negative X */
            {0x3e800000,0xc0000000,0x3f000000}, /* negative Y */
            {0x3e800000,0x3f000000,0xc0000000}, /* negative Z */
            {0,0x80000000,0},                  /* signed zeros */
            {1,0x80000001,0x007fffff},         /* subnormal decline */
            {0x7f7fffff,0x3f000000,0x3e800000}, /* finite overflow stores */
            {0x7fc12345,0x3f800000,0},         /* QNaN decline */
            {0x7f812345,0x3f800000,0},         /* SNaN decline */
            {0x7f800000,0x3f800000,0},         /* infinity decline */
            {0x3f123456,0xbf345678,0x3f56789a}, /* rounded fractions */
            {0x00800000,0x80800000,0x00800001}  /* minimum normals */
        };
        ct_alias=0;ct_mutation=0;context.r[4]=SP;
        for(unsigned i=0;i<depth;i++)for(unsigned j=0;j<3;j++){
            unsigned word=normals[profile-1][j];
            /* Exercise opposite-sign magnitude ties and both zero signs
             * across incoming TOP variants, without changing magnitudes. */
            if((context.fsp&1u)&&j!=1)word^=0x80000000u;
            put(P+i*16+j*4,word);
        }
        put(C,0x3df12345);put(C+4,0xbe234567);put(C+8,0x3e456789);
        for(unsigned i=0;i<nodes2;i++){
            put(T+i*20,normals[profile-1][0]);put(T+i*20+4,normals[profile-1][1]);
            put(T+i*20+8,0x3dfedcba);
        }
        if(!(variant&(1u<<24))){
            ct_entry=1;context.r[1]=Q;context.r[2]=0x80000000u;
            /* All three reference encodings match. High index bits disappear
             * under the original 32-bit <<4, so these are valid same-plane
             * addresses and expose emitted orientation for both sign bits. */
            put(Q+0x18,depth*3);
            for(unsigned i=0;i<depth;i++){
                put(Q+0x1c+i*12,0x80000000u|i);put(Q+0x20+i*12,i);put(Q+0x24+i*12,0x60000000u|i);
            }
        }
        else ct_entry=0;
        context.f_cf=(variant>>8)&1;context.f_of=(variant>>9)&1;
        context.f_cf_override=(variant>>10)&1;context.f_of_override=(variant>>8)&1;
        put(SP,0xdeadbeef);put(SP+4,C);flt(SP+8,.75f);
        if(variant&(1u<<22))for(unsigned i=0;i<12;i++)X_M16(0x1eaf30+i*2)=i&1?3:0xffffu;
    }
}
void arm_original(void)
{if(ct_entry==0)original_00088110(&context);else if(ct_entry==1)original_00087EA0(&context);else original_00087E10(&context);}
void arm_candidate(void)
{if(ct_entry==0)candidate_00088110(&context);else if(ct_entry==1)candidate_00087EA0(&context);else candidate_00087E10(&context);}
void test_boot(void){}
#ifndef TEST_ARM
int main(void)
{
    g_xram=malloc(ARENA);
    uint8_t *before=malloc(ARENA),*expected=malloc(ARENA);unsigned seen=0,total_yields=0;
    for(unsigned k=0;k<CT_CASES;k++){
        unsigned depth=(unsigned[]){1,3,8,16}[k%4],variant=k*104729u,budget=(k%3)?1:100000;
        if(k==1){depth=16;variant=(5u<<14)|4;budget=1;}
        if(k>=2&&k<8){depth=3;variant=(1u+(k-2)%3)<<26;budget=(k&1)?1:100000;}
        if(k>=8&&k<38){depth=3;variant=((1u+(k-8)%15)<<28)|((k>=23)?1u<<24:0);budget=(k&1)?1:100000;}
        if(k==38){depth=3;variant=(7u<<14)|8;budget=1;}
        if(k==39){depth=3;variant=(1u<<28)|(1u<<22);budget=100000;}
        if(getenv("CT_TRACE"))fprintf(stderr,"case%u d%u v%u b%u\n",k,depth,variant,budget);
        arm_prepare(depth,variant,budget);xctx initial=context;memcpy(before,g_xram,ARENA);
        uint32_t pages[PAGES];memcpy(pages,original_pages,sizeof pages);
        feclearexcept(FE_ALL_EXCEPT);arm_original();int fp=fetestexcept(FE_ALL_EXCEPT);
        xctx expected_context=context;memcpy(expected,g_xram,ARENA);
        unsigned ey=ct_yields,eh=ct_events,es=ct_seen,which=g_xpt==alternate_pages;
        uint32_t after[2][PAGES];memcpy(after[0],original_pages,sizeof pages);memcpy(after[1],alternate_pages,sizeof pages);
        context=initial;memcpy(g_xram,before,ARENA);memcpy(original_pages,pages,sizeof pages);
        memset(alternate_pages,0,sizeof alternate_pages);g_xpt=original_pages;ct_site=ct_yields=ct_seen=0;ct_events=2166136261u;
        feclearexcept(FE_ALL_EXCEPT);arm_candidate();int cfp=fetestexcept(FE_ALL_EXCEPT);
#ifdef CT_ALIAS_PROBES
        if(xv_collision_traversal_enabled()){
            if(ct_alias)assert(ct_projection_checks);
            if(k>=8&&k<23){
                unsigned profile=variant>>28;
                if(profile==9||profile==11||profile==12||profile==13)assert(ct_projection_declined&&!ct_projection_accepted);
                else assert(ct_projection_accepted);
            }
        }
#endif
        if(memcmp(&context,&expected_context,sizeof context)){
            fprintf(stderr,"context case%u\n",k);for(unsigned i=0;i<sizeof context;i++)if(((uint8_t*)&context)[i]!=((uint8_t*)&expected_context)[i])fprintf(stderr,"byte%u expected%02x got%02x\n",i,((uint8_t*)&expected_context)[i],((uint8_t*)&context)[i]);abort();}
        if(memcmp(g_xram,expected,ARENA)){for(unsigned i=0;i<ARENA;i++)if(g_xram[i]!=expected[i]){fprintf(stderr,"memory case%u at%x expected%02x got%02x\n",k,i,expected[i],g_xram[i]);break;}abort();}
        assert(!memcmp(original_pages,after[0],sizeof pages)&&!memcmp(alternate_pages,after[1],sizeof pages));
        assert((g_xpt==alternate_pages)==which);assert(ct_yields==ey&&ct_events==eh&&ct_seen==es);assert(fp==cfp);
        seen|=es;total_yields+=ey;
    }
    printf("PASS %u complete query/traversal comparisons; yields=%u sites=%x startup=%d\n",CT_CASES,total_yields,seen,xv_collision_traversal_enabled());
    assert(seen==31);
    free(before);free(expected);free(g_xram);return 0;
}
#endif
