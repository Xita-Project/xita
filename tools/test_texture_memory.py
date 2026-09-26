#!/usr/bin/env python3
"""Exercise production pool allocation/fallback without allocating GPU memory."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'runtime/xv_ui_gxm.c').read_text()
s = s[s.index('static void *ui_gpu_alloc_type'):s.index('/* ---- CPU texture decode')]
with tempfile.TemporaryDirectory(prefix='xita-texture-memory-') as d:
    d = Path(d)
    (d / 'allocator.inc').write_text(s)
    for default in (0, 1):
        exe = d / 'test'
        subprocess.run(['cc', '-D_GNU_SOURCE', f'-DXV_TEXTURE_CDRAM_DEFAULT={default}',
                        '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-no-pie', '-I', str(d),
                        str(root / 'tools/tests/texture_memory.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
