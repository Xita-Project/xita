#!/usr/bin/env python3
"""Check private clipping against owned-image x86 output; not an FPS benchmark."""
from pathlib import Path
import argparse,sys,subprocess
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from tools.gen_native_clip_region import generate
from games.halo_ce_3925.clip_region import hook
p=argparse.ArgumentParser();p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--workers',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
native,raw,_=generate(a.xbe,a.manifest)
(a.out/'region.c').write_text(native)
(a.out/'held.c').write_text(native.replace('#if XV_CLIP_PRIVATE', '#if 0').replace('int xv_math_clip_region(', 'int xv_math_clip_region_held('))
(a.out/'reference.c').write_text('#include "xv_x86rt.h"\nint xv_math_clip_region(xctx*,int),xv_math_clip_region_held(xctx*,int);\nvoid f_0001D130(xctx*),f_000117B0(xctx*),f_000B71C0(xctx*);\n'+'\n'.join(raw.values())+'\n'+hook(raw[0xB7F10]).replace('f_000B7F10(', 'full_candidate(')+'\n'+hook(raw[0xB7F10]).replace('f_000B7F10(', 'full_held(').replace('xv_math_clip_region(', 'xv_math_clip_region_held('))
s=(root/'recomp/kernel/xk_object_jobs.c').read_text()
def function(signature):
 b=s.index(signature);e=s.index('{',b)+1;depth=1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[b:e]+'\n'
(a.out/'admission.h').write_text(function('static int private_stack_span(unsigned lane,uint32_t address,unsigned bytes)\n{')+function('void xv_object_clip_release('))
cmd=['cc','-pthread','-O1','-g1','-fno-strict-aliasing','-ffp-contract=off','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_CLIP_PRIVATE=1','-DXV_NATIVE_CLIP_REGION','-I'+str(root/'recomp'),'-I'+str(a.out.resolve()),str(root/('tools/tests/clip_private_workers.c' if a.workers else 'tools/tests/clip_private.c')),str(a.out/'reference.c'),str(a.out/'region.c'),str(a.out/'held.c'),str(root/'recomp/xv_x86rt.c'),'-Wl,--gc-sections,--wrap=xv_preempt','-lm','-o',str(a.out/'check')]
subprocess.run(cmd,check=True);subprocess.run([str(a.out/'check')],check=True,timeout=90)
