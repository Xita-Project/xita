#include "xv_recomp_protos.h"
#include "kernel/xd3d.h"
extern xctx *const arm_context_ptr;
void arm_prepare(unsigned,unsigned,unsigned);
void caller_front_original(xctx*);void caller_front_candidate(xctx*);
void caller_back_original(xctx*);void caller_back_candidate(xctx*);
volatile uint32_t xv_cur_fn;
unsigned callback_mode,callback_calls;
const unsigned publication_size=sizeof(xd3d_state);
__attribute__((noinline)) void uv_frontier(xctx*c){(void)c;__asm__ volatile("":::"memory");}
__attribute__((noinline)) void uv_callback(xctx*c){
 callback_calls++;
 if(callback_mode==1){X_M32(c->r[5]+0x9c)=0x3f400000;X_IMG32(0x2fc918)=0x41900000;}
 if(callback_mode==2){X_M32(c->r[5]+0xfc+44)=0;}
 if(callback_mode==3){c->st[(c->fsp+3)&7]=17.25;c->r[2]=0xdead1234;c->f_cf=123;}
}
void arm_caller_prepare(unsigned back,unsigned top,unsigned variant){
 arm_prepare(top,0,variant);xctx*c=arm_context_ptr;
 c->r[5]=0x100004;c->r[0]=0x110000;c->r[6]=c->r[5]+0xfc;
 X_MF32(c->r[5]+0x9c)=1;X_MF32(c->r[5]+0xa0)=1;X_MF32(c->r[5]+0x38)=.625f;
 X_MF32(c->r[5]+0xd8)=.25f;X_MF32(c->r[5]+0xec)=.5f;
 X_MF32(0x1100c4)=variant?1.5f:1;X_MF32(0x1100c8)=variant?.5f:1;
 X_M32(0x110088)=0x120000;X_IMG32(0x2e3520)=0x110000;X_IMG32(0x2fc918)=0x418c0000;
 c->r[3]=0x9085c;c->r[7]=0x9086c;c->r[1]=0x110084;
 c->r[4]=back?0x90800:0x907e8;
 if(!back){for(unsigned i=0;i<3;i++)X_M32(c->r[4]+8+i*4)=0;X_M32(c->r[4]+20)=0x418c0000;}
 memset(&xd3d_state,0,sizeof xd3d_state);xd3d_state.vsc_dirty_lo=192;
 callback_calls=0;callback_mode=0;
}
void arm_front_original(void){caller_front_original(arm_context_ptr);}void arm_front_candidate(void){caller_front_candidate(arm_context_ptr);}
void arm_back_original(void){caller_back_original(arm_context_ptr);}void arm_back_candidate(void){caller_back_candidate(arm_context_ptr);}
int xv_phase_enabled;
void xv_phase_begin(xv_phase_scope*s,void*c,unsigned i){(void)s;(void)c;(void)i;__builtin_trap();}
void xv_phase_end(xv_phase_scope*s){(void)s;__builtin_trap();}
int snprintf(char*s,size_t n,const char*f,...){(void)s;(void)n;(void)f;__builtin_trap();}
int atoi(const char*s){(void)s;__builtin_trap();}

void xv_object_job_hle(xctx*c,unsigned a,void(*f)(xctx*)){(void)c;(void)a;(void)f;__builtin_trap();}
