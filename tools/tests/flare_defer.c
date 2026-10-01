#define _XOPEN_SOURCE 700
#include "xv_x86rt.h"
#include "kernel/xk_flare.h"
#include "xv_visibility.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdarg.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
volatile uint32_t xv_cur_fn=0x60560;
static uint32_t rng=418975, counts[1024];
static unsigned ready=1,submitted=1,force_error,read_ids[2048],nreads;
static unsigned sleeps,event_waits,word_waits,notifies,spurious,complete_after=1;
static uint64_t now=1;
static int asynchronous;
static ucontext_t main_fiber,drain_fiber,reset_fiber;
static unsigned reset_done,drain_done;
static uint8_t before_reset;
static unsigned dependency_callbacks;
static void require_retired(void)
{assert(ready);assert(X_IMG8(0x27ffb0+13)==(3*0xa5+255)/4);dependency_callbacks++;}
void xd3d_r_visibility_begin(uint32_t w,uint32_t h)
{(void)w;(void)h;require_retired();}
uint32_t xd3d_r_visibility_end(uint32_t id)
{assert(id==0);require_retired();return 0;}
void xd3d_r_present(uint32_t frame,uint32_t draws)
{(void)frame;(void)draws;require_retired();}
extern void xv_hle_D3DDevice_BeginVisibilityTest(xctx *);
extern void xv_hle_D3DDevice_EndVisibilityTest(xctx *);
extern void xv_hle_D3DDevice_Swap(xctx *);
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
void xk_os_log(const char *fmt,...){(void)fmt;}
uint64_t xk_os_monotonic_us(void){return ++now;}
void xk_os_scheduler_notify(void){notifies++;}
uint32_t xd3d_r_visibility_result(uint32_t id,uint32_t *pixels)
{
 assert(id<1024);
 if(id>=512)return XV_VISIBILITY_INVALID_ARGUMENT;
 if(!ready)return XV_VISIBILITY_INCOMPLETE;
 if(force_error&&id==0)return XV_VISIBILITY_INVALID_ARGUMENT;
 assert(nreads<2048);read_ids[nreads++]=id;*pixels=counts[id];return 0;
}
int xd3d_r_visibility_wait(uint32_t id,uint32_t timeout,uint32_t *age,uint32_t *elapsed)
{
 (void)id;(void)age;(void)elapsed;assert(timeout==100000);event_waits++;
 if(asynchronous){assert(!swapcontext(&drain_fiber,&main_fiber));return 1;}
 if(!submitted)return 0;
 if(spurious){spurious--;return 1;}
 ready=1;return 1;
}
void xk_sleep_us(uint64_t delay)
{assert(delay<=1000);sleeps++;if(asynchronous){assert(!swapcontext(&drain_fiber,&main_fiber));return;}if(sleeps>=complete_after)ready=1;}
void xk_yield(void){xk_sleep_us(0);}
int xk_wait_u32(const uint32_t *word,uint32_t value,uint64_t timeout)
{
 assert(asynchronous&&value==0&&timeout==100000);word_waits++;
 assert(__atomic_load_n(word,__ATOMIC_ACQUIRE)!=value);
 assert(!swapcontext(&reset_fiber,&main_fiber));
 return __atomic_load_n(word,__ATOMIC_ACQUIRE)==value;
}
void f_00060460(xctx *);
void f_00063460(xctx *c)
{unsigned id=c->r[6];assert(id<1024);c->r[0]=(id>=512||(force_error&&id==0))?UINT32_MAX:counts[id];c->r[4]+=4;}
static xctx context(uint32_t parent)
{
 xctx c={0};for(unsigned i=0;i<8;i++){c.r[i]=next();c.st[i]=i+.375;}
 c.r[4]=0x3e1100;c.r[5]=0x3e2300;c.preempt=10000;c.f_kind=XK_SUB;c.f_bits=32;c.f_res=1;c.fcw=0x37f;
 X_M32(c.r[4])=0x8022a;X_M32(c.r[4]+4)=parent;X_M16(c.r[5]+0xc)=1;return c;
}
static void seed(unsigned n,unsigned mode)
{
 memset(g_xram,0xa5,4u<<20);X_IMG32(0x2e34e0)=n;
 for(unsigned i=0;i<n;i++){
  uint32_t row=0x2c76d0+i*40u;unsigned object=(i&1)||mode==1;
  X_IMG16(row+0x1e)=object?0x8000:(i%19);
  X_IMG16(row+0x20)=mode==1?3:next()%32;
  X_IMG8(row+0x22)=mode==1?1:next()%4;
  int32_t area=(int32_t)next();if(i==0)area=511;
  X_IMG32(row+0x24)=(uint32_t)area;counts[i]=next();
 }
 ready=submitted=1;force_error=0;nreads=sleeps=event_waits=0;complete_after=1;
}
static void rejected(xctx *c)
{
 uint8_t *before=malloc(4u<<20);assert(before);memcpy(before,g_xram,4u<<20);xctx saved=*c;
 assert(!xv_flare_defer(c));assert(!memcmp(c,&saved,sizeof saved));assert(!memcmp(before,g_xram,4u<<20));free(before);
}
static void drain_run(void){xv_flare_barrier(XV_FLARE_QUERY);drain_done=1;}
static void reset_run(void){xv_flare_barrier(XV_FLARE_IDENTITY);before_reset=X_IMG8(0x27ffb0+13);X_IMG8(0x27ffb0+13)=0;reset_done=1;}
int main(int argc,char **argv)
{
 g_xram=malloc(4u<<20);g_img_base=g_xram;g_xpt=calloc(1u<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096u;
 seed(1,1);xctx c=context(0x5dc0c);ready=0;
 if(argc>1 && !strcmp(argv[1],"stale")) {xv_flare_defer_override(1);rejected(&c);puts("PASS: stale-result mode cannot enable deferred exact results");return 0;}
 const char *configured=getenv("XV_FLARE_DEFER");
 int configured_on=!configured || atoi(configured)!=0;
 if(configured_on) { assert(xv_flare_defer(&c));ready=1;xv_flare_barrier(XV_FLARE_NEXT); }
 else rejected(&c);
 seed(1,1);c=context(0x5dc0c);ready=0;
 xv_flare_defer_override(0);rejected(&c);
 xv_flare_defer_override(-1);
 if(configured_on) { assert(xv_flare_defer(&c));ready=1;xv_flare_barrier(XV_FLARE_NEXT); }
 else rejected(&c);
 xv_flare_defer_override(1);
 uint8_t *initial=malloc(4u<<20),*expected=malloc(4u<<20),*during=malloc(4u<<20);assert(initial&&expected&&during);
 for(unsigned k=0;k<600;k++){
  unsigned n=k%20==0?1024:k%521+1;seed(n,k%7==0);force_error=k%13==0;
  c=context(k%3==0?0x5dc0c:k%3==1?0x5d288:0xbc624);xctx reference=c;
  memcpy(initial,g_xram,4u<<20);f_00060460(&reference);memcpy(expected,g_xram,4u<<20);memcpy(g_xram,initial,4u<<20);
  ready=0;submitted=k%3!=0;spurious=k%7==0?2:0;complete_after=k%3?1:4;
  xctx after=c;after.r[4]+=4;
  assert(xv_flare_defer(&c));assert(!memcmp(&c,&after,sizeof c));assert(!X_IMG32(0x2e34e0));
  assert(!memcmp(g_xram+0x27ffb0,initial+0x27ffb0,0x47720));
  /* Current-frame records may overwrite the list before the old result is used. */
  memset(g_xram+0x2c76d0,0xdb,1024*40);X_IMG32(0x2e34e0)=17;
  xv_flare_barrier(XV_FLARE_COLLECTION);assert(!ready);
  memcpy(during,g_xram,4u<<20);
  if(k%11==0)xv_flare_defer_override(0); /* Existing work still has to drain. */
  xv_cur_fn=0x606b0;
  xv_flare_barrier(k%6);assert(xv_cur_fn==0x606b0);assert(!memcmp(&c,&after,sizeof c));
  assert(!memcmp(g_xram+0x27ffb0,expected+0x27ffb0,0x47720));
  memcpy(during+0x27ffb0,expected+0x27ffb0,0x47720);assert(!memcmp(g_xram,during,4u<<20));
  assert(X_IMG32(0x2e34e0)==17);assert(ready);
  xv_flare_defer_override(1);
 }
 /* Real HLE entry points must retire old reads before reusing IDs or Present. */
 void (*dependencies[])(xctx *)={xv_hle_D3DDevice_BeginVisibilityTest,xv_hle_D3DDevice_EndVisibilityTest,xv_hle_D3DDevice_Swap};
 for(unsigned i=0;i<3;i++){
  seed(1,1);counts[0]=511;c=context(0x5dc0c);ready=0;
  assert(xv_flare_defer(&c));X_M32(c.r[4]+4)=0;
  dependencies[i](&c);assert(dependency_callbacks==i+1);
 }
 /* Bounds, callers and eager results cannot partially publish or mutate inputs. */
 for(unsigned test=0;test<8;test++){
  seed(2,0);c=context(0x5dc0c);ready=0;
  switch(test){
   case 0:X_M32(c.r[4])=0xdead;break;
   case 1:X_M32(c.r[4]+4)=0xdead;break;
   case 2:X_M16(c.r[5]+0xc)=0;break;
   case 3:X_IMG32(0x2e34e0)=1025;break;
   case 4:X_IMG32(0x2e34e0)=UINT32_MAX;break;
   case 5:X_IMG16(0x2c76d0+40+0x1e)=0x8100;break;
   case 6:ready=1;break;
   case 7:X_IMG32(0x2e34e0)=0;break;
  }
  rejected(&c);
 }
 /* A second cooperative fiber cannot reset an entry while the first waits. */
 seed(1,1);counts[0]=511;c=context(0x5dc0c);ready=0;assert(xv_flare_defer(&c));
 unsigned notifications=notifies;asynchronous=1;
 char *a=malloc(1u<<20),*b=malloc(1u<<20);assert(a&&b);
 assert(!getcontext(&drain_fiber));drain_fiber.uc_stack.ss_sp=a;drain_fiber.uc_stack.ss_size=1u<<20;drain_fiber.uc_link=&main_fiber;makecontext(&drain_fiber,drain_run,0);
 assert(!getcontext(&reset_fiber));reset_fiber.uc_stack.ss_sp=b;reset_fiber.uc_stack.ss_size=1u<<20;reset_fiber.uc_link=&main_fiber;makecontext(&reset_fiber,reset_run,0);
 assert(!swapcontext(&main_fiber,&drain_fiber));assert(!drain_done&&!reset_done);
 assert(!swapcontext(&main_fiber,&reset_fiber));assert(word_waits&&!reset_done);
 ready=1;assert(!swapcontext(&main_fiber,&drain_fiber));assert(drain_done&&!reset_done&&notifies==notifications+1);
 assert(!swapcontext(&main_fiber,&reset_fiber));assert(reset_done&&before_reset==(3*0xa5+255)/4&&X_IMG8(0x27ffb0+13)==0);
 free(a);free(b);free(initial);free(expected);free(during);free(g_xpt);free(g_xram);
 puts("PASS: 600 deferred batches against the original loop; alias order, 1024 records, new-list mutation, errors, polls, timeouts, context preservation, fallback and competing-fiber barriers");
}
