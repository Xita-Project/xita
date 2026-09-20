#include "kernel/xk_object_jobs.h"
#include "kernel/xk_clip_region.h"
#include <stddef.h>
#define WORKERS 2
#define STACK_BYTES (256*1024u)
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const char xv_object_job_marker=0;
volatile uint32_t xv_cur_fn;int xv_watch_n,xv_trace_funcs;
static xctx *contexts;
static uint32_t stacks[2]={0x80000,0xc0000},stack_pages[2][64];
static unsigned math_depth[2],math_private_enabled=1;
static int math_private_override=-1;
unsigned clip_private_attempts[2],clip_private_released[2];
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
int xv_object_math_lock(void){math_depth[0]++;return 2;}
void xv_object_math_unlock(int *token){if(!*token)return;if(*token!=2||!math_depth[0])__builtin_trap();math_depth[0]--;}
#include "admission.h"
int xv_clip_region_begin(void){return 1;}
void xv_clip_region_end(const xv_clip_region_work*w){(void)w;}
void xv_clip_region_account(void){if(!math_depth[0])__builtin_trap();}
void __wrap_xv_preempt(xctx*c){(void)c;__builtin_trap();}
void full_candidate(xctx*),full_held(xctx*),f_000B7F10(xctx*);
static void setup(xctx*c){contexts=c;c->fiber=(void*)&xv_object_job_marker;math_depth[0]=0;clip_private_released[0]=0;for(unsigned i=0;i<64;i++)stack_pages[0][i]=g_xpt[0x80+i];}
void run_original(xctx*c){setup(c);f_000B7F10(c);}
void run_held(xctx*c){setup(c);full_held(c);if(math_depth[0])__builtin_trap();}
void run_private(xctx*c){setup(c);full_candidate(c);if(math_depth[0])__builtin_trap();}
void test_boot(void){}
void abort(void){__builtin_trap();}
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int v,size_t n){(void)v;(void)n;return d;}
int *__errno(void){static int e;return &e;}
/* Watch diagnostics are disabled in this execution fixture. */
char *getenv(const char*n){(void)n;return 0;}
int snprintf(char*d,size_t n,const char*f,...){(void)f;if(n)d[0]=0;return 0;}
unsigned long strtoul(const char*s,char**e,int b){(void)s;(void)e;(void)b;__builtin_trap();}
void sceClibPrintf(const char*f,...){(void)f;__builtin_trap();}
