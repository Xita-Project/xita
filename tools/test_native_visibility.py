#!/usr/bin/env python3
"""Differential test of the native BSP subcluster visibility pass (recomp/kernel/xk_native_visibility.c) against the
lifted guest bodies f_00052E10 + f_0005C300 of a stage (generated code is not in the repository).

  tools/test_native_visibility.py <stage>/recomp [cases] [--cc CC] [--seed N]

The stage's f_00052E10 must carry the entry hook (tools/patch_native_visibility_hooks.py). Builds
tools/tests/native_visibility.c three ways - plain page table, host per-thread table + render view (image globals
through the page table, as the Vita scene helper), and -O0 - and runs the randomized cases in each."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def body(src, fn):
    m = re.search(r'^void %s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: sys.exit(f'{fn} not found')
    return src[m.start():src.index('\n}\n', m.end()) + 3]
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    a = ap.parse_args(); rec = Path(a.recomp)
    shard = next((p for p in sorted(rec.glob('code_*.c')) if re.search(r'^void f_00052E10\(xctx', p.read_text(errors='replace'), re.M)), None)
    if not shard: sys.exit('no shard defines f_00052E10')
    s52 = shard.read_text(errors='replace')
    if 'xv_native_visibility(c)' not in body(s52, 'f_00052E10'): sys.exit('f_00052E10 has no XV_NATIVE_VISIBILITY hook')
    s5c = next(p.read_text(errors='replace') for p in sorted(rec.glob('code_*.c')) if re.search(r'^void f_0005C300\(xctx', p.read_text(errors='replace'), re.M))
    preamble = s52[:s52.index('\nvoid f_')]
    guest = ('#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + preamble + '\n' +
             body(s52, 'f_00052E10') + '\n' + body(s5c, 'f_0005C300'))
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-visibility-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest)
        for name, flags in variants.items():
            exe = d / 'test'
            cmd = [a.cc, *flags, '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_VISIBILITY=1', '-I' + str(rec), '-I' + str(rec / 'kernel'),
                   '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'), str(ROOT / 'tools/tests/native_visibility.c'), str(d / 'guest.c'),
                   str(ROOT / 'recomp/kernel/xk_native_visibility.c'), '-lm', '-o', str(exe)]
            subprocess.run(cmd, check=True)
            r = subprocess.run([str(exe), a.cases, a.seed], capture_output=True, text=True)
            print(f'[{name}] ' + r.stdout.strip().replace('\n', f'\n[{name}] '))
            rc |= r.returncode
    sys.exit(rc)
if __name__ == '__main__': main()
