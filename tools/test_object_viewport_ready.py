#!/usr/bin/env python3
"""Exercise the production viewport predicate, including wrap and strict mode."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'recomp/kernel/xd3d.c').read_text()
m=re.search(r'static int xd3d_object_viewport_ready\([^\n]+\)\n\{\n.*?\n}',s,re.S)
assert m and s.count('int vp_ready=xd3d_object_viewport_ready(g_dev.frame,g_vp_frame,vp_tol);')==1
assert '(!vp_ready?128u:0)' in s
ready=re.search(r'^int xd3d_object_jobs_ready\(void\)\n\{\n.*?^}\n',s,re.M|re.S)[0]
checks=r'''
int main(void){
 const uint32_t frames[]={0,1,2,90,0x7fffffffu,0xfffffffeu,0xffffffffu};
 for(unsigned i=0;i<sizeof frames/sizeof *frames;i++) {
  uint32_t f=frames[i];
  for(unsigned d=0;d<512;d++) {
   assert(xd3d_object_viewport_ready(f,f-d,0)==(d==1));
   assert(xd3d_object_viewport_ready(f,f-d,1)==(d<=1));
  }
  assert(!xd3d_object_viewport_ready(f,f+1,0));
  assert(!xd3d_object_viewport_ready(f,f+1,1));
 }
 /* The prior expression rejected even the intended strict frame-1 case. */
 uint32_t lag=1,allowed=0;if(lag!=1)lag=2;assert(lag>allowed);
 assert(xd3d_object_viewport_ready(91,90,0));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text('#include <stdint.h>\n#include <assert.h>\n'+m[0]+checks)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
integration = r"""
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
static struct { uint32_t frame; } g_dev;
static uint32_t g_vp_frame, gg=0x1000, mode=0x11E750, blocked;
static unsigned char flags[3]={1,1,0};
static int xk_file_in_ui_map, tolerant;
static uint32_t read32(uint32_t a) {
 if(a==0x2F8CA0u)return gg;if(a==0x271100u)return mode;
 assert(a==0x2E4000u);return blocked;
}
static unsigned read8(uint32_t a){assert(a>=gg&&a<gg+3);return flags[a-gg];}
static char *config(const char *s){
 if(!strcmp(s,"XV_OBJECT_JOBS_VP_TOLERANT"))return tolerant?"1":"0";
 if(!strcmp(s,"XV_OBJECT_JOBS_STABLE"))return "90";
 return 0;
}
#define X_M32(a) read32(a)
#define X_M8(a) read8(a)
#define D3DLOG(...) ((void)0)
#define getenv config
"""
exercise = r"""
static void settle(uint32_t first,unsigned lag){
 for(unsigned i=0;i<90;i++){
  g_dev.frame=first+i;g_vp_frame=g_dev.frame-lag;
  assert(xd3d_object_jobs_ready()==(i==89));
  assert(xd3d_object_jobs_ready()==(i==89)); /* duplicate tick adds no frame */
 }
}
int main(int argc,char**argv){
 tolerant=argc>1?atoi(argv[1]):0;settle(100,tolerant?0:1);
 for(unsigned reason=0;reason<9;reason++){
  ++g_dev.frame;g_vp_frame=g_dev.frame-1;
  switch(reason){
   case 0:xk_file_in_ui_map=1;break;case 1:gg=0;break;
   case 2:flags[0]=0;break;case 3:flags[1]=0;break;case 4:flags[2]=1;break;
   case 5:blocked=1;break;case 6:mode=0x120A90;break;
   case 7:g_vp_frame=g_dev.frame-2;break;case 8:g_vp_frame=g_dev.frame+1;break;
  }
  assert(!xd3d_object_jobs_ready());
  xk_file_in_ui_map=0;gg=0x1000;flags[0]=flags[1]=1;flags[2]=0;blocked=0;mode=0x11DF50;
  settle(g_dev.frame+1,tolerant?0:1);
 }
 ++g_dev.frame;g_vp_frame=g_dev.frame;
 assert(xd3d_object_jobs_ready()==tolerant);
 xk_file_in_ui_map=1;g_dev.frame=0xffffffc0u;assert(!xd3d_object_jobs_ready());
 xk_file_in_ui_map=0;settle(0xffffffc1u,1);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text('#include <string.h>\n'+integration+m[0]+ready+exercise)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(p/'test.c'),'-o',str(p/'test')],check=True)
 for mode in ('0','1'):subprocess.run([str(p/'test'),mode],check=True)
print('PASS production predicate and readiness: both modes, 90-frame stabilization, repeated ticks, all reset gates, frame wrap')
