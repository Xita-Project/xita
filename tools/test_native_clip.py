#!/usr/bin/env python3
"""Differential test of the complete native clipper against an independent lift."""
from pathlib import Path
import os, shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
ref=root/'recomp/host/build/original_000B71C0.c'
if not ref.exists():raise SystemExit('Run tools/gen_native_clip.py --check in the recompiler environment first')
with tempfile.TemporaryDirectory(prefix='xita-native-clip-') as directory:
    d=Path(directory)
    reference=ref.read_text().replace('void f_0001D130(xctx *restrict c)', 'void original_probe(xctx *restrict c)')
    # Match the diagnostic build's XV_FN_BACK after the one direct callee.
    # Its arithmetic/control-flow reference remains the independent lift.
    reference=reference.replace('    f_0001D130(c);', '    f_0001D130(c);\n    if (xv_watch_n) xv_watch_leave(xv_cur_fn, 0xB71C0, c);\n    xv_cur_fn=0xB71C0;')
    (d/'original.c').write_text('#include "xv_x86rt.h"\nextern void f_0001D130(xctx *);\nextern volatile uint32_t xv_cur_fn;\nextern int xv_watch_n;\nextern void xv_watch_leave(uint32_t,uint32_t,xctx *);\n'+reference)
    subprocess.run([os.environ.get('CC','cc'),'-O2','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off',
        '-ffunction-sections','-fdata-sections','-I'+str(root/'recomp'),
        *shlex.split(os.environ.get('NATIVE_CLIP_CFLAGS','')),
        str(root/'tools/tests/native_clip.c'),str(d/'original.c'),
        str(root/'recomp/kernel/xk_clip.c'),str(root/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections','-lm','-o',str(d/'test')],check=True)
    for registers in ('1', '0'):
        for mode in ([],['disabled']):
            subprocess.run([str(d/'test'),*mode],env={**os.environ,'XV_CLIP_REGISTERS':registers},check=True)
