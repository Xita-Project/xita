#!/usr/bin/env python3
"""Differential and cooperative-lifetime tests for deferred Halo flare results."""
from pathlib import Path
import argparse,os,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=root/'recomp');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
s=(args.source/'code_011.c').read_text();a=s.index('void f_00060460(');b=s.index('\nvoid f_',a+1);body=s[a:b]
sys.path.insert(0,str(root));from recompiler.halo_flare_hooks import ENTRY_HOOK
body=body.replace(ENTRY_HOOK+'\n','')
hle=(args.source/'kernel/xd3d.c').read_text()
def hle_function(name):
 a=hle.index('void xv_hle_D3DDevice_'+name+'(xctx *c)');b=hle.index('{',a);depth=1;e=b+1
 while depth:
  if hle[e]=='{':depth+=1
  elif hle[e]=='}':depth-=1
  e+=1
 return hle[a:e]+'\n'
present=hle_function('Present')
assert present.index('xv_flare_barrier(XV_FLARE_PRESENT)')<present.index('lockstep_init()')<present.index('xd3d_r_present(')
hle_prefix='''
#include "kernel/xk_flare.h"
#define XD3D_COUNT(name) ((void)0)
static struct {unsigned vp_w,vp_h;} xd3d_state;
static struct {unsigned frame,draws,clears;} g_dev;
static int visibility_retry_valid;
extern void xd3d_r_visibility_begin(uint32_t,uint32_t);
extern uint32_t xd3d_r_visibility_end(uint32_t);
extern void xd3d_r_present(uint32_t,uint32_t);
extern void xk_yield(void);
'''
body+=hle_prefix+''.join(hle_function(n) for n in ('BeginVisibilityTest','EndVisibilityTest','Swap'))
with tempfile.TemporaryDirectory(prefix='xita-flare-defer-test-') as directory:
 d=Path(directory);(d/'original.c').write_text('#include "xv_x86rt.h"\n#define XV_FN(a) ((void)0)\n#define XV_FN_BACK(a) ((void)0)\nextern void f_00063460(xctx *);\n'+body)
 cmd=[os.environ.get('CC','cc'),'-O1','-g','-std=gnu11','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-I'+str(root/'recomp'),'-I'+str(root),'-I'+str(root/'runtime'),str(root/'tools/tests/flare_defer.c'),str(d/'original.c'),str(root/'recomp/kernel/xk_flare.c'),str(root/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(d/'test')]
 if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined']
 subprocess.run(cmd,check=True)
 env=dict(os.environ);env.pop('XV_FLARE_DEFER',None);env['XV_VIS_STALE']='0'
 for configured in [{},{'XV_FLARE_DEFER':'0'},{'XV_FLARE_DEFER':'1'}]:
  for extra in [{},{'XV_VISIBILITY_EVENTS':'0'},{'XV_VISIBILITY_POLL_US':'0'},{'XV_VISIBILITY_BACKOFF':'0'}]:
   subprocess.run([str(d/'test')],env={**env,**configured,**extra},check=True)
 subprocess.run([str(d/'test'),'stale'],env={**env,'XV_VIS_STALE':'1'},check=True)
