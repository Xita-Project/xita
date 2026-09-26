"""Exercise the production constant setter and its dirty-range consumer timeline."""
from pathlib import Path
import subprocess,tempfile,argparse
root=Path(__file__).resolve().parents[2]
s=(root/'recomp/kernel/xd3d.c').read_text();s=s[s.index('static float vsc_scratch'):s.index('/* Halo builds its register-combiner')]
pre=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef uint32_t xu32_u __attribute__((aligned(1),may_alias));
typedef struct {uint32_t r[8],args[3];} xctx;
static unsigned char guest[16384];
static struct state {float vsc[192][4];uint32_t vsc_dirty_lo,vsc_dirty_hi;} xd3d_state;
static int vsc_equal, g_opt_vsc;
static unsigned vsc_equal_checks,vsc_equal_skips,vsc_equal_bytes;
static int vsc_mode_now(void){return 2;}
static int xd3d_hist_gate(void){return 0;}
static int xv_rec_opt_result(int *o,int equal){(void)o;return !equal;}
static void x_guest_read(void *p,uint32_t a,unsigned n){assert(a+n<=sizeof guest);memcpy(p,guest+a,n);}
#define X_G(a) (guest+(a))
#define X_ARG(i) (c->args[i])
#define X_M32(a) (*(xu32_u *)(guest+(a)))
#define X_RET(n) do {c->r[4]+=4*((n)+1);}while(0)
#define XD3D_COUNT(...) ((void)0)
#define D3DLOG(...) ((void)0)
#define xk_os_log(...) ((void)0)
'''
consumer=(root/'runtime/xv_d3d.c').read_text()
consumer=consumer[consumer.index('void xv_d3d_SetAllConstants('):consumer.index('void xv_d3d_SetPixelShader(')]
consumer_pre=r'''
static struct recorder {float vsc[192][4];const float (*vsc_source)[4];unsigned vsc_gen;} S;
static unsigned scan_constant_checks,scan_constant_bytes,scan_constant_reused;
static int draw_scan_neon(void){return 0;}
static int xv_bytes_equal(const void *a,const void *b,size_t n){return !memcmp(a,b,n);}
'''
post=r''' 
int main(void){
 struct state states[2]={0};float consumed[2][192][4]={0};struct recorder recorded[2]={0};uint32_t seed=1;
 states[0].vsc_dirty_hi=states[1].vsc_dirty_hi=192;
 for(unsigned step=0;step<10000;step++){
  seed=seed*1664525u+1013904223u;
  uint32_t n=(seed>>24)%12+1,src=(step%13==0?4090:128);
  int reg=(int)((seed>>8)%210)-105;
  /* Repeated input between updates; include NaNs/infinities and page crossings. */
  if(step%5==0)for(unsigned j=0;j<n*4;j++){
   uint32_t bits=step%31==0?0x7f800000u:0x3f000000u+(j<<8);
   memcpy(guest+src+j*4,&bits,4);
  }
  xctx results[2];
  for(unsigned mode=0;mode<2;mode++){
   xctx c={0};c.args[0]=(uint32_t)reg;c.args[1]=src;c.args[2]=n;c.r[4]=8000;
   xd3d_state=states[mode];vsc_equal=(int)mode;
   xv_hle_D3DDevice_SetVertexShaderConstant(&c);states[mode]=xd3d_state;results[mode]=c;
   if(step%7==0){
    unsigned lo=states[mode].vsc_dirty_lo,hi=states[mode].vsc_dirty_hi;
    if(hi>lo)memcpy(consumed[mode][lo],states[mode].vsc[lo],(hi-lo)*16);
    S=recorded[mode];
    /* Alternate inline source and queued mirror; UI writes invalidate identity. */
    if(step%91==0){S.vsc_source=NULL;S.vsc[0][0]=123.0f;}
    xv_d3d_SetTrackedConstants(step%2?states[mode].vsc:consumed[mode],
        &states[mode].vsc_dirty_lo,&states[mode].vsc_dirty_hi);
    recorded[mode]=S;
   }
  }
  assert(!memcmp(results,results+1,sizeof(xctx)));
  assert(!memcmp(states[0].vsc,states[1].vsc,sizeof states[0].vsc));
  assert(!memcmp(consumed[0],consumed[1],sizeof consumed[0]));
  assert(!memcmp(recorded[0].vsc,recorded[1].vsc,sizeof recorded[0].vsc));
  assert(recorded[0].vsc_gen==recorded[1].vsc_gen);
 }
 assert(vsc_equal_skips>0);
 printf("PASS: 10000 setter/consumer steps, %u identical uploads skipped\n",vsc_equal_skips);
}
'''
ap=argparse.ArgumentParser();ap.add_argument('--cc',default='cc');ap.add_argument('--keep',type=Path);args=ap.parse_args()
with tempfile.TemporaryDirectory() as tmp:
 p=args.keep or Path(tmp);p.mkdir(parents=True,exist_ok=True);(p/'test.c').write_text(pre+s+consumer_pre+consumer+post)
 subprocess.run([args.cc,'-O2','-std=gnu11','-fno-strict-aliasing',*(['-static'] if args.keep else ['-fsanitize=address,undefined']),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 if not args.keep:subprocess.run([str(p/'test')],check=True)
