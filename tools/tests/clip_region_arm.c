#include "clip_region_observe.h"
#include "kernel/xk_clip_region.h"
#include "kernel/xk_object_jobs.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t*g_xpt;
volatile uint32_t xv_cur_fn;int xv_watch_n=0; int xv_trace_funcs;static xctx*active;
unsigned lock_depth,clip_count,probe_count,yields;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
__attribute__((noinline,noipa)) void observe_event(unsigned kind,xctx*c){(void)kind;(void)c;}
void raw_probe(xctx*);
void f_0001D130(xctx*c){observe_event(1,c);xv_object_job_stack_probe(c);probe_count++;raw_probe(c);
#ifndef CLIP_TEST_NO_MARKERS
 xv_cur_fn=0x1D130;
#endif
}
void xv_watch_leave(uint32_t from,uint32_t back,xctx*c){(void)from;(void)back;if(xv_watch_n)observe_event(2,c);}
int __real_xv_object_math_lock(void);
void __real_xv_object_math_unlock(int*);
#ifdef PARK_MUTATION
static void park_mutate(xctx*c){
 c->fsp=(c->fsp+5)&7;c->fsw^=0x4300;c->fcw^=0x400;
 c->r[0]^=0xabcd0000;c->r[1]^=0x55000000;c->r[7]^=0x12345678;
 c->f_kind=XK_SUB;c->f_op1=0x1234;c->f_op2=0x8765;c->f_res=c->f_op1-c->f_op2;c->f_bits=32;c->f_cf_override=1;c->f_cf=1;
 static const uint64_t bits[]={0x8000000000000000ull,0x7ff0000000004321ull,0x7ff8000000001234ull,0xfff0000000000000ull,1ull,0x3ff0000000000000ull,0x400921fb54442d18ull,0ull};
 for(unsigned i=0;i<8;i++)memcpy(&c->st[i],&bits[(i+clip_count)&7],8);
 c->scratch^=0x31415926;uint32_t plane=X_M32(c->r[4]+12);X_M32(plane+8)^=0x00010000u;
 observe_event(0x30,c);
}


#endif
int __wrap_xv_object_math_lock(void){observe_event(3,active);
#ifdef PARK_MUTATION
park_mutate(active);
#endif
lock_depth++;clip_count++;return __real_xv_object_math_lock();}
void __wrap_xv_object_math_unlock(int*token){observe_event(4,active);lock_depth--;__real_xv_object_math_unlock(token);}
void __real_x_str_movs(xctx*,unsigned,int);
void __wrap_x_str_movs(xctx*c,unsigned size,int mode){observe_event(0x10+size,c);__real_x_str_movs(c,size,mode);}
void __wrap_xv_preempt(xctx*c){observe_event(5,c);yields++;c->preempt=7;}
void run_original(xctx*c){active=c;lock_depth=clip_count=probe_count=yields=0;xv_cur_fn=0xB7F10;original_wrapper(c);}
void run_current(xctx*c){active=c;lock_depth=clip_count=probe_count=yields=0;xv_cur_fn=0xB7F10;current_wrapper(c);}
void run_fused(xctx*c){active=c;lock_depth=clip_count=probe_count=yields=0;xv_cur_fn=0xB7F10;fused_wrapper(c);}
void test_boot(void){xv_native_clip_region_init();xv_native_clip_region_override(1);}
void abort(void){__builtin_trap();}
int sceKernelGetThreadId(void){return 1;}
void xk_os_log(const char*f,...){(void)f;}
void sceClibPrintf(const char*f,...){(void)f;}
char *getenv(const char*n){(void)n;return 0;}int atoi(const char*s){(void)s;return 1;}
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int v,size_t n){(void)v;(void)n;return d;}
int snprintf(char*d,size_t n,const char*f,...){(void)d;(void)n;(void)f;__builtin_trap();}
unsigned long strtoul(const char*s,char**e,int b){(void)s;(void)e;(void)b;__builtin_trap();}
int xv_phase_enabled;
/* Every unavailable kernel/worker route must fault in this owner-idle model. */
#define STOP(name) void name(void){__builtin_trap();}
STOP(sceKernelCreateLwMutex) STOP(sceKernelCreateMutex) STOP(sceKernelCreateSema)
STOP(sceKernelCreateThread) STOP(sceKernelDelayThread) STOP(sceKernelDeleteLwMutex)
STOP(sceKernelDeleteMutex) STOP(sceKernelDeleteSema) STOP(sceKernelDeleteThread)
STOP(sceKernelGetThreadCurrentPriority) STOP(sceKernelLockLwMutex) STOP(sceKernelLockMutex)
STOP(sceKernelPollSema) STOP(sceKernelSignalSema) STOP(sceKernelStartThread)
STOP(sceKernelTryLockLwMutex) STOP(sceKernelTryLockMutex) STOP(sceKernelUnlockLwMutex)
STOP(sceKernelUnlockMutex) STOP(sceKernelWaitSema) STOP(sceKernelWaitThreadEnd)
STOP(xk_mem_alloc) STOP(xk_mem_free) STOP(xk_object_io_step) STOP(xk_os_monotonic_us)
STOP(xv_cpu_log_thread) STOP(f_0008FB70)

int *__errno(void){static int e;return &e;}
