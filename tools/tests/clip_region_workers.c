#define _POSIX_C_SOURCE 200809L
/* Include the unmodified production pool so this fixture can deterministically
 * order its two wakeups. Real worker(), execute(), park_worker(), semaphores,
 * service_owner(), recursive/timed mutex branches and stack probes all run. */
#include "../../recomp/kernel/xk_object_jobs.c"
#include "kernel/xk_clip_region.h"
#include "clip_region_observe.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sched.h>

#define T_SIZE (2u<<20)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;int xv_watch_n,xv_trace_funcs,xv_phase_enabled;
static unsigned t_arm,t_mode,t_mutate,t_alloc,t_waiting,t_peer_ready,t_contended,t_services,t_events,t_expected_events;
static _Thread_local xctx *t_active;
static xctx *t_parked;
static xctx t_result,t_expected;
static unsigned char *t_memory;
static pthread_t t_owner;
static unsigned t_clips,t_probes,t_yields,t_expected_clips;
struct t_event {xctx c;uint64_t hash;unsigned type,depth,marker;};
static struct t_event t_history[256];
static uint64_t t_hash(const void *p,size_t n){const unsigned char*s=p;uint64_t h=0;for(size_t i=0;i<n;i++)h=(h^s[i])*0x100000001b3ull;return h;}
static void t_event(unsigned type,xctx*c){
 assert(t_events<256);struct t_event e={0};e.c=*c;e.type=type;e.depth=math_depth[0];e.marker=xv_cur_fn;
 e.hash=t_hash(g_xram+stacks[0],STACK_BYTES)^t_hash(g_xram+0x10000,0x90000);
 if(t_arm==0)t_history[t_events]=e;else if(memcmp(&e,&t_history[t_events],sizeof e)){
   fprintf(stderr,"worker event mismatch arm%u mode%u event%u type%u/%u marker%x/%x depth%u/%u\n",t_arm,t_mode,t_events,e.type,t_history[t_events].type,e.marker,t_history[t_events].marker,e.depth,t_history[t_events].depth);abort();}
 t_events++;
}
uint64_t xk_os_monotonic_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char*f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t align,uint32_t lo,uint32_t hi,int top){(void)align;(void)lo;(void)hi;(void)top;assert(n==STACK_BYTES);return 0x100000+(t_alloc++)*STACK_BYTES;}
int xk_mem_free(uint32_t p){(void)p;return 1;}
int xk_object_io_step(void){abort();}
void raw_probe(xctx*);
void f_0001D130(xctx*c){t_event(1,c);xv_object_job_stack_probe(c);t_probes++;raw_probe(c);
#ifndef CLIP_TEST_NO_MARKERS
 xv_cur_fn=0x1D130;
#endif
}
void xv_watch_leave(uint32_t from,uint32_t back,xctx*c){(void)from;(void)back;t_event(2,c);}
int __real_xv_object_math_lock(void);
void __real_xv_object_math_unlock(int*);
int __real_pthread_mutex_trylock(pthread_mutex_t*);
int __wrap_pthread_mutex_trylock(pthread_mutex_t*p){int r=__real_pthread_mutex_trylock(p);if(r==EBUSY&&t_active)__atomic_store_n(&t_contended,1,__ATOMIC_RELEASE);return r;}
int __wrap_xv_object_math_lock(void){
 assert(t_active);t_event(3,t_active);
 if(!t_clips){
  t_parked=t_active;__atomic_store_n(&t_waiting,1,__ATOMIC_RELEASE);
  while(!__atomic_load_n(&t_peer_ready,__ATOMIC_ACQUIRE))sched_yield();
  if(t_mode!=2)while(!__atomic_load_n(&pause_workers,__ATOMIC_ACQUIRE))sched_yield();
 }
 int token=__real_xv_object_math_lock();t_clips++;t_event(0x30,t_active);return token;
}
void __wrap_xv_object_math_unlock(int*p){t_event(4,t_active);__real_xv_object_math_unlock(p);}
void __real_x_str_movs(xctx*,unsigned,int);
void __wrap_x_str_movs(xctx*c,unsigned n,int mode){t_event(0x10+n,c);__real_x_str_movs(c,n,mode);}
void __wrap_xv_preempt(xctx*c){t_event(5,c);t_yields++;c->preempt=7;c->fsp=(c->fsp+3)&7;c->fsw^=0x80;c->scratch^=0x900;}
static void t_service(xctx*peer){
 assert(pthread_equal(pthread_self(),t_owner));assert(__atomic_load_n(&service_state[0],__ATOMIC_ACQUIRE)==4);
 assert(t_parked==&contexts[0]);assert(math_depth[0]==(t_mode==1?1u:0u));
 if(t_mutate){
 xctx*c=t_parked;c->fsp=(c->fsp+5)&7;c->fsw^=0x4300;c->fcw^=0x400;
 c->r[0]^=0xabcd0000;c->r[1]^=0x55000000;c->r[7]^=0x12345678;
 c->f_kind=XK_SUB;c->f_op1=0x1234;c->f_op2=0x8765;c->f_res=c->f_op1-c->f_op2;c->f_bits=32;c->f_cf_override=1;c->f_cf=1;
 for(unsigned i=0;i<8;i++)c->st[i]=i+.375;c->scratch^=0x31415926;
 X_M32(X_M32(c->r[4]+12)+8)^=0x10000u;
 }
 t_services++;peer->r[4]+=4;
}
void f_0008FB70(xctx*c){
 unsigned sp=c->r[4];
 if(c->r[1]==1){
  assert(c==&contexts[1]);int token=0;if(t_mode==2)token=__real_xv_object_math_lock();
  __atomic_store_n(&t_peer_ready,1,__ATOMIC_RELEASE);
  while(!__atomic_load_n(&t_waiting,__ATOMIC_ACQUIRE))sched_yield();
  if(t_mode==2)while(!__atomic_load_n(&t_contended,__ATOMIC_ACQUIRE))sched_yield();
  X_PUSH32(0x3223Fu);xv_object_job_hle(c,0x184AB0u,t_service);
  if(token)__real_xv_object_math_unlock(&token);
 }else{
  assert(c==&contexts[0]);t_active=c;
  int outer=0;if(t_mode==1)outer=__real_xv_object_math_lock();
  c->r[4]-=32;X_M32(c->r[4])=0x12345678;X_M32(c->r[4]+4)=4;X_M32(c->r[4]+8)=0x30000;
  X_M32(c->r[4]+12)=64;X_M32(c->r[4]+16)=0x50000;X_MF32(c->r[4]+20)=.0001f;
  c->r[1]=16;c->r[2]=0x10000;c->preempt=1;
  xv_cur_fn=0xB7F10;
  if(t_arm==0)original_wrapper(c);else if(t_arm==1)current_wrapper(c);else fused_wrapper(c);
  t_result=*c;
  if(outer)__real_xv_object_math_unlock(&outer);
  t_active=NULL;
 }
 c->r[4]=sp+4;
}
static void t_pass(void){
 /* Equivalent to the pool's join driver, with deterministic lane wake order
  * so all three arms use identical guest stack addresses. No guard/parking or
  * owner-service behavior is replaced. */
 next=0;count=2;owner=(xctx*)1;__atomic_store_n(&running,1,__ATOMIC_RELEASE);
 memset(service_state,0,sizeof service_state);memset(jobs,0,2*sizeof jobs[0]);jobs[1].r[1]=1;
 jobs[0].fsp=3;jobs[0].fcw=0x37f;for(unsigned j=0;j<8;j++)jobs[0].st[j]=j+.125;
 sem_post(&wakes[0]);while(!__atomic_load_n(&t_waiting,__ATOMIC_ACQUIRE))sched_yield();
 sem_post(&wakes[1]);service_owner();
 for(unsigned i=0;i<active_workers;i++)wait_sem(&dones[i]);
 __atomic_store_n(&running,0,__ATOMIC_RELEASE);count=0;owner=NULL;
 assert(!math_depth[0]&&!math_depth[1]&&t_services==1&&t_clips>0&&t_probes>1);
 if(t_mode==2)assert(t_contended);
}
int main(void){
 t_owner=pthread_self();g_xram=malloc(T_SIZE);g_img_base=g_xram;g_xpt=calloc(1<<20,4);t_memory=malloc(T_SIZE);assert(g_xram&&g_xpt&&t_memory);
 for(unsigned i=0;i<T_SIZE/4096;i++)g_xpt[i]=i*4096;
 setenv("XV_OBJECT_JOB_WORKERS","2",1);setenv("XV_OBJECT_LOCK_PROFILE","0",1);
 assert(initialize());xv_native_clip_region_init();xv_native_clip_region_override(1);
 for(t_mutate=0;t_mutate<2;t_mutate++)for(unsigned waits=0;waits<2;waits++)for(t_mode=0;t_mode<4;t_mode++){
  math_fast_path=t_mode!=3;math_wait_override=waits;
  for(t_arm=0;t_arm<3;t_arm++){
   t_waiting=t_peer_ready=t_contended=t_services=t_events=t_clips=t_probes=t_yields=0;t_parked=NULL;
   memset(g_xram,0xa5,T_SIZE);
   for(unsigned n=0;n<16;n++){double a=6.283185307179586*n/16;X_MF32(0x10000+n*8)=cos(a)*.75;X_MF32(0x10004+n*8)=sin(a)*.75;}
   const float edges[8]={-1,-1,-1,1,1,1,1,-1};memcpy(X_G(0x30000),edges,sizeof edges);
   X_MF32(0x1F0A68)=0;X_MF32(0x1F0A78)=1;double tiny=9.999999747378752e-05;x_guest_write(0x1F0AF8,&tiny,8);
   t_pass();
   if(!t_arm){t_expected=t_result;memcpy(t_memory,g_xram,T_SIZE);t_expected_events=t_events;t_expected_clips=t_clips;}
   else {assert(!memcmp(&t_expected,&t_result,sizeof t_result));assert(!memcmp(t_memory,g_xram,T_SIZE));assert(t_events==t_expected_events&&t_clips==t_expected_clips);}
  }
  xv_clip_region_work n;xv_clip_region_read_work(&n);assert(n.regions==1&&n.clips==t_clips&&n.planes>=n.clips&&n.max_clips==n.clips);
  printf("PASS actual workers: mode%u timed%u mutate%u clips%u events%u; parked owner mutation/full context/memory\n",t_mode,waits,t_mutate,t_clips,t_events);
 }
 xv_native_clip_region_override(-1);xv_object_jobs_shutdown();free(t_memory);free(g_xpt);free(g_xram);return 0;
}
