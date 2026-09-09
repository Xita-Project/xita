#!/usr/bin/env python3
"""Compare the complete bounds helper with an unmodified lift of the local XBE."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
ref = root/'recomp/host/build/original_0005C300.c'
if not ref.exists():
    raise SystemExit('Run tools/gen_native_bounds.py first in the recompiler environment')
with tempfile.TemporaryDirectory(prefix='xita-native-bounds-') as directory:
    d = Path(directory)
    (d/'original.c').write_text('#include "xv_x86rt.h"\n'+ref.read_text())
    subprocess.run([os.environ.get('CC','cc'), '-O2', '-std=gnu11',
        '-fno-strict-aliasing', '-ffp-contract=off', '-ffunction-sections',
        '-fdata-sections', '-I'+str(root/'recomp'),
        *shlex.split(os.environ.get('NATIVE_BOUNDS_CFLAGS','')),
        str(root/'tools/tests/native_bounds.c'), str(d/'original.c'),
        str(root/'recomp/kernel/xk_bounds.c'), str(root/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections', '-lm', '-o', str(d/'test')], check=True)
    for args in ([], ['disabled']):
        subprocess.run([str(d/'test'), *args], check=True)
