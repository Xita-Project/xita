#!/usr/bin/env python3
"""Compare 80720 memory/register lowering with synthetic callee contracts.
Generated bodies stay outside the repository. This validates lowering, NOT the
real collision routines or gameplay. Supply baseline and register shard paths.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def extract(path):
    text = path.read_text()
    match = re.search(r'^void f_00080720\(xctx \*restrict c\)\n\{\n.*?\n}\n', text, re.M | re.S)
    if not match:
        raise ValueError('80720 body missing')
    return match[0]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('registers', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cc', default='cc')
    p.add_argument('--extra', default='')
    p.add_argument('--build-only', action='store_true')
    p.add_argument('--cases', type=int, default=1000)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    bodies = [extract(a.baseline), extract(a.registers)]
    if 'xfsp0' in bodies[0] or 'xfsp0' not in bodies[1]:
        raise ValueError('expected memory baseline and register candidate')
    src = a.output/'particle-bodies.c'
    src.write_text('#include "xv_x87reg.h"\n'
                   'void f_000571F0(xctx *); void f_00057810(xctx *); void f_001721B0(xctx *);\n' +
                   bodies[0].replace('void f_00080720(', 'void particle_reference(') +
                   bodies[1].replace('void f_00080720(', 'void particle_candidate('))
    cmd = [a.cc, '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-ffp-contract=off',
           '-I'+str(ROOT/'recomp'), '-I'+str(ROOT/'recomp/kernel'),
           str(src), str(ROOT/'tools/tests/particle_registers.c'), '-lm', '-o', str(a.output/'particle-test')]
    cmd += shlex.split(a.extra)
    subprocess.run(cmd, check=True)
    (a.output/'build.json').write_text(json.dumps(dict(command=cmd,
        body_sha256=[hashlib.sha256(b.encode()).hexdigest() for b in bodies]), indent=2))
    if not a.build_only:
        subprocess.run([str(a.output/'particle-test'), str(a.cases)], check=True)

if __name__ == '__main__':
    main()
