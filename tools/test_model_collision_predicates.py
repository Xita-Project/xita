#!/usr/bin/env python3
"""Compare private 1731D0 lowerings with modeled collision callees.

Checks full context, arena, call observations and preemption counts. This is
bounded synthetic lowering coverage, not real collision or performance proof.
Generated game bodies and compiled artifacts are written only to --output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--headers', type=Path, default=ROOT / 'recomp')
    p.add_argument('--cc', default='cc')
    p.add_argument('--flags', default='')
    p.add_argument('--cases', type=int, default=4096)
    p.add_argument('--build-only', action='store_true')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    sources, hashes = [], []
    for name, path in [('reference', a.reference), ('candidate', a.candidate)]:
        match = re.search(r'^void f_001731D0\(xctx \*restrict c\)\n\{\n.*?\n}\n',
                          path.read_text(), re.M | re.S)
        if not match:
            raise ValueError(f'1731D0 missing: {path}')
        body = match[0]
        calls = set(re.findall(r'f_([0-9A-F]{8})\(c\)', body))
        if calls != {'000B6210', '000B5EA0', '000B5E40', '00088E90'}:
            raise ValueError(f'unmodeled callee set: {calls}')
        hashes.append(hashlib.sha256(body.encode()).hexdigest())
        source = a.output / (name + '.c')
        prefix = '#include "xv_x86rt.h"\n' + ''.join(
            f'void f_{addr}(xctx *restrict c);\n' for addr in sorted(calls))
        source.write_text(prefix + body.replace('void f_001731D0(', f'void {name}('))
        sources.append(str(source))
    cmd = [a.cc, '-std=gnu11', '-O2', '-fno-strict-aliasing', '-ffp-contract=off',
           '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1', '-I' + str(a.headers),
           *shlex.split(a.flags), *sources,
           str(ROOT / 'tools/tests/model_collision_predicates.c'), '-lm',
           '-o', str(a.output / 'test')]
    subprocess.run(cmd, check=True)
    (a.output / 'build.json').write_text(json.dumps(
        dict(command=cmd, body_sha256=hashes), indent=2))
    if not a.build_only:
        result = subprocess.run([str(a.output / 'test'), str(a.cases)],
                                check=True, text=True, capture_output=True, timeout=120)
        (a.output / 'result.txt').write_text(result.stdout)
        print(result.stdout, end='')


if __name__ == '__main__':
    main()
