#!/usr/bin/env python3
"""Check production string fills against an independent byte-level reference."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-string-fill-') as directory:
    binary = Path(directory) / 'test'
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']):
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', *flags,
                        '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
                        '-I', str(root / 'recomp'), str(root / 'tools/tests/string_fill.c'),
                        str(root / 'recomp/xv_x86rt.c'), '-Wl,--gc-sections', '-lm',
                        '-o', str(binary)], check=True)
        environment = os.environ.copy()
        environment.pop('XV_WATCH_ADDR', None)
        subprocess.run([str(binary)], env=environment, check=True, timeout=120)
