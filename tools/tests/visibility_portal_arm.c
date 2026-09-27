/* Private retained-object fixture services. Guest/math bodies are not mocked.
 * Scheduler callbacks here are deliberately returning test services; this is
 * not production scheduler, worker admission, or fatal-stop qualification. */
#include "xv_x86rt.h"
#include <stddef.h>
#include <stdint.h>
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
int xv_phase_enabled;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),
 offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),
 offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
unsigned vp_yields,vp_stack_probes,vp_refill=10000;
void test_boot(void){}
void __wrap_xv_preempt(xctx*c){vp_yields++;c->preempt=(int32_t)vp_refill;}
void xv_object_job_stack_probe(xctx*c){(void)c;vp_stack_probes++;}
int xv_is_object_job(xctx*c){(void)c;return 0;}
int xv_object_math_lock(void){return 0;}
void xv_object_math_unlock(int *p){(void)p;}
void xv_object_math_report_check(void){}
int xv_object_math_release_private(xctx*c,int*token,unsigned kind,unsigned out,
 unsigned bytes,unsigned scratch,unsigned scratch_bytes)
{(void)c;(void)token;(void)kind;(void)out;(void)bytes;(void)scratch;(void)scratch_bytes;return 0;}
int sceKernelGetThreadId(void){return 17;}
#ifdef XV_PORTAL_SCENE_FIXTURE
unsigned portal_test_scene, portal_test_mode_off;
static int keyeq(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
char *getenv(const char*s){
 if(keyeq(s,"XV_SCENE_PORTAL"))return portal_test_scene?"1":"0";
 if(keyeq(s,"XV_NATIVE_CLIP")&&portal_test_mode_off)return "0";
 return 0;
}
int atoi(const char*s){return s&&*s=='1';}
#else
char *getenv(const char*s){(void)s;return 0;}
int atoi(const char*s){(void)s;return 0;}
#endif
void xk_os_log(const char*s,...){(void)s;}
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int s,size_t n){(void)s;(void)n;return d;}
void abort(void){__builtin_trap();}
