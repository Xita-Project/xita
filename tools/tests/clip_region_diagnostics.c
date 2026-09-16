#include "kernel/xk_clip_region.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t*g_xram,*g_img_base;uint32_t*g_xpt;
volatile uint32_t xv_cur_fn;int xv_trace_funcs,xv_watch_n;
static unsigned events,which;static xctx *active;
struct record {xctx c;unsigned from,back,kind;};static struct record history[128];
static void observe(unsigned kind,uint32_t from,uint32_t back){assert(events<128);struct record r={0};r.c=*active;r.from=from;r.back=back;r.kind=kind;if(!which)history[events]=r;else assert(!memcmp(&r,&history[events],sizeof r));events++;}
void xv_trace_func(uint32_t fn){observe(0,fn,0);}
void xv_watch_enter(uint32_t fn,xctx*c){assert(c==active);observe(1,fn,0);c->fsp=(c->fsp+3)&7;c->fsw^=0x400;c->scratch^=0x1234;}
void xv_watch_leave(uint32_t fn,uint32_t back,xctx*c){assert(c==active);observe(2,fn,back);c->fsp=(c->fsp+5)&7;c->fsw^=0x800;c->scratch^=0x5678;}
void baseline(xctx*),candidate(xctx*),xv_clip_registers_override(int);
int main(void){unsigned size=2<<20;g_xram=malloc(size);g_img_base=g_xram;g_xpt=calloc(1<<20,4);unsigned char*initial=malloc(size),*wanted=malloc(size);assert(g_xram&&g_xpt&&initial&&wanted);for(unsigned i=0;i<size/4096;i++)g_xpt[i]=i*4096;xv_native_clip_region_init();xv_native_clip_region_override(1);
 for(int regs=0;regs<2;regs++)for(unsigned diag=0;diag<4;diag++){
  xv_clip_registers_override(regs);xv_watch_n=diag&1;xv_trace_funcs=(diag>>1)&1;
  memset(g_xram,0,size);for(unsigned n=0;n<8;n++){double a=6.283185307179586*n/8;X_MF32(0x10000+n*8)=cos(a)*.25;X_MF32(0x10004+n*8)=sin(a)*.25;}
  const float edges[8]={-1,-1,-1,1,1,1,1,-1};memcpy(X_G(0x30000),edges,sizeof edges);X_MF32(0x1F0A68)=0;X_MF32(0x1F0A78)=1;double tiny=.0001;x_guest_write(0x1F0AF8,&tiny,8);
  unsigned sp=0x90000;X_M32(sp)=0x12345678;X_M32(sp+4)=4;X_M32(sp+8)=0x30000;X_M32(sp+12)=64;X_M32(sp+16)=0x50000;X_MF32(sp+20)=.0001f;
  xctx start={0},expected;start.r[4]=sp;start.r[1]=8;start.r[2]=0x10000;start.fcw=0x37f;start.fsp=3;start.preempt=10000;for(unsigned j=0;j<8;j++)start.st[j]=j+.375;
  memcpy(initial,g_xram,size);unsigned expected_events=0;
  for(which=0;which<2;which++){xctx c=start;active=&c;events=0;xv_cur_fn=0;memcpy(g_xram,initial,size);if(!which)baseline(&c);else candidate(&c);if(!which){expected=c;expected_events=events;memcpy(wanted,g_xram,size);}else{assert(!memcmp(&expected,&c,sizeof c));assert(!memcmp(wanted,g_xram,size));assert(events==expected_events);}}
  xv_clip_region_work n;xv_clip_region_read_work(&n);int admitted=regs&&!diag&&atoi(getenv("XV_NATIVE_CLIP"));assert(n.regions==(unsigned)admitted);
 }
 xv_native_clip_region_override(-1);assert(!xv_math_clip_region(NULL,1));puts("PASS actual traced emission: watch/trace context mutations and call order, register/FP-only/native-disabled fallback, exact restored context/memory");return 0;
}
