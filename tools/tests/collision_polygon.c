#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
__thread uint32_t *xv_host_page_table;
void reference(xctx*); void candidate(xctx*);
#define SIZE (4u<<20)
static unsigned seed=73,scenario,preempts,packet;
static uint64_t trace;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void hash(const void*v,size_t n){const uint8_t*p=v;while(n--)trace=(trace^*p++)*1099511628211ull;}
void x_guest_read_pages(void*out,uint32_t a,size_t n){uint8_t*p=out;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);p+=k;a+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void*in,size_t n){const uint8_t*p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);p+=k;a+=k;n-=k;}}
void xv_preempt(xctx*c){
    hash(c,sizeof *c);preempts++;assert(preempts<100);
    /* Change the active mapping at a scheduling boundary, preserving its bytes. */
    if((scenario&3)==0){unsigned page=c->r[1]>>12,off=g_xpt[page];
        unsigned spare=SIZE-4096;if(off==spare)spare=SIZE-8192;
        memcpy(g_xram+spare,g_xram+off,4096);g_xpt[page]=spare;}
    if((scenario&7)==2){
        c->fsp=(c->fsp+3)&7; c->fsw^=0x4500;
        for(unsigned j=0;j<8;j++)c->st[j]=(scenario+j+preempts)*0.125;
    }
    c->preempt=1;
}
static void putf(unsigned a,float f){x_guest_write(a,&f,4);}
int main(int argc,char**argv){
 unsigned cases=argc>1?strtoul(argv[1],0,0):1024;
 g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=malloc(4*(1u<<20));xv_host_page_table=g_xpt;
 uint8_t*before=malloc(SIZE),*expected=malloc(SIZE);uint32_t*pt=malloc(4*(1u<<20)),*ept=malloc(4*(1u<<20));
 assert(g_xram&&g_xpt&&before&&expected&&pt&&ept);
 for(scenario=0;scenario<cases;scenario++){
  memset(g_xram,0,SIZE);for(unsigned j=0;j<(1u<<20);j++)g_xpt[j]=((j*17)%1009)*4096;
  xctx init={0};for(unsigned j=0;j<8;j++){init.r[j]=rnd();init.st[j]=(int32_t)rnd()*0.25;}
  init.r[4]=0x800ee000;init.fsp=scenario&7;init.fsw=rnd();init.fcw=0x37f;init.preempt=scenario&1?100:0;
  init.f_kind=XK_SUB;init.f_op1=rnd();init.f_op2=rnd();init.f_res=rnd();init.f_bits=32;
  init.f_cf_override=scenario&1;init.f_cf=1;init.f_of_override=(scenario>>1)&1;init.f_of=1;
  packet=0x80020000+(scenario%4)*4;unsigned plane=0x80011000,points=0x80012ff8;
  const int counts[]={0,1,254,255,256,257,-1};int count=counts[scenario%7];
  unsigned record=packet+0x4408+104*count;
  if(scenario%5==0)plane=record+12;
  if(scenario%11==0)points=record+40;
  unsigned nargs=scenario%10; /* includes empty and above-game-limit input (9) */
  for(unsigned j=0;j<30;j++)putf(points+j*4,(int32_t)(rnd()%20001)-10000);
  float norm[4]={0,0,0,3};norm[scenario%3]=(scenario&8)?-1:1;
  if(scenario%13==0)norm[0]=norm[1]=norm[2]=0.5;
  if(scenario%17==0)norm[0]=NAN;
  if(scenario%19==0)norm[1]=INFINITY;
  x_guest_write(plane,norm,sizeof norm);
  X_M16(packet+4)=(uint16_t)count;
  putf(0x1F0A68,0);
  const uint16_t axes[]={1,2,2,1,2,0,0,2,0,1,1,0};x_guest_write(0x1EAF30,axes,sizeof axes);
  float depth=(scenario%3)?0:0.25;unsigned depthbits;memcpy(&depthbits,&depth,4);
  unsigned args[]={0x12345678,nargs,points,plane,depthbits,0x3f800000,rnd(),rnd(),rnd(),rnd(),rnd(),packet};
  x_guest_write(init.r[4],args,sizeof args);
  memcpy(before,g_xram,SIZE);memcpy(pt,g_xpt,4*(1u<<20));xctx ref=init;
  preempts=0;trace=1469598103934665603ull;reference(&ref);unsigned rp=preempts;uint64_t rt=trace;
  memcpy(expected,g_xram,SIZE);memcpy(ept,g_xpt,4*(1u<<20));memcpy(g_xram,before,SIZE);memcpy(g_xpt,pt,4*(1u<<20));
  xctx got=init;preempts=0;trace=1469598103934665603ull;candidate(&got);
  if(memcmp(&ref,&got,sizeof ref)||memcmp(expected,g_xram,SIZE)||memcmp(ept,g_xpt,4*(1u<<20))||rp!=preempts||rt!=trace){
   fprintf(stderr,"FAIL case %u ctx %d memory %d preempts %u/%u\n",scenario,memcmp(&ref,&got,sizeof ref)!=0,memcmp(expected,g_xram,SIZE)!=0,rp,preempts);
   free(ept);free(pt);free(expected);free(before);free(g_xpt);free(g_xram);return 1;}
 }
 printf("PASS %u cases: full context, arena, page table, preemption observations\n",cases);
 free(ept);free(pt);free(expected);free(before);free(g_xpt);free(g_xram);return 0;
}
