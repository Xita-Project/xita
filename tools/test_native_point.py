#!/usr/bin/env python3
"""Compare the native point helper against an independent lift of the owned XBE."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
reference=root/'recomp/host/build/original_000B5EA0.c'
if not reference.exists():
    raise SystemExit('Run tools/test_math_hooks.py with the owned XBE first')
with tempfile.TemporaryDirectory(prefix='xita-native-point-') as directory:
    work=Path(directory)
    (work/'original.c').write_text('#include "xv_x86rt.h"\n'+reference.read_text())
    subprocess.run([os.environ.get('CC','cc'),'-O3','-funroll-loops','-std=gnu11',
        '-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections',
        '-I'+str(root/'recomp'),*shlex.split(os.environ.get('NATIVE_MATH_CFLAGS','')),
        str(root/'tools/tests/native_point.c'),str(work/'original.c'),
        str(root/'recomp/kernel/xk_math.c'),str(root/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections','-lm','-o',str(work/'test')],check=True)
    for arguments in ([],['global-disabled'],['point-disabled']):
        subprocess.run([str(work/'test'),*arguments],check=True)
