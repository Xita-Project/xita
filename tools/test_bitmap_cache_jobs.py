#!/usr/bin/env python3
"""Exercise original bitmap-cache wait on the production worker/service pool."""
import argparse,os,re,shlex,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--recomp',type=Path,required=True);args=ap.parse_args()
bodies=[]
for p in args.recomp.glob('code_*.c'):
    m=re.search(r'void f_000325C0\(xctx \*restrict c\)\n\{.*?\n\}',p.read_text(),re.S)
    if m:bodies.append(m[0])
assert len(bodies)==1 and 'XV_OBJECT_MATH_GUARD()' in bodies[0]
with tempfile.TemporaryDirectory(prefix='xita-bitmap-jobs-') as directory:
    d=Path(directory);body=d/'original.c';binary=d/'test'
    body.write_text('#include "kernel/xk_object_jobs.h"\n'+''.join('void f_%08X(xctx *);\n'%a for a in (0x32510,0x13030,0x282B0,0x12AA3))+bodies[0])
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(root/'recomp'),'-ffunction-sections','-fdata-sections',
        *shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),str(body),str(root/'tools/tests/bitmap_cache_jobs.c'),
        str(root/'recomp/kernel/xk_object_jobs.c'),'-pthread','-lm','-Wl,--gc-sections','-o',str(binary)],check=True)
    for workers in ('2','1','0'):
        subprocess.run([str(binary)],env=dict(os.environ,XV_OBJECT_JOB_WORKERS=workers),check=True,timeout=20)
