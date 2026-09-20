/* Full guest context/memory oracle. Admission is extracted from production;
 * locks model the original caller-held depth. Concurrency needs its own test. */
#include "kernel/xk_object_jobs.h"
#include "kernel/xk_clip_region.h"
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WORKERS 2
#define STACK_BYTES (256*1024u)
#define SIZE (2u*1024*1024)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const char xv_object_job_marker=0;
volatile uint32_t xv_cur_fn;int xv_watch_n,xv_trace_funcs;
static xctx contexts[2];
static uint32_t stacks[2]={0x80000,0xc0000},stack_pages[2][64];
static unsigned math_depth[2],math_private_enabled=1;
static int math_private_override=-1;
static unsigned clip_private_attempts[2],clip_private_released[2];
int xv_object_math_lock(void){math_depth[0]++;return 2;}
void xv_object_math_unlock(int *token){if(!*token)return;assert(*token==2&&math_depth[0]);math_depth[0]--;}
#include "admission.h"
int xv_clip_region_begin(void){return 1;}
void xv_clip_region_end(const xv_clip_region_work*w){(void)w;}
void xv_clip_region_account(void){assert(math_depth[0]);}
void __wrap_xv_preempt(xctx*c){(void)c;abort();}
void f_000B7F10(xctx*);
void full_held(xctx*),full_candidate(xctx*);
static void fp(uint32_t a,float f){x_guest_write(a,&f,4);}
int main(void){
 g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1u<<20,4);
 unsigned char *initial=malloc(SIZE),*expected=malloc(SIZE);assert(g_xram&&g_xpt&&initial&&expected);
 for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=i*4096;
 for(unsigned l=0;l<2;l++)for(unsigned i=0;i<64;i++)stack_pages[l][i]=stacks[l]+i*4096;
 unsigned released=0;const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 for(unsigned trial=0;trial<512;trial++){
  memset(g_xram,0xa5,SIZE);assert(!fesetround(modes[trial%4]));
  unsigned n=3+trial%60,edges=3+trial%5;
  uint32_t input=0x81000,output=0x82000,clip=0x83000,sp=0xb8000;
  if(trial%8==1)output=input;
  if(trial%8==2)input=0x11000;
  if(trial%8==3)output=0x12000;
  for(unsigned i=0;i<n;i++){double angle=6.283185307179586*i/n;fp(input+i*8,cos(angle)*(trial%3+.25));fp(input+i*8+4,sin(angle)*(trial%3+.25));}
  for(unsigned i=0;i<edges;i++){double angle=(trial&1?1:-1)*6.283185307179586*i/edges;fp(clip+i*8,cos(angle));fp(clip+i*8+4,sin(angle));}
  fp(0x1f0a68,0);fp(0x1f0a78,1);double threshold=9.999999747378752e-05;x_guest_write(0x1f0af8,&threshold,8);
  X_M32(sp)=0x12345678;X_M32(sp+4)=edges;X_M32(sp+8)=clip;X_M32(sp+12)=64;X_M32(sp+16)=output;fp(sp+20,.0001);
  xctx start={0};start.fiber=(void*)&xv_object_job_marker;start.r[4]=sp;start.r[1]=n;start.r[2]=input;start.preempt=1000000;start.fcw=0x37f;start.fsp=trial%8;
  for(unsigned i=0;i<8;i++)start.st[i]=i+.25;
  memcpy(initial,g_xram,SIZE);xctx reference=start;feclearexcept(FE_ALL_EXCEPT);f_000B7F10(&reference);memcpy(expected,g_xram,SIZE);
  memcpy(g_xram,initial,SIZE);contexts[0]=start;unsigned outer=trial%8==4;math_depth[0]=outer;
  feclearexcept(FE_ALL_EXCEPT);full_held(&contexts[0]);
  int exceptions=fetestexcept(FE_ALL_EXCEPT);
  if(memcmp(&reference,&contexts[0],sizeof reference)||memcmp(expected,g_xram,SIZE)){
   fprintf(stderr,"held mismatch trial %u\n",trial);
   for(unsigned k=0;k<sizeof reference;k++)if(((unsigned char*)&reference)[k]!=((unsigned char*)&contexts[0])[k]){fprintf(stderr,"ctx byte %u %02x/%02x\n",k,((unsigned char*)&reference)[k],((unsigned char*)&contexts[0])[k]);break;}
   for(unsigned k=0;k<SIZE;k++)if(expected[k]!=g_xram[k]){fprintf(stderr,"mem %x %02x/%02x\n",k,expected[k],g_xram[k]);break;}
   abort();
  }
  memcpy(g_xram,initial,SIZE);contexts[0]=start;math_depth[0]=outer;
  unsigned before=clip_private_released[0];feclearexcept(FE_ALL_EXCEPT);full_candidate(&contexts[0]);
  assert(math_depth[0]==outer);assert(exceptions==fetestexcept(FE_ALL_EXCEPT));
  if(memcmp(&reference,&contexts[0],sizeof reference)||memcmp(expected,g_xram,SIZE)){fprintf(stderr,"private clip mismatch trial %u\n",trial);abort();}
  if(outer)assert(clip_private_released[0]==before);
  released+=clip_private_released[0]-before;
 }
 assert(released);printf("PASS: 512 owned-image context/memory/FP cases; %u inner clips released\n",released);
 free(g_xram);free(g_xpt);free(initial);free(expected);
}
