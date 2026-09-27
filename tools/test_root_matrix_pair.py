#!/usr/bin/env python3
"""Build/run synthetic root-pair checks; use ARM for exact NaN payload checks."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',required=True)
p.add_argument('--cflags',default='')
p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--build-only',action='store_true')
a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
binary=out/'root-matrix-pair-test'
cmd=[a.cc,'-O3','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off',
     '-frounding-math','-ffunction-sections','-fdata-sections',*shlex.split(a.cflags),
     '-I'+str(ROOT/'recomp'),str(ROOT/'tools/tests/root_matrix_pair.c'),
     str(ROOT/'recomp/kernel/xk_math.c'),str(ROOT/'recomp/xv_x86rt.c'),
     '-Wl,--gc-sections','-lm','-o',str(binary)]
(out/'build-command.json').write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,check=True)
if not a.build_only:
    for mode in ('enabled','unset','phase','scene','math-off'):
        subprocess.run([str(binary),mode],check=True)
print(binary)
