#!/usr/bin/env python3
"""Real snapshot retention and benchmark controls for exact grouped loads.

Host comparisons use the portable fallback. Run test_arm_bytes_equal.py
--blocks separately to exercise the actual Vita NEON loads and read bounds.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('VITASDK', str(Path.home()/'vitasdk')))
with tempfile.TemporaryDirectory(prefix='xita-vertex-blocks-') as directory:
    out = Path(directory)
    flags = ['cc', '-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-parameter', '-Wno-unused-function', '-fno-strict-aliasing',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie',
             '-I'+str(ROOT), '-I'+str(ROOT/'runtime'),
             '-idirafter', str(SDK/'arm-vita-eabi/include')]
    for name, source in [('uploads', 'recomp/host/vertex_references_test.c'),
                         ('equality', 'recomp/host/bytes_equal_test.c'),
                         ('controller', 'tools/tests/vertex_blocks_benchmark.c'),
                         ('absent', 'tools/tests/vertex_blocks_benchmark.c')]:
        binary = out/name
        command = flags + (['-DTEST_NO_VERTEX_BLOCKS'] if name=='absent' else [])
        subprocess.run(command+[str(ROOT/source), '-lm', '-o', str(binary)], check=True)
        for mode in (['0', '1'] if name=='uploads' else ['0']):
            subprocess.run([str(binary)], check=True,
                           env=dict(os.environ, XV_VERTEX_BLOCK_LOADS=mode,
                                    ASAN_OPTIONS='detect_leaks=1'))
    print('PASS grouped-load source retention, controls and full-state restoration')
