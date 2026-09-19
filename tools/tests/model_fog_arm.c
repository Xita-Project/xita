#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;static xctx context;xctx *const arm_context_ptr=&context;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
uint32_t pages[1024];
void fog_original(xctx*);void fog_cached(xctx*);void fog_reset(void);
void abort(void){__builtin_trap();} char *getenv(const char*x){(void)x;return 0;}
void x_guest_read_pages(void*out,uint32_t a,size_t n){unsigned char*p=out;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);p+=k;a+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void*in,size_t n){const unsigned char*p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);p+=k;a+=k;n-=k;}}
void xv_preempt(xctx*c){(void)c;abort();}void xv_trap(xctx*c,uint32_t a){(void)c;(void)a;abort();}
void xv_call_indirect(xctx*c,uint32_t a){(void)c;(void)a;abort();}
void arm_prepare(unsigned variant,unsigned top,unsigned sp){
 for(unsigned i=0;i<1024;i++)pages[i]=(i^1)*4096;g_xpt=pages;g_img_base=g_xram+(4<<20);
 memset(g_xram,0x5a,8<<20);memset(&context,0xa5,sizeof context);
 unsigned addr[]={0x1f0a68,0x1f0a78,0x2fc6c8,0x2fc6cc,0x2fc6d0,0x2fc8ac,0x2fc8b0,0x2fc8b4,0x2fc8b8,0x2fc8bc,0x2fc8c0,0x2fc8c8,0x2fc8cc,0x2fc8d0,0x2fc8d4,0x2fc8d8,0x2fc8dc,0x2fc8e0};
 float vals[]={0,1,2,3,4,.2f,.4f,.7f,.5f,1,20,.1f,.2f,.3f,.4f,.8f,.7f,.6f};
 for(unsigned i=0;i<18;i++)X_MF32(addr[i])=vals[i];X_IMG8(0x2fc8a8)=variant&2;
 for(unsigned i=0;i<8;i++){context.r[i]=0x33330000+i;context.st[i]=i*.125;}
 context.r[4]=sp?sp:0x90800;X_MF32(context.r[4]+0x48)=variant&1?-3.f:10.f;
 context.fsp=top&7;context.fcw=0x027f;context.fsw=0x3210;context.preempt=1000;context.df=0;
 context.f_kind=XK_SUB;context.f_bits=32;context.f_op1=2;context.f_op2=1;context.f_res=1;context.f_cf_override=context.f_of_override=0;
}
void arm_original(void){fog_original(&context);}void arm_candidate(void){fog_cached(&context);}void arm_reset(void){fog_reset();}void test_boot(void){}

/* Only ownership identity and memory-bound discovery are synthetic. The cache
 * and its admission/replay run from the production source below. */
int xv_watch_n,xv_trace_funcs;
unsigned root_mode;
unsigned owner_allowed=1,owner_generation=1,arena_bytes=8u<<20;
unsigned image_lo=0x10000,image_hi=0x400000;
int xv_owner_phase_active(void *c,unsigned phase,uint32_t *token){
 (void)phase;if(!owner_allowed || c!=&context || (*token && *token!=owner_generation))return -1;
 *token=owner_generation;return 1;
}
uint32_t xk_mem_arena_size(void){return arena_bytes;}
uint32_t xk_mem_image_lo(void){return image_lo;}
uint32_t xk_mem_image_hi(void){return image_hi;}
void xk_os_log(const char *format,...){(void)format;}
#include "../../recomp/kernel/xk_model_fog.h"
void fog_cached(xctx*c){unsigned token;if(xk_model_fog_begin(c,g_xram+(root_mode==1),g_xpt+(root_mode==2),g_img_base+(root_mode==3),&token))return;fog_original(c);xk_model_fog_end(c,token);}
void fog_reset(void){root_mode=0;xv_watch_n=xv_trace_funcs=0;owner_allowed=owner_generation=1;arena_bytes=8u<<20;image_lo=0x10000;image_hi=0x400000;xk_model_fog_override(&context,1);}
void arm_disable(void){xk_model_fog_override(&context,0);}
void arm_enable(void){xk_model_fog_override(&context,1);}
