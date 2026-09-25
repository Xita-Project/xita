#include "kernel/xk.h"
#include "kernel/xk_owner_phase.h"
#include "kernel/xk_model_uv.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;static xk_thread first,second;
#define context first.ctx
xctx *const arm_context_ptr=&context;
xk_thread *xk_cur;static xk_fiber *current_fiber;
const char xv_object_job_marker=0;
unsigned on_worker,thread_id=17;
int xv_object_is_worker_thread(void){return on_worker;}
int xv_object_is_worker_id(int32_t id){if(id!=(int32_t)thread_id)__builtin_trap();return on_worker;}
__attribute__((noinline)) int __wrap_sceKernelGetThreadId(void){return thread_id;}
int32_t xv_owner_thread_id(void){return __wrap_sceKernelGetThreadId();}
xk_fiber *xk_os_fiber_current(void){return current_fiber;}
uint64_t xk_os_monotonic_us(void){return 1;}
void xk_os_log(const char*f,...){(void)f;}
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
uint32_t pages[1024];unsigned scope_active=1,diagnostic_active,root_mode;
int xv_watch_n,xv_trace_funcs;
unsigned arena_bytes=8u<<20,image_lo=0x10000,image_hi=0x400000;
uint32_t xk_mem_arena_size(void){return arena_bytes;}
uint32_t xk_mem_image_lo(void){return image_lo;}
uint32_t xk_mem_image_hi(void){return image_hi;}
static unsigned model_token,nested_token;static xv_owner_phase_scope scene_token;
void f_00056F20(xctx*);void uv_cached(xctx*);
void abort(void){__builtin_trap();}char *getenv(const char*x){(void)x;return 0;}
/* No environment value exists in this fixture: conversion must stay cold. */
int atoi(const char*x){(void)x;abort();return 0;}
void x_guest_read_pages(void*out,uint32_t a,size_t n){unsigned char*p=out;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(p,X_G(a),k);p+=k;a+=k;n-=k;}}
void x_guest_write_pages(uint32_t a,const void*in,size_t n){const unsigned char*p=in;while(n){size_t k=4096-(a&4095);if(k>n)k=n;memcpy(X_G(a),p,k);p+=k;a+=k;n-=k;}}
void xv_preempt(xctx*c){(void)c;abort();}void xv_trap(xctx*c,uint32_t a){(void)c;(void)a;abort();}
void xv_call_indirect(xctx*c,uint32_t a){(void)c;(void)a;abort();}
void f_00180ADA(xctx*c){(void)c;abort();} /* Unadmitted animated waveform CRT frontier. */
void arm_prepare(unsigned top,unsigned sp,unsigned variant){
 for(unsigned i=0;i<1024;i++)pages[i]=(i^1)*4096;g_xpt=pages;g_img_base=g_xram+(4<<20);
 memset(g_xram,0x5a,8<<20);memset(&context,0xa5,sizeof context);
 X_M32(0x1f0a68)=0;X_M32(0x1f0a78)=0x3f800000;X_M32(0x1f0b40)=0x3c8efa35;
 for(unsigned i=0;i<8;i++){context.r[i]=0x33330000+i;context.st[i]=i*.125;}
 context.r[4]=sp?sp:0x90800;context.r[6]=0x100100;context.r[3]=context.r[4]+0x80;context.r[7]=context.r[4]+0x90;context.r[1]=0x110100;
 X_M32(context.r[1]+4)=0x120000;for(unsigned i=0;i<8;i++)X_MF32(0x120000+i*4)=1.25f+i*.125f;
 const uint32_t desc[14]={0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x3f800000,0,0x43b40000,0,0};
 for(unsigned i=0;i<14;i++)X_M32(context.r[6]+i*4)=desc[i];
 float args[6]={1,1,0,0,0,17.5f};if(variant){args[0]=1.5f;args[1]=.25f;args[2]=.75f;args[3]=-2;args[4]=45;}
 for(unsigned i=0;i<6;i++)X_MF32(context.r[4]+4+i*4)=args[i];X_M32(context.r[4])=0x70960;
 context.fsp=top&7;context.fcw=0x023f;context.fsw=0x7800;context.preempt=1000;context.df=0;
 context.f_kind=XK_SUB;context.f_bits=32;context.f_op1=2;context.f_op2=1;context.f_res=1;context.f_cf_override=context.f_of_override=0;
 scope_active=1;diagnostic_active=0;context.fiber=NULL;
 X_IMG32(0x2e3520)=0x110000;
}
void arm_original(void){f_00056F20(&context);}void arm_candidate(void){uv_cached(&context);}void arm_reset(void){
 on_worker=0;thread_id=17;xk_cur=&first;first.state=0;first.fiber=(xk_fiber*)&first;current_fiber=first.fiber;
 xv_owner_phase_enabled=1;xv_owner_phase_present(&context);
 xk_model_uv_scope_end(&context,model_token);xv_owner_phase_end(&scene_token);
 xv_watch_n=xv_trace_funcs=0;root_mode=0;arena_bytes=8u<<20;image_lo=0x10000;image_hi=0x400000;
 xv_owner_phase_begin(&scene_token,&context,XV_OWNER_SCENE);
 model_token=xk_model_uv_scope_begin(&context,g_xram,g_xpt,g_img_base);
}
void uv_cached(xctx*c){unsigned token;
 xv_watch_n=diagnostic_active;
 if(!scope_active)xk_model_uv_scope_end(c,model_token);
 if(xk_model_uv_begin(c,g_xram+(root_mode==1),g_xpt+(root_mode==2),g_img_base+(root_mode==3),0,&token))return;
 f_00056F20(c);xk_model_uv_end(c,token);
}
void arm_scope_end(void){xk_model_uv_scope_end(&context,model_token);}
void arm_nested_begin(void){nested_token=xk_model_uv_scope_begin(&context,g_xram,g_xpt,g_img_base);}
void arm_nested_end(void){xk_model_uv_scope_end(&context,nested_token);}
void arm_owner_case(unsigned which){
 if(which==1)on_worker=1;
 if(which==2)thread_id=18;
 if(which==3)xk_cur=&second;
 if(which==4)current_fiber=NULL;
 if(which==5)context.fiber=(void*)&xv_object_job_marker;
 if(which==6)xv_owner_phase_end(&scene_token);
 if(which==7){xk_cur=&second;second.state=0;second.fiber=(xk_fiber*)&second;current_fiber=second.fiber;xv_owner_phase_present(&second.ctx);}
}
void test_boot(void){}

void arm_scope_begin(void){model_token=xk_model_uv_scope_begin(&context,g_xram,g_xpt,g_img_base);}
void arm_scene_end(void){xv_owner_phase_end(&scene_token);}
void arm_scene_begin(void){xv_owner_phase_begin(&scene_token,&context,XV_OWNER_SCENE);}
void arm_present(void){xv_owner_phase_present(&context);}
void arm_pending_begin(void){xk_model_uv_begin(&context,g_xram,g_xpt,g_img_base,0,&model_token);}
void arm_pending_finish(void){f_00056F20(&context);xk_model_uv_end(&context,model_token);}
void arm_model_original(void){arm_original();arm_scope_end();arm_scope_begin();}
void arm_model_candidate(void){arm_candidate();arm_scope_end();arm_scope_begin();}
