#!/usr/bin/env python3
"""Compare 862A0 candidates with synthetic callee contracts.
Generated bodies stay outside the repository. This validates lowering, NOT the
real object-collision callees or gameplay. Comparison is bit-exact, including inactive flag backing storage.
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
    match = re.search(r'^void f_000862A0\(xctx \*restrict c\)\n\{\n.*?\n}\n', text, re.M | re.S)
    if not match:
        raise ValueError('862A0 body missing')
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
    p.add_argument('--plain-candidate', action='store_true',
                   help='accept a non-register-lowered experimental body')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    bodies = [extract(a.baseline), extract(a.registers)]
    if 'xfsp0' in bodies[0] or (not a.plain_candidate and 'xfsp0' not in bodies[1]):
        raise ValueError('expected memory baseline and register candidate')
    # Bound synthetic paths without changing arithmetic or guest state.
    bodies = [re.sub(r'^([LM]_([0-9A-F]{8})(?:_\d+)?):',
                     lambda m: m[0]+' contact_capsule_step(0x'+m[2]+'u);', body,
                     flags=re.M) for body in bodies]
    src = a.output/'contact-capsule-bodies.c' 
    calls = sorted(set(re.findall(r'f_([0-9A-F]{8})\(c\)', bodies[0])))
    src.write_text('#include "xv_x87reg.h"\nvoid contact_capsule_step(unsigned);\n' +
                   ''.join('void f_'+addr+'(xctx *);\n' for addr in calls) +
                   bodies[0].replace('void f_000862A0(', 'void contact_capsule_reference(') +
                   bodies[1].replace('void f_000862A0(', 'void contact_capsule_candidate('))
    cmd = [a.cc, '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-ffp-contract=off',
           '-I'+str(ROOT/'recomp'), '-I'+str(ROOT/'recomp/kernel'),
           str(src), str(ROOT/'tools/tests/contact_capsule_registers.c'), '-lm', '-o', str(a.output/'contact-capsule-test')]
    cmd += shlex.split(a.extra)
    subprocess.run(cmd, check=True)
    (a.output/'build.json').write_text(json.dumps(dict(command=cmd,
        body_sha256=[hashlib.sha256(b.encode()).hexdigest() for b in bodies],
        plain_candidate=a.plain_candidate), indent=2))
    if not a.build_only:
        subprocess.run([str(a.output/'contact-capsule-test'), str(a.cases)], check=True, timeout=60)

if __name__ == '__main__':
    main()
