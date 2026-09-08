#!/usr/bin/env python3
"""Differentially check native BSP arithmetic against the original lifted block."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
reference=root/'recomp/host/build/original_bsp_interval.c'
if not reference.exists():
    raise SystemExit('Run tools/test_quality_hooks.py with the recompiler Python environment to generate '+str(reference))
head='''#include "xv_x86rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
void xv_bsp_plane_interval(xctx *);
static void original(xctx *c) {
'''
tail=r'''}
static uint32_t rng=919;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 g_xram=calloc(1,8<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
 for(unsigned i=0;i<2048;++i)g_xpt[i]=(i^1)*4096;
 unsigned cases=0;
 for(unsigned k=0;k<100000;++k){
  xctx a={0};a.fsp=k&7;a.r[0]=3;a.r[1]=0x11000+(k%4096);
  a.r[2]=0x18000+(k%4096);a.r[4]=0x21000+(k%4096);a.r[5]=0x31000;
  uint32_t direction=0x41000+(k%4096);X_M32(a.r[5]+0x14)=direction;
  for(unsigned j=0;j<8;++j)a.st[j]=j+12.75;
  uint32_t plane=a.r[2]+48;
  for(unsigned j=0;j<4;++j){
   uint32_t v=next();if(k&1)v=(v&0x807fffff)|((110+k%30)<<23);
   x_guest_write(plane+j*4,&v,4);
   if(j<3){v=next();x_guest_write(a.r[1]+j*4,&v,4);v=next();x_guest_write(direction+j*4,&v,4);}
  }
  x87_store_f32(&a,a.r[4]+0x20,-.2);x87_store_f32(&a,a.r[4]+0x24,1.75);
  xctx b=a;original(&a);
  uint8_t expected[12];x_guest_read(expected,a.r[4]+0x10,12);
  xv_bsp_plane_interval(&b);
  uint8_t actual[12];x_guest_read(actual,b.r[4]+0x10,12);
  for(unsigned j=0;j<3;++j){float x,y;memcpy(&x,expected+j*4,4);memcpy(&y,actual+j*4,4);assert(!memcmp(&x,&y,4)||(isnan(x)&&isnan(y)));}
  for(unsigned j=0;j<8;++j){assert(a.r[j]==b.r[j]);assert(!memcmp(&a.st[j],&b.st[j],8)||(isnan(a.st[j])&&isnan(b.st[j])));}
  assert(a.fsp==b.fsp&&a.fsw==b.fsw&&a.fcw==b.fcw);
  assert(XF_C(&a)==XF_C(&b)&&XF_Z(&a)==XF_Z(&b)&&XF_S(&a)==XF_S(&b)&&XF_O(&a)==XF_O(&b));cases++;
 }
 printf("BSP interval: %u original/native comparisons, all alignments, split pages, floating-point edges, spills and register/x87 state passed\n",cases);
 free(g_xpt);free(g_xram);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xita-bsp-math-') as directory:
    d=Path(directory);src=d/'test.c';src.write_text(head+reference.read_text()+tail)
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-ffp-contract=off','-I'+str(root),'-I'+str(root/'recomp'),
        '-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('BSP_TEST_CFLAGS','')),
        str(src),str(root/'recomp/kernel/xk_geometry.c'),str(root/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections','-lm','-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
