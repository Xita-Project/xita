#!/usr/bin/env python3
"""Offline experiment: force-inline environment helpers, retaining guest bodies.
Generated code stays in --output. This does not install a game/runtime patch.
"""
import argparse, hashlib, json, re, shlex, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
IDS = ('000571F0', '000580D0', '00057810')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('shard', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cc', default='cc')
    p.add_argument('--extra', default='')
    p.add_argument('--build-only', action='store_true')
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
    text = a.shard.read_text(); bodies = {}
    for name in IDS:
        matches = re.findall(r'^void f_' + name + r'\(xctx \*restrict c\)\n\{\n.*?^}\n', text, re.M | re.S)
        if len(matches) != 1: raise ValueError('ambiguous or missing body: ' + name)
        bodies[name] = matches[0]
    generated = '#include "xv_x87reg.h"\nvoid f_000579D0(xctx *); void f_00057110(xctx *);\n'
    for prefix in ('reference', 'fused'):
        for name in IDS:
            body = bodies[name]
            for other in IDS: body = body.replace('f_'+other, prefix+'_'+other)
            if prefix == 'fused' and name != '00057810':
                body = body.replace('void '+prefix, 'static inline __attribute__((always_inline)) void '+prefix, 1)
            generated += body
    src = a.output/'environment-bodies.c'; src.write_text(generated)
    cmd = [a.cc, '-std=gnu11', '-O2', '-fno-strict-aliasing', '-ffp-contract=off',
           '-I'+str(ROOT/'recomp'), '-I'+str(ROOT/'recomp/kernel'), str(src),
           str(ROOT/'tools/tests/environment_fusion.c'), '-lm', '-o', str(a.output/'environment-test')]
    cmd += shlex.split(a.extra)
    subprocess.run(cmd, check=True)
    (a.output/'build.json').write_text(json.dumps(dict(command=cmd, source_sha256=hashlib.sha256(a.shard.read_bytes()).hexdigest(),
        body_sha256={n:hashlib.sha256(b.encode()).hexdigest() for n,b in bodies.items()}), indent=2)+'\n')
    if not a.build_only: subprocess.run([str(a.output/'environment-test')], check=True)
if __name__ == '__main__': main()
