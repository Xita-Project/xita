#!/usr/bin/env python3
"""Compare native leaf math with independent lifts of the user's local XBE."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
refs=[root/f'recomp/host/build/original_{n}.c' for n in ('000B5B40','000B5F60')]
if not all(p.exists() for p in refs):raise SystemExit('Run tools/test_math_hooks.py in the recompiler Python environment first')
with tempfile.TemporaryDirectory(prefix='xita-native-math-') as directory:
    d=Path(directory)
    source='#include "xv_x86rt.h"\n'+''.join(p.read_text() for p in refs)
    (d/'original.c').write_text(source)
    subprocess.run([os.environ.get('CC','cc'),'-O2','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off',
        '-ffunction-sections','-fdata-sections','-I'+str(root/'recomp'),
        *shlex.split(os.environ.get('NATIVE_MATH_CFLAGS','')),
        str(root/'tools/tests/native_math.c'),str(d/'original.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections','-lm','-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
    subprocess.run([str(d/'test'),'disabled'],check=True)
