#define _POSIX_C_SOURCE 200809L
#include "xv_x86rt.h"
#include "kernel/xk_polygon_edge.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
void f_000B77C0(xctx *);int xv_math_polygon_edge(xctx *);
enum {SIZE=2<<20,MAXTRACE=80};
static xctx traces[MAXTRACE];static unsigned nt,expected_nt,candidate,mutate,case_no;
static uint32_t rng=77123,polygon;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void same(const xctx *a,const xctx *b){if(memcmp(a,b,sizeof *a)){fprintf(stderr,"context case %u\n",case_no);for(unsigned i=0;i<sizeof *a;i++)if(((const unsigned char*)a)[i]!=((const unsigned char*)b)[i])fprintf(stderr,"%u %02x %02x\n",i,((const unsigned char*)a)[i],((const unsigned char*)b)[i]);abort();}}
#ifdef EDGE_MUTATE_HANDOFF
void xd3d_lockstep_preempt(xctx *c){assert(nt<MAXTRACE);if(candidate){assert(nt<expected_nt);same(c,&traces[nt]);}else traces[nt]=*c;nt++;c->preempt=mutate?1:3;if(mutate){
 c->fsp=(c->fsp+3)&7;
 static const uint64_t bits[8]={0x8000000000000000ull,0x7ff0000000004321ull,0x7ff8000000001234ull,0xfff0000000000000ull,0x1ull,0x3ff0000000000000ull,0x400921fb54442d18ull,0ull};
 for(unsigned k=0;k<8;k++)memcpy(&c->st[(c->fsp+k)&7],&bits[(k+nt)&7],8);
 c->r[0]^=0xaaaa5500u;c->r[2]^=0x12340000u;c->r[6]^=0x55667788u;
 if(c->r[3]>3)c->r[3]--;
 if((c->r[1]>>12)==0x18u)c->r[1]+=0x30000u;
 c->r[5]=0x28ffcu+case_no%8;
 if(nt==1){unsigned char saved[32];x_guest_read(saved,c->r[4],32);c->r[4]+=0x1000u;x_guest_write(c->r[4],saved,32);}
 c->r[7]++;
 c->f_kind=XK_EXPLICIT;c->f_op1^=0x12345678u;c->f_op2^=0x89abcdefu;c->f_res^=0x234u;c->f_bits=16;c->f_cf_override^=1;c->f_cf^=1;c->f_of_override^=1;c->f_of^=1;c->fsw^=0x80;c->fcw^=0x100;
 c->df^=1;c->fs_base^=0x123000u;c->scratch^=0x77u;c->eip_hint^=0x5678u;
 for(unsigned k=0;k<8;k++){c->mm[k]^=0xfedcba9876543210ull;for(unsigned j=0;j<4;j++){uint32_t b=0x7f800001u+k*4+j;memcpy(&c->xmm[k][j],&b,4);}}
}}
#else
void xd3d_lockstep_preempt(xctx *c){assert(nt<MAXTRACE);if(candidate){assert(nt<expected_nt);same(c,&traces[nt]);}else traces[nt]=*c;nt++;c->preempt=mutate?1:3;if(mutate){c->fsp=(c->fsp+3)&7;c->st[(c->fsp+6)&7]=3.125;c->f_cf^=1;c->fsw^=0x80;}}
#endif
static void word(uint32_t a,uint32_t v){x_guest_write(a,&v,4);}static void fp(uint32_t a,float v){x_guest_write(a,&v,4);}
int main(void){assert(!xv_native_polygon_edge_available());assert(!xv_math_polygon_edge(NULL));xv_native_polygon_edge_init();assert(!xv_math_polygon_edge(NULL));xv_native_polygon_edge_override(1);g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);uint8_t *initial=malloc(SIZE),*want=malloc(SIZE);assert(g_xram&&g_xpt&&initial&&want);for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=(i^1)*4096;g_xpt[0x48]=g_xpt[0x18];g_xpt[0x49]=g_xpt[0x19];unsigned accepted=0,yields=0;const int rounding[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
for(case_no=0;case_no<4096;case_no++){
 memset(g_xram,0xa5,SIZE);assert(!fesetround(rounding[case_no%4]));xctx a={0};for(unsigned j=0;j<8;j++){a.r[j]=next();a.st[j]=j+.125;a.mm[j]=next();for(unsigned k=0;k<4;k++)a.xmm[j][k]=j+k+.25f;}a.fsp=(case_no/4)%8;a.fsw=next();a.fcw=0x37f|((case_no%4)<<10);a.f_kind=XK_SUB;a.f_op1=next();a.f_op2=next();a.f_res=a.f_op1-a.f_op2;a.f_bits=32;a.f_cf_override=next()&1;a.f_of_override=next()&1;a.f_cf=next()&1;a.f_of=next()&1;a.fs_base=next();a.df=case_no&1;a.eip_hint=next();a.scratch=next();a.preempt=case_no%3?1000:1;
 unsigned sp=0x61ff0+(case_no%4),count=3+case_no%14,point=0x28ffc+case_no%8;polygon=0x18ff0+case_no%8;if(case_no%19==0)point=polygon;if(case_no%23==0)point=0x48ff0+case_no%8;if(case_no%31==0)polygon=sp+8;
 a.r[4]=sp;a.r[1]=polygon;for(unsigned j=0;j<count;j++){float ang=6.283185307f*j/count;fp(polygon+8*j,cosf(ang));fp(polygon+8*j+4,sinf(ang));}fp(point,case_no%7*.4f-1.2f);fp(point+4,case_no%5*.3f-.6f);
 if(case_no%11==0){static const uint32_t edge[]={0,0x80000000,1,0x007fffff,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234};word(polygon+4,edge[(case_no/11)%10]);}
 *(float*)(g_img_base+0x1f0a68)=0;fp(0x1f0a68,0);word(sp,0x12345678);word(sp+4,case_no%29==0?0:case_no%37==0?0xffff:count);word(sp+8,point);fp(sp+12,case_no%9*.2f);
 xctx b=a;memcpy(initial,g_xram,SIZE);candidate=0;nt=0;mutate=case_no%7==0;f_000B77C0(&a);expected_nt=nt;memcpy(want,g_xram,SIZE);memcpy(g_xram,initial,SIZE);candidate=1;nt=0;assert(xv_math_polygon_edge(&b));same(&a,&b);if(memcmp(want,g_xram,SIZE)){fprintf(stderr,"memory case %u\n",case_no);abort();}assert(nt==expected_nt);accepted+=(a.r[0]&255)!=0;yields+=nt;
}assert(xv_math_polygon_edge_calls()==4096);assert(xv_math_polygon_edge_calls()==0);xv_native_polygon_edge_override(0);assert(!xv_math_polygon_edge(NULL));printf("PASS 4096 complete-context/memory fixtures; true %u yields %u; exact preemption snapshots, aliases and four rounding modes\n",accepted,yields);return 0;}
