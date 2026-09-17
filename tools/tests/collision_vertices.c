#include "kernel/xk_collision_vertices.h"
#include <stddef.h>
#include <stdlib.h>
#ifndef TEST_ARM
#include <assert.h>
#include <stdio.h>
#endif
enum { ARENA=1<<20,PAGES=ARENA/4096,QUERY=0x10000,WORLD=0x11000,
       EDGES=0x12000,VERTICES=0x16000,SURFACE=0x19000,PACKET=0x20000 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
static xctx context;
xctx *const arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),
 offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned vertex_yields,vertex_events,vertex_mutation,vertex_fp_block;
void original_collision_vertices(xctx *);
void candidate_collision_vertices(xctx *);
void original_full_vertices(xctx *);
void candidate_full_vertices(xctx *);
#ifdef TEST_ARM
void abort(void){__builtin_trap();}
#endif
static void put(uint32_t a,uint32_t v){X_M32(a)=v;}
static void flt(uint32_t a,float v){X_MF32(a)=v;}
void x_guest_read_pages(void *dst,uint32_t a,size_t n)
{uint8_t *d=dst;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(d,X_G(a),k);d+=k;a+=(uint32_t)k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void *src,size_t n)
{const uint8_t *s=src;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),s,k);s+=k;a+=(uint32_t)k;n-=k;}}
static void hash(const void *data,unsigned n)
{const unsigned char *p=data;for(unsigned i=0;i<n;i++)vertex_events=(vertex_events^p[i])*16777619u;}
void f_000B0CB0(xctx *c)
{
    hash(c,sizeof *c);
    for(unsigned i=0;i<12;i++){uint8_t v=X_M8(c->r[4]+i);hash(&v,1);}
    c->r[0]=0x12340000u|(vertex_mutation&1u);c->r[1]=0x2345;c->r[2]=0x3456;
    c->st[(c->fsp+7)&7]=9.125;c->r[4]+=8;
}
void __wrap_xv_preempt(xctx *c)
{
    if(vertex_yields>100000)abort();
    vertex_yields++;hash(c,sizeof *c);
    for(unsigned i=0;i<0x48;i++){uint8_t v=X_M8(c->r[4]+i);hash(&v,1);}
    c->preempt=19;
    if(vertex_yields!=1)return;
    switch(vertex_mutation){
    case 1:c->r[1]=0;break; /* already-selected target must not be re-tested */
    case 2:{uint32_t old=c->r[4];for(unsigned i=0;i<0x48;i++)X_M8(0x7000+i)=X_M8(old+i);c->r[4]=0x7000;break;}
    case 3:put(PACKET+0x808,3);break;
    case 4:flt(VERTICES+16,7.5f);break;
    case 5:{unsigned page=VERTICES>>12,old=g_xpt[page];memcpy(g_xram+0x90000,g_xram+old,4096);g_xpt[page]=0x90000;break;}
    case 6:c->fsp=(c->fsp+3)&7;c->fsw^=0x6900;c->st[c->fsp]=11.25;break;
    case 7:{uint32_t q=c->r[6];for(unsigned i=0;i<0x20;i++)X_M8(0x1b000+i)=X_M8(q+i);c->r[6]=0x1b000;break;}
    case 8:vertex_fp_block=1;break;
    }
}
void arm_prepare(unsigned n,unsigned variant,unsigned budget)
{
    xv_collision_vertices_init();xv_collision_vertices_override(1);(void)xv_collision_vertices_calls();
    for(unsigned i=0;i<PAGES;i++)g_xpt[i]=(i^1)*4096;
    memset(g_xram,0x33,ARENA);memset(&context,0xa5,sizeof context);
    uint32_t sp=0x6000+(variant&3),center=0x18000+(variant&3);
    if(variant&4)center=sp+0x14; /* source aliases destination scratch */
    if(variant&8)center=sp+0x2c; /* source observes preceding pointer spill */
    unsigned mode=(variant>>4)&7;
    uint32_t vertices=VERTICES+((variant&128)?0xff4:0)+(variant&3);
    uint32_t packet=PACKET+((variant&256)?0xfec:0)+(variant&3);
    put(QUERY,WORLD);put(WORLD+0x4c,EDGES);put(WORLD+0x58,vertices);
    put(QUERY+0xc,center);flt(QUERY+0x10,mode==1?-2:2);put(QUERY+0x14,packet);
    put(SURFACE+4,0);
    flt(center,0);flt(center+4,0);flt(center+8,0);
    static const unsigned counts[]={0,1,3,16,128,255,256,257};
    unsigned count=counts[(variant>>9)&7];
    put(packet+0x808,count);
    for(unsigned i=0;i<count;i++)put(packet+0x80c+4*i,0x100+i);
    if(mode==2&&count)put(packet+0x80c,0);
    if(mode==3&&count)put(packet+0x80c+(count-1)*4,0);
    for(unsigned i=0;i<n;i++){
        uint32_t e=EDGES+i*24;unsigned side=(i&1);
        put(e+side*4,i);put(e+(1-side)*4,i);
        put(e+8+side*4,(i+1)%n);put(e+8+(1-side)*4,(i+1)%n);
        put(e+0x14,side?23:24);
        flt(vertices+i*16,mode==1?20:(float)(i%4)*.25f);
        flt(vertices+i*16+4,(float)(i%3)*.25f);flt(vertices+i*16+8,(float)(i%2)*.25f);
    }
    static const uint32_t odd[]={0x7fc00123,0x7f800000,0x00000001,0x80000000,0xffc00456,0x7f7fffff};
    if(mode>=4){put(vertices,odd[(variant>>12)%6]);put(center+4,odd[((variant>>12)+1)%6]);}
    context.r[0]=0x76543210;context.r[1]=0x12345678;context.r[2]=23;
    context.r[3]=0;context.r[4]=sp;context.r[5]=SURFACE;context.r[6]=QUERY;context.r[7]=0;
    context.f_kind=XK_SUB;context.f_bits=32;context.f_cf_override=0;context.f_of_override=0;
    context.f_cf=1;context.f_of=1;context.fsp=(variant>>15)&7;
    context.fcw=0x37f;context.fsw=(uint16_t)(variant*1777);context.preempt=budget;
    for(unsigned i=0;i<8;i++)context.st[i]=(double)i+.125;
    put(sp+0x20,SURFACE);put(sp+0x40,QUERY);put(sp+0x44,23);
    vertex_mutation=(variant>>18)&15;vertex_yields=0;vertex_events=2166136261u;vertex_fp_block=!!(variant&(1u<<25));
}
void arm_original(void){original_collision_vertices(&context);}
void arm_candidate(void){candidate_collision_vertices(&context);}
void arm_off(void){xv_collision_vertices_override(0);}
void test_boot(void){}
#ifndef TEST_ARM
int main(void)
{
    g_xram=malloc(ARENA);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    uint8_t *before=malloc(ARENA),*expected=malloc(ARENA);
    uint32_t pages[PAGES],expected_pages[PAGES];unsigned cases=0,yields=0;
    for(unsigned k=0;k<2048;k++){
        unsigned n=(unsigned[]){1,3,4,8,16,32}[k%6];
        unsigned v=k*104729u;unsigned budget=(unsigned[]){1,2,13,100000}[k%4];
        if(k<2){n=8;v=(8u<<18)|(k?(4u<<9):0);budget=1;}
        if(k==2){n=8;v=1u<<25;budget=100000;}
        if(getenv("VERTEX_CASE_TRACE"))fprintf(stderr,"case %u n%u v%u b%u\n",k,n,v,budget);
        arm_prepare(n,v,budget);unsigned initial_fp=vertex_fp_block;xctx initial=context;memcpy(before,g_xram,ARENA);memcpy(pages,g_xpt,sizeof pages);
        arm_original();xctx expected_context=context;memcpy(expected,g_xram,ARENA);memcpy(expected_pages,g_xpt,sizeof pages);
        unsigned ey=vertex_yields,eh=vertex_events;
        context=initial;memcpy(g_xram,before,ARENA);memcpy(g_xpt,pages,sizeof pages);vertex_yields=0;vertex_events=2166136261u;vertex_fp_block=initial_fp;
        arm_candidate();
#ifdef COLLISION_VERTICES_FP_MODEL
        assert(xv_collision_vertices_calls()==!initial_fp);
#else
        assert(xv_collision_vertices_calls()==1);
#endif
        if(memcmp(&context,&expected_context,sizeof context)){
            fprintf(stderr,"context case %u\n",k);for(unsigned i=0;i<sizeof context;i++)if(((uint8_t*)&context)[i]!=((uint8_t*)&expected_context)[i])fprintf(stderr,"byte%u expected%02x got%02x\n",i,((uint8_t*)&expected_context)[i],((uint8_t*)&context)[i]);abort();}
        assert(!memcmp(g_xram,expected,ARENA));assert(!memcmp(g_xpt,expected_pages,sizeof pages));assert(vertex_yields==ey&&vertex_events==eh);
        cases++;yields+=ey;
    }
    printf("PASS %u full context/memory/mapping comparisons and %u matching yield observations\n",cases,yields);
    for(unsigned k=0;k<128;k++){
        if(getenv("VERTEX_CASE_TRACE"))fprintf(stderr,"full case %u\n",k);
        unsigned n=(unsigned[]){3,4,8,16}[k%4],v=(k*99991u)&0x3ffffu;
        arm_prepare(n,v,100000);vertex_mutation=k&1;
        context.r[4]+=0x3c;put(WORLD+0x40,SURFACE-23*12);
        X_M16(QUERY+0x21c)=0;X_M8(QUERY+0x21e)=0;
        X_M16(0x1eaf30)=0;X_M16(0x1eaf32)=1;flt(0x1f0a68,0);
        flt(QUERY+0x220,0);flt(QUERY+0x224,0);
        unsigned packet=X_M32(QUERY+0x14);
        for(unsigned j=0;j<256;j++){put(packet+4+j*4,0x100+j);put(packet+0x408+j*4,0x200+j);}
        put(packet,k%5?0:256);put(packet+0x404,k%7?0:256);
        if(k&2)xv_collision_vertices_override(0);
        xctx initial=context;memcpy(before,g_xram,ARENA);memcpy(pages,g_xpt,sizeof pages);
        original_full_vertices(&context);xctx expected_context=context;
        memcpy(expected,g_xram,ARENA);memcpy(expected_pages,g_xpt,sizeof pages);
        unsigned ey=vertex_yields,eh=vertex_events;
        context=initial;memcpy(g_xram,before,ARENA);memcpy(g_xpt,pages,sizeof pages);vertex_yields=0;vertex_events=2166136261u;
        candidate_full_vertices(&context);
        assert(xv_collision_vertices_calls()==!(k&2));
        assert(!memcmp(&context,&expected_context,sizeof context));assert(!memcmp(g_xram,expected,ARENA));
        assert(!memcmp(g_xpt,expected_pages,sizeof pages));assert(vertex_yields==ey&&vertex_events==eh);
    }
    printf("PASS 128 full86F50 ON/OFF integration comparisons, including original later paths and edge-call observations\n");
    free(g_xram);free(g_xpt);free(before);free(expected);return 0;
}
#endif
