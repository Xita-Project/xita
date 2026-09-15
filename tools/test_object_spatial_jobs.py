#!/usr/bin/env python3
"""Stress the emitted original object lists; --recomp is private generated code."""
from pathlib import Path
import argparse,os,re,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--recomp',type=Path,required=True);a=p.parse_args()
addresses=(0x565e0,0x56670,0xa92c0,0xa9330);bodies={}
for f in a.recomp.glob('code_*.c'):
    text=f.read_text()
    for address in addresses:
        m=re.search(r'void f_%08X\(xctx \*restrict c\)\n\{.*?\n\}'%address,text,re.S)
        if m:
            assert address not in bodies
            assert 'XV_OBJECT_MATH_GUARD(); /* shared list/datum transaction */' in m[0]
            bodies[address]=m[0]
assert set(bodies)==set(addresses)
with tempfile.TemporaryDirectory(prefix='xita-spatial-jobs-') as directory:
    d=Path(directory);source=d/'original.c';binary=d/'test'
    source.write_text('#include "kernel/xk_object_jobs.h"\n'+''.join('void f_%08X(xctx *);\n'%n for n in (*addresses,0x52240))+'\n'.join(bodies.values()))
    subprocess.run([os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-fno-strict-aliasing',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(root/'recomp'),'-ffp-contract=off',
        '-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('OBJECT_JOB_TEST_FLAGS','')),
        str(source),str(root/'tools/tests/object_spatial_jobs.c'),str(root/'recomp/kernel/xk_object_jobs.c'),
        str(root/'recomp/xv_x86rt.c'),'-pthread','-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=45)
