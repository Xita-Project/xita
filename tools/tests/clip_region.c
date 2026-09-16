#define _POSIX_C_SOURCE 200809L
#include "clip_region_observe.h"
#include "kernel/xk_clip_region.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#ifndef CASES
#define CASES 512
#endif
#define SIZE (2u<<20)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const char xv_object_job_marker=0;
volatile uint32_t xv_cur_fn;int xv_watch_n=0;int xv_trace_funcs;
static unsigned arm,case_no,event_no,event_count,lock_depth,probe_count,clip_count,yields,mutation;
static uint8_t relocated[0x5000];
static xctx *active;
static jmp_buf cut;
static unsigned budget_stopped;
typedef struct {xctx c;uint64_t ram,pages;unsigned type,marker,depth;} Event;
static Event events[4096];
static uint64_t hash(const void*ptr,size_t bytes){const unsigned char*p=ptr;uint64_t h=0;for(size_t i=0;i<bytes;i+=8){uint64_t x=0;memcpy(&x,p+i,bytes-i<8?bytes-i:8);h=(h^x)*0x100000001b3ull;}return h;}
static void same_ctx(const xctx*a,const xctx*b){if(memcmp(a,b,sizeof*a)){fprintf(stderr,"ctx arm%u case%u event%u\n",arm,case_no,event_no);for(unsigned i=0;i<sizeof*a;i++)if(((const unsigned char*)a)[i]!=((const unsigned char*)b)[i])fprintf(stderr," byte%u:%02x != %02x\n",i,((const unsigned char*)a)[i],((const unsigned char*)b)[i]);abort();}}
static void event(unsigned type,xctx*c){if(event_no>=4096){assert(case_no==3895 && !lock_depth);budget_stopped=1;longjmp(cut,1);}Event e={0};e.c=*c;e.ram=hash(g_xram,SIZE);e.pages=hash(g_xpt,512*4);e.type=type;e.marker=xv_cur_fn;e.depth=lock_depth;if(!arm)events[event_no]=e;else{Event*w=&events[event_no];same_ctx(&w->c,c);if(e.ram!=w->ram||e.pages!=w->pages||e.type!=w->type||e.marker!=w->marker||e.depth!=w->depth){fprintf(stderr,"event mismatch arm%u case%u ev%u type%u/%u marker%x/%x RAM%llx/%llx depth%u/%u\n",arm,case_no,event_no,e.type,w->type,e.marker,w->marker,(unsigned long long)e.ram,(unsigned long long)w->ram,e.depth,w->depth);abort();}}event_no++;}
void raw_probe(xctx*);
void f_0001D130(xctx*c){event(1,c);assert(c->r[4]>=0x80000&&c->r[4]<0xc0000&&c->r[0]<=0x2010);probe_count++;raw_probe(c);
#ifndef CLIP_TEST_NO_MARKERS
 xv_cur_fn=0x1D130;
#endif
}
void xv_watch_leave(uint32_t fn,uint32_t back,xctx*c){assert(fn==0x1D130&&(back==0xB71C0||back==0xB7F10));if(xv_watch_n)event(2,c);}

#ifdef PARK_MUTATION
static void park_mutate(xctx*c){
 c->fsp=(c->fsp+5)&7;c->fsw^=0x4300;c->fcw^=0x400;
 c->r[0]^=0xabcd0000;c->r[1]^=0x55000000;c->r[7]^=0x12345678;
 c->f_kind=XK_SUB;c->f_op1=0x1234;c->f_op2=0x8765;c->f_res=c->f_op1-c->f_op2;c->f_bits=32;c->f_cf_override=1;c->f_cf=1;
 static const uint64_t bits[]={0x8000000000000000ull,0x7ff0000000004321ull,0x7ff8000000001234ull,0xfff0000000000000ull,1ull,0x3ff0000000000000ull,0x400921fb54442d18ull,0ull};
 for(unsigned i=0;i<8;i++)memcpy(&c->st[i],&bits[(i+clip_count)&7],8);
 c->scratch^=0x31415926;uint32_t plane=X_M32(c->r[4]+12);X_M32(plane+8)^=0x00010000u;
 event(0x30,c);
}

#endif
int xv_object_math_lock(void){event(3,active);
#ifdef PARK_MUTATION
park_mutate(active);
#endif
lock_depth++;clip_count++;return 1;}
void xv_object_math_unlock(int*token){assert(*token==1&&lock_depth==1);event(4,active);lock_depth--;*token=0;}
void xv_object_math_report_check(void){assert(!lock_depth);}
void __real_x_str_movs(xctx*,unsigned,int);
void __wrap_x_str_movs(xctx*c,unsigned size,int mode){event(0x10+size,c);__real_x_str_movs(c,size,mode);}
void __wrap_xv_preempt(xctx*c){event(5,c);yields++;c->preempt=7;
 if(mutation){c->fsp=(c->fsp+3)&7;static const uint64_t fpbits[]={0x8000000000000000ull,0x7ff0000000004321ull,0x7ff8000000001234ull,0xfff0000000000000ull,1ull,0x3ff0000000000000ull,0x400921fb54442d18ull,0ull};for(unsigned i=0;i<8;i++)memcpy(&c->st[i],&fpbits[(i+yields)&7],8);c->f_cf^=1;c->fsw^=0x80;c->fcw^=0x100;c->r[0]^=0xaaaa0000u;
#ifdef STRONG_MUTATION
 if(yields==1){x_guest_read(relocated,c->r[4]-0x100,sizeof relocated);x_guest_write(c->r[4]+0x20000-0x100,relocated,sizeof relocated);c->r[4]+=0x20000;}
 if(xv_cur_fn==0xB71C0)c->r[2]=0x78ff0;
 if(yields==1){memcpy(g_xram+0x1d0000,g_xram+g_xpt[0x58],4096);g_xpt[0x58]=0x1d0000;}
#endif
 c->scratch^=0x900;c->fs_base^=0x10000;c->eip_hint^=0x333;c->df^=0;for(unsigned i=0;i<8;i++){c->mm[i]^=0xaa55aa55aa55aa55ull;for(unsigned j=0;j<4;j++)c->xmm[i][j]+=.25f;}}
}
static uint32_t rng=89517;static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void word(uint32_t a,uint32_t v){x_guest_write(a,&v,4);}static void fp(uint32_t a,float v){x_guest_write(a,&v,4);}
int main(void){setenv("XV_NATIVE_CLIP","1",1);setenv("XV_CLIP_REGISTERS","1",1);unsetenv("XV_WATCH_ADDR");g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);uint8_t*initial=malloc(SIZE),*want=malloc(SIZE);assert(g_xram&&g_xpt&&initial&&want);xv_native_clip_region_init();xv_native_clip_region_override(1);unsigned total_clips=0,total_yields=0,failed=0,empty=0,nonempty=0,bounded=0;uint32_t before_pages[512],want_pages[512];
 const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 for(case_no=0;case_no<CASES;case_no++){
  assert(!fesetround(modes[case_no%4]));memset(g_xram,0xa5,SIZE);for(unsigned i=0;i<512;i++)g_xpt[i]=(i^1)*4096;
  unsigned n=3+case_no%30,edges=4+(case_no%4),cap=n+16;uint32_t in=0x18ff0+(case_no%4),clip=0x38ff0+(case_no%4),out=0x58ff0+(case_no%4),sp=0x91ff0+(case_no%4)*4;
  if(case_no%23==0)n=0;if(case_no%29==0)n=1;if(case_no%31==0)n=2;if(case_no%47==0)n=128;
  if(case_no%13==0)edges=0;if(case_no%17==0)edges=1;if(case_no%19==0)edges=2;if(case_no%43==0)edges=0xffff;
  if(case_no%7==0)cap=0;else if(case_no%11==0)cap=n-1;
  if(case_no%9==0)out=in;else if(case_no%9==1)out=in+4;else if(case_no%9==2)out=in-4;else if(case_no%9==3){g_xpt[0x58]=g_xpt[0x18];g_xpt[0x59]=g_xpt[0x19];}
  if(case_no%53==0)out=clip;
  if(case_no%59==0)in=sp-0x2020+0x20;
  if(case_no%61==0)out=sp-0x2020+0x14;
  for(unsigned i=0;i<n;i++){double ang=6.283185307179586*i/n;float radius=(case_no%3==0)?0.25f:(case_no%3==1)?1.25f:2.5f;fp(in+i*8,cos(ang)*radius);fp(in+i*8+4,sin(ang)*radius);}
  if(edges!=0xffff)for(unsigned i=0;i<edges;i++){double ang=(case_no&1?1:-1)*6.283185307179586*i/edges;fp(clip+i*8,cos(ang));fp(clip+i*8+4,sin(ang));}
  if(edges>1&&edges!=0xffff&&case_no%5==0){word(clip+8,X_M32(clip));word(clip+12,X_M32(clip+4));}
  if(case_no%37==0&&n){word(in,0);word(in+4,0x80000000);}
  fp(0x1F0A68,0);fp(0x1F0A78,1);double threshold=9.999999747378752e-05;x_guest_write(0x1F0AF8,&threshold,8);
  word(sp,0x12345678);word(sp+4,edges);word(sp+8,clip);word(sp+12,cap);word(sp+16,out);fp(sp+20,case_no%3==0?0:case_no%3==1?.0001f:-.0001f);
  xctx start={0};for(unsigned i=0;i<8;i++){start.r[i]=next();start.st[i]=i+.375;start.mm[i]=next();for(unsigned j=0;j<4;j++)start.xmm[i][j]=i+j+.125f;}start.r[4]=sp;start.r[1]=n;start.r[2]=in;start.fsp=case_no%8;start.fsw=next();start.fcw=0x37f|(case_no%4<<10);start.f_kind=XK_SUB;start.f_op1=next();start.f_op2=next();start.f_res=start.f_op1-start.f_op2;start.f_bits=32;start.f_cf_override=next()&1;start.f_cf=next()&1;start.f_of_override=next()&1;start.f_of=next()&1;start.df=case_no%41==0;start.preempt=case_no%3==0?1:100000;mutation=case_no%3==0;
  memcpy(initial,g_xram,SIZE);memcpy(before_pages,g_xpt,sizeof before_pages);xctx expected;
  unsigned wc=0,wp=0,wy=0,ws=0;
#ifdef START_CASE
 if(case_no<START_CASE)continue;
#endif
  for(arm=0;arm<3;arm++){memcpy(g_xram,initial,SIZE);memcpy(g_xpt,before_pages,sizeof before_pages);xctx *c=malloc(sizeof*c);assert(c);*c=start;active=c;budget_stopped=0;event_no=clip_count=probe_count=yields=lock_depth=0;xv_cur_fn=0xB7F10;
   if(!setjmp(cut)){if(!arm)original_wrapper(c);else if(arm==1)current_wrapper(c);else fused_wrapper(c);}
   else if(arm==2)xv_clip_region_end(NULL); /* Test-only unwind of a deliberately bounded nonterminating reference prefix. */
   assert(!lock_depth);if(!arm){expected=*c;event_count=event_no;ws=budget_stopped;memcpy(want,g_xram,SIZE);memcpy(want_pages,g_xpt,sizeof want_pages);wc=clip_count;wp=probe_count;wy=yields;}else{same_ctx(&expected,c);assert(budget_stopped==ws&&event_no==event_count&&wc==clip_count&&wp==probe_count&&wy==yields);if(memcmp(want,g_xram,SIZE)||memcmp(want_pages,g_xpt,sizeof want_pages)){fprintf(stderr,"final memory case%u arm%u\n",case_no,arm);abort();}}
   free(c);
  }
  if(ws)bounded++;
  total_clips+=wc;total_yields+=wy;int16_t result=expected.r[0];if(ws){}else if(result==-1)failed++;else if(!result)empty++;else nonempty++;
  if(case_no%64==0){printf("case %u clips %u result %d\n",case_no,wc,result);fflush(stdout);}
 }
 printf("PASS %u original/current/fused fixture comparisons; clips%u yields%u failure%u empty%u nonempty%u bounded-prefix%u\n",CASES,total_clips,total_yields,failed,empty,nonempty,bounded);return 0;
}
