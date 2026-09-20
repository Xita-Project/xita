#define _POSIX_C_SOURCE 200809L
/* Actual pool, guarded helper, owner service, semaphores and private mappings. */
#include "../../recomp/kernel/xk_object_jobs.c"
#include "kernel/xk_clip_region.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <sched.h>
#define SIZE (2u*1024*1024)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;int xv_watch_n,xv_trace_funcs,xv_phase_enabled;
static unsigned allocs,arrived,mode,services;
static xctx results[2],expected[2];
static pthread_t main_thread;
uint64_t xk_os_monotonic_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char*f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t lo,uint32_t hi,int top){(void)a;(void)lo;(void)hi;(void)top;assert(n==STACK_BYTES);return 0x80000+(allocs++)*STACK_BYTES;}
int xk_mem_free(uint32_t p){(void)p;return 1;}
int xk_object_io_step(void){abort();}
void full_candidate(xctx*),full_held(xctx*);
int xv_clip_region_begin(void){return 1;}
void xv_clip_region_end(const xv_clip_region_work*w){(void)w;}
void xv_clip_region_account(void){int lane=worker_lane();assert(lane>=0&&math_depth[lane]);}
void __wrap_xv_preempt(xctx*c){(void)c;abort();}
static void service(xctx*c){assert(pthread_equal(pthread_self(),main_thread));services++;c->r[4]+=4;}
void f_0008FB70(xctx*c){
 int lane=worker_lane();assert(lane>=0&&lane<2);uint32_t base=stacks[lane],entry=c->r[4];
 __atomic_fetch_add(&arrived,1,__ATOMIC_RELEASE);
 while(__atomic_load_n(&arrived,__ATOMIC_ACQUIRE)!=2)sched_yield();
 for(unsigned iteration=0;iteration<32;iteration++){
  if(lane==1&&iteration==8){X_PUSH32(0x3223fu);xv_object_job_hle(c,0x184ab0u,service);}
  uint32_t input=base+0x1000,output=base+0x2000,clip=base+0x3000,sp=base+0x38000;
  for(unsigned i=0;i<16;i++){double angle=6.283185307179586*i/16;X_MF32(input+i*8)=cos(angle)*.75;X_MF32(input+i*8+4)=sin(angle)*.75;}
  const float square[8]={-1,-1,-1,1,1,1,1,-1};memcpy(X_G(clip),square,sizeof square);
  c->r[4]=sp;c->r[1]=16;c->r[2]=input;c->preempt=1000000;c->df=0;
  X_M32(sp)=0x12345678;X_M32(sp+4)=4;X_M32(sp+8)=clip;X_M32(sp+12)=64;X_M32(sp+16)=output;X_MF32(sp+20)=.0001f;
  if(mode)full_candidate(c);else full_held(c);
  assert(!math_depth[lane]);
 }
 results[lane]=*c;c->r[4]=entry+4;
}
int main(void){
 main_thread=pthread_self();g_xram=malloc(SIZE);g_img_base=g_xram;g_xpt=calloc(1u<<20,4);unsigned char *memory=malloc(SIZE);
 for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=4096*i;
 setenv("XV_OBJECT_JOB_WORKERS","2",1);setenv("XV_OBJECT_LOCK_PROFILE","0",1);assert(initialize());
 for(mode=0;mode<2;mode++){
  memset(g_xram,0xa5,SIZE);X_MF32(0x1f0a68)=0;X_MF32(0x1f0a78)=1;double threshold=9.999999747378752e-05;x_guest_write(0x1f0af8,&threshold,8);
  arrived=services=next=0;count=2;owner=(xctx*)1;memset(jobs,0,2*sizeof jobs[0]);memset(service_state,0,sizeof service_state);__atomic_store_n(&running,1,__ATOMIC_RELEASE);
  for(unsigned i=0;i<2;i++)sem_post(&wakes[i]);service_owner();for(unsigned i=0;i<2;i++)wait_sem(&dones[i]);
  __atomic_store_n(&running,0,__ATOMIC_RELEASE);count=0;owner=NULL;assert(services==1);
  if(!mode){memcpy(expected,results,sizeof results);memcpy(memory,g_xram,SIZE);}
  else{assert(!memcmp(expected,results,sizeof results));assert(!memcmp(memory,g_xram,SIZE));}
 }
 assert(clip_private_released[0]&&clip_private_released[1]);
 printf("PASS: production workers, owner service, complete context/memory; released %u/%u clips\n",clip_private_released[0],clip_private_released[1]);
 xv_object_jobs_shutdown();free(memory);free(g_xpt);free(g_xram);
}
