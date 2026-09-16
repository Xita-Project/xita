/* Numeric ARM oracle only. Native worker ownership/parking is tested by the
 * pthread fixture; this single-thread instruction harness models that gate. */
#include "kernel/xk_worker_query.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t*g_xpt;
static xctx *active;static unsigned depth;
unsigned admitted,yields,query_depth,query_dirty;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void ref_00056670(xctx*),f_00056670(xctx*);
int xv_object_math_lock(void){depth++;return 2;}
void xv_object_math_unlock(int *token){if(*token)depth--;}
int xv_object_query_lane(xctx*c,int guard,uint32_t base,unsigned bytes)
{return c==active&&guard==2&&depth==1&&base>=0x100000&&base+bytes<0x200000?1:0;}
void xv_worker_query_test_ready(xctx*c,unsigned lane){(void)c;(void)lane;admitted++;}
unsigned xk_mem_arena_size(void){return 8<<20;}
uint32_t xk_mem_image_lo(void){return 0;}
uint32_t xk_mem_image_hi(void){return 4<<20;}
uint64_t xk_os_monotonic_us(void){return 0;}
void run_original(xctx*c){active=c;depth=admitted=yields=0;ref_00056670(c);}
void run_candidate(xctx*c){active=c;depth=admitted=yields=0;f_00056670(c);}
void test_boot(void){}
void __wrap_xv_preempt(xctx*c){yields++;c->preempt=1000000;}
void abort(void){__builtin_trap();}
void xk_os_log(const char*f,...){(void)f;}
void sceClibPrintf(const char*f,...){(void)f;}
char *getenv(const char*n){(void)n;return 0;}
int snprintf(char*d,size_t n,const char*f,...){(void)d;(void)n;(void)f;__builtin_trap();}
unsigned long strtoul(const char*s,char**e,int b){(void)s;(void)e;(void)b;__builtin_trap();}
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int v,size_t n){(void)v;(void)n;return d;}
int memcmp(const void*a,const void*b,size_t n){(void)a;(void)b;(void)n;return 0;}
int *__errno(void){static int e;return &e;}
