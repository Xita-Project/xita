#!/usr/bin/env python3
"""Actual traced emission fallback, modes/counters and callback state changes."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(ROOT))
from games.halo_ce_3925.clip_region import hook
from test_native_clip_region import MAPPING
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();d=a.out.resolve();d.mkdir(parents=True,exist_ok=True)
raw={int(k):v for k,v in json.loads((ROOT/'recomp/host/build/clip_region_original.json').read_text())['traced'].items()}
# These are the actual protocol macro source literals, not test replacements.
compiler=(ROOT/'recompiler/xita_recomp.py').read_text()
import ast
proto=[]
for line in compiler.splitlines():
    if 'proto.append(' in line and ('#define XV_FN(' in line or '#define XV_FN_BACK(' in line):
        proto.append(ast.literal_eval(line.strip()[len('proto.append('):-1]))
assert len(proto)==2
src='''#include "xv_x86rt.h"
#include "kernel/xk_object_jobs.h"
extern volatile uint32_t xv_cur_fn;
extern int xv_trace_funcs;void xv_trace_func(uint32_t);
extern void f_0001D130(xctx*),f_000117B0(xctx*),f_000B71C0(xctx*);
extern int xv_math_polygon_clip(xctx*);
'''+ '\n'.join(proto)+'\n'+MAPPING
src+=raw[0x1D130]+raw[0x117B0]
src+=raw[0xB71C0].replace('    XV_FN(0x000B71C0u);','    XV_FN(0x000B71C0u);\n    if(xv_math_polygon_clip(c))return;')
src+=raw[0xB7F10].replace('f_000B7F10(', 'baseline(')
src+=hook(raw[0xB7F10]).replace('f_000B7F10(', 'candidate(')
(d/'diagnostics.c').write_text(src)
cmd=[os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-pthread','-ffunction-sections','-fdata-sections','-DXV_NATIVE_CLIP_REGION','-I'+str(ROOT/'recomp'),'-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
cmd +=[str(q) for q in (d/'diagnostics.c',ROOT/'tools/tests/clip_region_diagnostics.c',ROOT/'recomp/kernel/xk_clip.c',ROOT/'recomp/kernel/xk_clip_region.c',ROOT/'recomp/kernel/xk_clip_region_control.c',ROOT/'recomp/xv_x86rt.c')]
cmd+=['-Wl,--gc-sections','-lm','-o',str(d/'diagnostics')]
subprocess.run(cmd,check=True)
for mode in ('0','1'):
 subprocess.run([str(d/'diagnostics')],check=True,env=dict(os.environ,XV_NATIVE_CLIP=mode,ASAN_OPTIONS='detect_leaks=0'),timeout=30)
