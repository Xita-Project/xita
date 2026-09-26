#!/usr/bin/env python3
"""Replay private mesh captures through production CPU vertex preparation.

GXM services are mocked: output correctness only, never Vita FPS or GPU timing.
Cross builds use --cc ARM_GCC --cflags=-static --build-only --output BINARY.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('captures', nargs='*', type=Path, help='mesh files or directories')
    p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    p.add_argument('--cflags', default='')
    p.add_argument('--output', type=Path)
    p.add_argument('--build-only', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--partial', choices=['0', '1'], default='1')
    p.add_argument('--wait-batch', type=int, choices=range(1, 17), default=1)
    p.add_argument('--mode', choices=['all', 'full', 'sparse', 'packed'], default='all')
    a = p.parse_args()
    if a.build_only and not a.output:
        p.error('--build-only requires --output')
    files = set()
    for path in a.captures:
        files.update(path.glob('mesh_*.bin') if path.is_dir() else [path])
    files = sorted(files, key=lambda x: (str(x.parent), tuple(map(int, re.findall(r'\d+', x.stem))), x.name))
    if not a.build_only and not files:
        p.error('provide private mesh captures')
    sdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk'))
    with tempfile.TemporaryDirectory(prefix='xita-mesh-replay-') as tmp:
        binary = a.output.resolve() if a.output else Path(tmp) / 'replay'
        flags = dict(XV_VERTEX_RESIDENT_REFERENCES_DEFAULT=1, XV_PACKED_VERTEX_LAYOUT=1,
                     XV_VERTEX_CAPTURE_PACKED=1, XV_VERTEX_CAPTURE_REUSE=1,
                     XV_CAPTURE_TRUST_TAGS=1, XV_VERTEX_PERSISTENT=0,
                     XV_VERTEX_CAPTURE_READY=1, XV_VERTEX_CAPTURE_NOTIFY=1)
        cmd = [a.cc, '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-parameter', '-Wno-misleading-indentation',
               '-include', str(ROOT / 'recomp/host/neon_x4_compat.h'),
               '-I' + str(ROOT / 'runtime'), '-idirafter', str(sdk / 'arm-vita-eabi/include')]
        cmd += [f'-D{k}={v}' for k, v in flags.items()] + shlex.split(a.cflags)
        if a.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(cmd + [str(ROOT / 'tools/tests/vertex_capture_replay.c'),
                             '-pthread', '-o', str(binary)], check=True)
        if not a.build_only:
            for mode in ['full', 'sparse', 'packed'] if a.mode == 'all' else [a.mode]:
                subprocess.run([str(binary), mode, *map(str, files)], check=True, timeout=120,
                               env={**os.environ, "XV_CAPTURE_PARTIAL_WAIT": a.partial,
                                    "XV_CAPTURE_WAIT_BATCH": str(a.wait_batch)})


if __name__ == '__main__':
    main()
