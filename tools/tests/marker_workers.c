#define _POSIX_C_SOURCE 200809L
#include "../../recomp/kernel/xk_object_jobs.c"
#include <assert.h>
#include <sched.h>
#include <stdarg.h>
enum { SIZE=4<<20 };
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocs,arrived,services,accepted[2];
static pthread_t main_thread;
static _Thread_local unsigned poison_source;
uint64_t xk_os_monotonic_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return(uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *f,...){(void)f;}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t lo,uint32_t hi,int top)
{
 (void)a;(void)lo;(void)hi;(void)top;assert(n==STACK_BYTES);
 uint32_t base=0x100000+allocs++*STACK_BYTES;
 for(unsigned i=0;i<n/4096;i++)g_xpt[(base>>12)+i]=base+n-4096-i*4096;
 return base;
}
int xk_mem_free(uint32_t p){(void)p;return 1;}
int xk_object_io_step(void){abort();}
int xv_math_marker_record(xctx *);
void current_marker(xctx *),hooked_marker(xctx *);
void __real_xv_object_math_unlock(int *);
void __wrap_xv_object_math_unlock(int *guard)
{
 int released=*guard>=2;
 __real_xv_object_math_unlock(guard);
 /* Each worker owns a distinct fixture source. Replacing it after capture
  * detects any accidental later read through a borrowed source pointer. */
 if(released&&poison_source){memset(X_G(poison_source),0xcc,128);poison_source=0;}
}
static void service(xctx *c)
{
 assert(pthread_equal(pthread_self(),main_thread));
 int guard=xv_object_math_lock();assert(!xv_object_marker_admit(c,guard));
 xv_object_math_unlock(&guard);services++;c->r[4]+=4;
}
static void marker_rejected(xctx *c)
{
 xctx before=*c;unsigned char bytes[4096];uint32_t page=c->r[4]&~4095u;
 memcpy(bytes,X_G(page),sizeof bytes);assert(!xv_math_marker_record(c));
 assert(!memcmp(c,&before,sizeof before));assert(!memcmp(bytes,X_G(page),sizeof bytes));
}
void f_0008FB70(xctx *c)
{
 int lane=worker_lane();assert(lane>=0&&lane<2);uint32_t entry=c->r[4];
 __atomic_fetch_add(&arrived,1,__ATOMIC_RELEASE);
 while(__atomic_load_n(&arrived,__ATOMIC_ACQUIRE)!=2)sched_yield();
 for(unsigned iteration=0;iteration<32;iteration++) {
  if(lane==1&&iteration==8){X_PUSH32(0x3223fu);xv_object_job_hle(c,0x184ab0u,service);}
  uint32_t area=stacks[lane]+0x1000,sp=area+512,out=area+1024;
  uint32_t source=0x30000+lane*4096,matrix=source+64;
  memset(X_G(area),0xa5,4096);memset(X_G(source),0,128);
  X_MF32(source+28)=1.f;X_MF32(source+4)=iteration*.25f;
  X_MF32(matrix)=1.f;X_MF32(matrix+4)=1.f;X_MF32(matrix+20)=1.f;X_MF32(matrix+36)=1.f;
  c->r[0]=0;c->r[4]=sp;c->r[6]=out;c->r[7]=source;c->df=0;
  X_M32(sp+0x24)=matrix;
  xctx initial=*c,expected=*c;
  unsigned char before[4096],after[4096],source_before[128];memcpy(before,X_G(area),4096);
  memcpy(source_before,X_G(source),128);
  {int guard=xv_object_math_lock();current_marker(&expected);xv_object_math_unlock(&guard);}
  memcpy(after,X_G(area),4096);memcpy(X_G(area),before,4096);
  poison_source=source;assert(xv_math_marker_record(c));assert(!poison_source);
  assert(!math_depth[lane]);assert(!memcmp(c,&expected,sizeof expected));
  assert(!memcmp(X_G(area),after,4096));accepted[lane]++;
  *c=initial;memcpy(X_G(area),before,4096);memcpy(X_G(source),source_before,128);
  poison_source=source;hooked_marker(c);assert(!poison_source);
  assert(!memcmp(c,&expected,sizeof expected));assert(!memcmp(X_G(area),after,4096));
  *c=initial;
  xctx copy=*c;marker_rejected(&copy);
  {int guard=xv_object_math_lock();marker_rejected(c);xv_object_math_unlock(&guard);}
  c->r[6]=0x50000;marker_rejected(c);*c=initial;
  c->r[6]=stacks[1-lane]+0x2000;marker_rejected(c);*c=initial;
  c->df=1;marker_rejected(c);*c=initial;
  c->r[7]=UINT32_MAX-15;marker_rejected(c);*c=initial;
  c->r[7]=out-4;marker_rejected(c);*c=initial;
  X_M32(sp+0x24)=out;marker_rejected(c);X_M32(sp+0x24)=matrix;
  c->r[6]=area+4096-52;marker_rejected(c);*c=initial;
  uint32_t saved=g_xpt[out>>12];g_xpt[out>>12]=0x60000+lane*4096;
  marker_rejected(c);g_xpt[out>>12]=saved;
 }
 c->r[4]=entry+4;
}
int main(void)
{
 main_thread=pthread_self();g_xram=calloc(1,SIZE);g_img_base=g_xram;g_xpt=calloc(1u<<20,4);
 for(unsigned i=0;i<SIZE/4096;i++)g_xpt[i]=i*4096;
 X_MF32(0x1f0a68)=0;X_MF32(0x1f0a78)=1;X_MF32(0x1f0b04)=2;
 setenv("XV_NATIVE_MARKER_RECORD","1",1);setenv("XV_OBJECT_JOB_WORKERS","2",1);
 setenv("XV_OBJECT_PRIVATE_MATH","1",1);assert(initialize());
 next=0;count=2;owner=(xctx*)1;memset(jobs,0,2*sizeof jobs[0]);
 __atomic_store_n(&running,1,__ATOMIC_RELEASE);
 for(unsigned i=0;i<2;i++)sem_post(&wakes[i]);service_owner();
 for(unsigned i=0;i<2;i++)wait_sem(&dones[i]);
 __atomic_store_n(&running,0,__ATOMIC_RELEASE);count=0;owner=NULL;
 assert(services==1&&accepted[0]==32&&accepted[1]==32);
 assert(marker_captured[0]==64&&marker_captured[1]==64);
 puts("PASS: 64 captured markers on production workers; exact context/private memory; source replacement, owner service and rejection checks");
 xv_object_jobs_shutdown();free(g_xpt);free(g_xram);
}
