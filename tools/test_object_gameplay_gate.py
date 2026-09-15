#!/usr/bin/env python3
"""Test the production object-job gate across loading, cameras and frame wrap."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'recomp/kernel/xd3d.c').read_text()
f=re.search(r'int xd3d_object_jobs_ready\(void\)\n\{.*?\n\}',s,re.S)[0]
fixture=r'''
#include "xv_x86rt.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
static struct {unsigned frame;} g_dev;
static unsigned g_vp_frame;
static int xk_file_in_ui_map;
'''+f+r'''
static int step(void) {g_dev.frame++;g_vp_frame=g_dev.frame-1;return xd3d_object_jobs_ready();}
static void settle(void) {assert(!step());assert(!step());assert(step());assert(xd3d_object_jobs_ready());}
int main(void)
{
 g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=4096*i;
 unsigned gg=0x20000;X_M32(0x2F8CA0)=gg;X_M8(gg)=X_M8(gg+1)=1;X_M32(0x271100)=0x11E750;
 assert(!xd3d_object_jobs_ready());settle();
 for(unsigned condition=0;condition<8;condition++) {
  switch(condition) {
   case 0:xk_file_in_ui_map=1;break;
   case 1:X_M32(0x2F8CA0)=0;break;
   case 2:X_M8(gg)=0;break;
   case 3:X_M8(gg+1)=0;break;
   case 4:X_M8(gg+2)=1;break;
   case 5:X_M32(0x2E4000)=1;break;
   case 6:X_M32(0x271100)=0x120A90;break;
   case 7:g_vp_frame=g_dev.frame;break;
  }
  assert(!xd3d_object_jobs_ready());
  xk_file_in_ui_map=0;X_M32(0x2F8CA0)=gg;X_M8(gg)=X_M8(gg+1)=1;X_M8(gg+2)=0;
  X_M32(0x2E4000)=0;X_M32(0x271100)=0x11E750;g_vp_frame=g_dev.frame-1;settle();
 }
 X_M32(0x271100)=0x11DF50;assert(step());
 xk_file_in_ui_map=1;assert(!step());xk_file_in_ui_map=0;g_dev.frame=0xfffffffeu;settle();
 free(g_xram);free(g_xpt);puts("PASS: loading/UI/cinematic/stale-view gates, first-person and vehicle readiness, same-frame calls, reset and frame wrap");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-object-gate-') as d:
 p=Path(d);(p/'gate.c').write_text(fixture)
 subprocess.run([os.environ.get('CC','cc'),'-O2','-std=gnu11','-fno-strict-aliasing','-fsanitize=address,undefined','-I'+str(root/'recomp'),str(p/'gate.c'),'-o',str(p/'gate')],check=True)
 subprocess.run([str(p/'gate')],check=True,timeout=10)
