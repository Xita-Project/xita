#!/usr/bin/env python3
"""Production pthread worker/semaphore/owner-park three-arm acceptance."""
from pathlib import Path
import argparse,os,subprocess,tempfile
from test_native_clip_region import prepare,ROOT
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--no-markers',action='store_true');a=p.parse_args();d=a.out.resolve();d.mkdir(parents=True,exist_ok=True);prepare(d,a.no_markers)
cmd=[os.environ.get('CC','cc'),'-O2','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-pthread','-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_CLIP_REGION','-I'+str(ROOT/'tools/tests'),'-I'+str(ROOT/'recomp')]
if a.no_markers:cmd+=['-DCLIP_TEST_NO_MARKERS']
if a.sanitize:cmd+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
cmd +=[str(q) for q in (d/'reference.c',d/'probe.c',ROOT/'tools/tests/clip_region_workers.c',ROOT/'recomp/kernel/xk_clip.c',ROOT/'recomp/kernel/xk_clip_region.c',ROOT/'recomp/kernel/xk_clip_region_control.c',ROOT/'recomp/xv_x86rt.c')]
cmd+=['-Wl,--gc-sections,--wrap=x_str_movs,--wrap=xv_preempt,--wrap=xv_object_math_lock,--wrap=xv_object_math_unlock,--wrap=pthread_mutex_trylock','-lm','-o',str(d/'workers')]
subprocess.run(cmd,check=True);subprocess.run([str(d/'workers')],check=True,timeout=60)
