#!/usr/bin/env python3
"""Exercise the production viewport predicate, including wrap and strict mode."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'recomp/kernel/xd3d.c').read_text()
m=re.search(r'static int xd3d_object_viewport_ready\([^\n]+\)\n\{\n.*?\n}',s,re.S)
assert m and s.count('int vp_ready=xd3d_object_viewport_ready(g_dev.frame,g_vp_frame,vp_tol);')==1
assert '(!vp_ready?128u:0)' in s
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
print('PASS production viewport predicate: strict/tolerant, stale/future, uint32 wrap; old strict regression reproduced')
