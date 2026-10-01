#!/usr/bin/env python3
"""Compare the native visible-index job to the user's generated Halo 3925 code."""
from pathlib import Path
import os,re,shlex,subprocess
root=Path(__file__).resolve().parents[2]
head=r'''#define xv_call test_call
#define xv_preempt test_preempt
#include "xv_recomp_protos.h"
#include "kernel/xk.h"
#include "runtime/xv_geometry_sort.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#undef XV_FN
#undef XV_FN_BACK
#define XV_FN(a) ((void)0)
#define XV_FN_BACK(a) ((void)0)
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
'''
body=r'''void xv_call(xctx *c,uint32_t a) {if(a==0x11B5A0){f_0011B5A0(c);return;}assert(a==0x53F60);f_00053F60(c);}
void xv_preempt(xctx *c) {c->preempt=1000000;}
static uint32_t rng=123;
static uint32_t rand32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=4096*i;
 int32_t *a=(int32_t*)X_G(0x20000),*b=malloc(131072);
 unsigned sizes[]={0,1,2,7,8,9,31,128,511,512,513,4096,32767};
 for(unsigned style=0;style<5;style++)for(unsigned j=0;j<sizeof sizes/sizeof *sizes;j++){
  unsigned n=sizes[j];for(unsigned i=0;i<n;i++)a[i]=b[i]=style==0?(int32_t)rand32():style==1?(int32_t)i:style==2?(int32_t)(n-i):style==3?7:(int32_t)(rand32()%23)-11;
  xctx c={0};c.preempt=1000000;c.r[0]=n;c.r[1]=0x20000;c.r[4]=0x10000;X_M32(0x10004)=0x53F60;
  f_0011B5A0(&c);assert(c.r[4]==0x10008);xv_sort_triangles(b,n);
  for(unsigned i=0;i<n;i++)if(a[i]!=b[i]){printf("mismatch style%u n%u at%u: %d %d\n",style,n,i,a[i],b[i]);return 1;}
 }
 puts("65 original Xbox/native triangle-sort comparisons passed");
 for(unsigned fragmented=0;fragmented<2;fragmented++){
  if(fragmented)for(unsigned i=0x100;i<0x200;i++)g_xpt[i]=4096*(i^3);
  const uint32_t input=0x100ffc,output=0x150ffe,table=0x190002;
  X_M32(0x39BE58)=0x9000;X_M32(0x90fc)=table;
  for(unsigned i=0;i<32768*3;i++)X_M16(table+i*2)=(uint16_t)(i*17);
  for(unsigned j=0;j<sizeof sizes/sizeof *sizes;j++)for(unsigned pattern=0;pattern<3;pattern++){
   unsigned n=sizes[j];for(unsigned i=0;i<n;i++)X_M32(input+i*4)=pattern==0?rand32()%32768:pattern==1?n-i:3;
   unsigned bytes=n*6+16;
   uint8_t *before=malloc(n*4+1),*expected=malloc(bytes),*sorted=malloc(n*4+1);
   x_guest_read(before,input,n*4);
   xctx c={0};c.preempt=1000000;c.r[0]=output;c.r[1]=input;c.r[2]=222;c.r[3]=333;c.r[4]=0x10000;c.r[5]=555;c.r[6]=666;c.r[7]=777;X_M32(0x10004)=n;
   xctx native=c;f_00053FA0(&c);
   x_guest_read(expected,output,n*6);x_guest_read(sorted,input,n*4);
   x_guest_write(input,before,n*4);for(unsigned i=0;i<bytes;i++)X_M8(output+i)=0xA5;
   xv_hle_HaloBuildVisibleIndices(&native);
   for(unsigned i=0;i<n*6;i++)assert(X_M8(output+i)==expected[i]);
   for(unsigned i=n*6;i<bytes;i++)assert(X_M8(output+i)==0xA5);
   for(unsigned i=0;i<n*4;i++)assert(X_M8(input+i)==sorted[i]);
   for(unsigned i=0;i<8;i++)assert(c.r[i]==native.r[i]);
   assert(XF_C(&c)==XF_C(&native) && XF_Z(&c)==XF_Z(&native) && XF_S(&c)==XF_S(&native) && XF_O(&c)==XF_O(&native));
   free(before);free(expected);free(sorted);
  }
 }
 puts("78 original Xbox/native index expansions passed, including fragmented pages and register/flag ABI");
 free(b);free(scratch);free(g_xpt);free(g_xram);return 0;
}
'''
parts=[]
for name in ['0011B550','0011B5A0','00053F60','00053FA0']:
    for p in (root/'recomp').glob('code_*.c'):
        m=re.search(r'^void f_'+name+r'\(.*?^\}',p.read_text(),re.M|re.S)
        if m: parts.append(m[0]);break
    else:
        cache=root/'recomp/host/build/original_geometry_functions.c'
        m=re.search(r'^void f_'+name+r'\(.*?^\}',cache.read_text() if cache.exists() else '',re.M|re.S)
        if not m:raise SystemExit('Generate original Halo 3925 without the 53FA0 HLE and run this test once: '+name)
        parts.append(m[0])
p=root/'recomp/host/build/geometry_original_test.c';p.parent.mkdir(exist_ok=True)
(root/'recomp/host/build/original_geometry_functions.c').write_text('\n'.join(parts))
p.write_text(head+'\n'.join(parts)+'\n#include "kernel/xk_geometry.c"\n'+body)
exe=p.with_suffix('')
subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-I'+str(root),'-I'+str(root/'recomp'),'-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('GEOMETRY_TEST_CFLAGS','')),str(p),str(root/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
