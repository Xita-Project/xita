#!/usr/bin/env python3
"""Exercise the production private-clip guard using mapped worker stacks."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'recomp/kernel/xk_object_jobs.c').read_text()
def extract(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "xv_x86rt.h"
#define WORKERS 2
#define STACK_BYTES (256*1024u)
#define XV_OBJECT_HOLD_PROFILE 1
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
static xctx contexts[2];
static uint32_t stacks[2],stack_pages[2][64];
static unsigned math_depth[2],hold_enabled,math_private_enabled=1;
static int math_private_override=-1;
static unsigned clip_private_attempts[2],clip_private_released[2],unlocks;
static const char marker;
static int xv_is_object_job(xctx*c){return c->fiber==&marker;}
static void xv_object_math_unlock(int*g){assert(*g==2||*g==3);assert(math_depth[*g-2]==1);math_depth[*g-2]=0;unlocks++;}
'''
code+=extract('static int private_stack_span(unsigned lane,uint32_t address,unsigned bytes)\n{')
code+=extract('void xv_object_clip_release(')
code+=r'''
static void reset(void){
 memset(g_xram,0,4*1024*1024);memset(contexts,0,sizeof contexts);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
 for(unsigned lane=0;lane<2;lane++){
  stacks[lane]=0x10000+lane*STACK_BYTES;
  for(unsigned p=0;p<64;p++)stack_pages[lane][p]=stacks[lane]+p*4096;
  xctx*c=&contexts[lane];uint32_t b=stacks[lane];
  c->fiber=(void*)&marker;c->preempt=1000000;c->r[4]=b+0x4000;c->r[5]=b+0x5018;c->r[2]=b+0x8000;
  X_M32(c->r[5]+8)=4;X_M32(c->r[5]+12)=b+0x6000;X_M32(c->r[5]+16)=b+0x7000;X_M32(c->r[5]+20)=64;
  math_depth[lane]=1;
 }
 X_M32(0x1f0a78)=0x3f800000;hold_enabled=0;math_private_enabled=1;math_private_override=-1;unlocks=0;
}
static unsigned cases;
static void check(unsigned lane,int guard,int accepted){
 xctx before=contexts[lane];unsigned char *memory=malloc(4*1024*1024);memcpy(memory,g_xram,4*1024*1024);
 unsigned previous=unlocks;int oldguard=guard;
 xv_object_clip_release(&contexts[lane],&guard);
 assert(unlocks-previous==(unsigned)accepted);assert(guard==(accepted?0:oldguard));
 assert(!memcmp(&before,&contexts[lane],sizeof before));assert(!memcmp(memory,g_xram,4*1024*1024));free(memory);cases++;
}
int main(void){
 g_xram=calloc(1,4*1024*1024);g_img_base=g_xram;g_xpt=calloc(1024,4);
 for(unsigned lane=0;lane<2;lane++){
  reset();check(lane,lane+2,1);
  reset();X_M32(contexts[lane].r[5]+12)=contexts[lane].r[2];check(lane,lane+2,1);
  for(unsigned v=0;v<22;v++){
   reset();xctx*c=&contexts[lane];uint32_t bp=c->r[5];
   switch(v){
   case 0:c->df=1;break;case 1:c->preempt=65535;break;case 2:math_depth[lane]=2;break;
   case 3:hold_enabled=1;break;case 4:math_private_enabled=0;break;case 5:c->fiber=0;break;
   case 6:X_M32(bp+8)=0;break;case 7:X_M32(bp+8)=65;break;case 8:X_M32(bp+20)=3;break;
   case 9:X_M32(bp+20)=65;break;case 10:X_M32(bp+24)=stacks[lane]+0x9000;break;
   case 11:X_M32(bp+28)=stacks[lane]+0x9000;break;
   case 12:X_M32(bp+12)=stacks[1-lane]+0x6000;break;
   case 13:X_M32(bp+16)=stacks[1-lane]+0x7000;break;
   case 14:c->r[2]=stacks[1-lane]+0x8000;break;
   case 15:c->r[2]=bp;break;case 16:c->r[2]=stacks[lane]+0x6004;break;
   case 17:c->r[2]++;break;case 18:X_M32(0x1f0a78)=0;break;
   case 19:g_xpt[(stacks[lane]+0x6000)>>12]+=4096;break;
   case 20:c->r[5]=0xfffffff0;break;case 21:c->r[4]=0;break;
   }check(lane,lane+2,0);
  }
  for(unsigned offset=0;offset<32;offset++){
   reset();contexts[lane].r[2]=stacks[lane]+STACK_BYTES-512+offset;
   check(lane,lane+2,offset==0);
  }
  reset();check(lane,0,0);reset();check(lane,1,0);reset();check(lane,3-lane,0);
 }
 printf("PASS: %u private clip admission/lifetime/mapping/alias cases; context and memory unchanged\n",cases);
 free(g_xram);free(g_xpt);
}
'''
with tempfile.TemporaryDirectory(prefix='xita-clip-private-') as d:
    d=Path(d);(d/'test.c').write_text(code)
    subprocess.run(['cc','-O1','-g','-fno-strict-aliasing','-fsanitize=address,undefined','-I'+str(root/'recomp'),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
