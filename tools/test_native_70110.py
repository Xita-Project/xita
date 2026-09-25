#!/usr/bin/env python3
"""Differential test of the native material setup (recomp/kernel/xk_native_70110.c) against the lifted guest body of a
stage (generated code is not in the repository).

  tools/test_native_70110.py <stage>/recomp [cases] [--seed N] [--cc CC] [--variants a,b] [--mutants] [--no-verify]
                             [--bench N] [--threads N --iters K] [--keep FILE --cflags ...]

Extracts f_00070110 and the shard's preamble, patches it like the stage (tools/patch_native_70110_hooks.py: the wrapper,
the renamed body, the tapped copy), links it with tools/tests/native_70110.c (deterministic stand-ins for every callee,
HLE and memo) and the native, and runs the randomized cases in several builds: plain, with the lift's optional hooks
(XV_MODEL_UV, XV_MODEL_FOG, XV_NATIVE_MATERIAL_SAMPLER), with the per-thread page table and the render view, with object
jobs, at -O1 (the host harness) and -O0. --mutants builds deliberately broken copies of the native and expects each to
be caught."""
import argparse, os, re, subprocess, sys, tempfile, importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('p70', ROOT / 'tools/patch_native_70110_hooks.py'); P = importlib.util.module_from_spec(spec); spec.loader.exec_module(P)
HOOKS = ['-DXV_MODEL_UV=1', '-DXV_MODEL_FOG=1', '-DXV_NATIVE_MATERIAL_SAMPLER']
VARIANTS = {
    'plain -O2': ['-O2'],
    'hooks -O2': ['-O2', *HOOKS],
    'hooks+thread-table+render-view -O2': ['-O2', *HOOKS, '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
    'hooks+thread-table -O2': ['-O2', *HOOKS, '-DXV_THREAD_PAGE_TABLE=1'],
    'hooks+object-jobs -O2': ['-O2', *HOOKS, '-DXV_EXPERIMENTAL_OBJECT_JOBS'],
    'hooks -O1': ['-O1', *HOOKS],
    'hooks -O0': ['-O0', *HOOKS],
}
# (description, old, new): each must match exactly once in the native; the test must report mismatches.
MUTANTS = []

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='2000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--variants', default=''); ap.add_argument('--mutants', action='store_true'); ap.add_argument('--no-verify', action='store_true')
    ap.add_argument('--native', default=str(ROOT / 'recomp/kernel/xk_native_70110.c'))
    ap.add_argument('--mutant-filter', default='')
    ap.add_argument('--bench', type=int, default=0, help='time guest and native on game-like scenes (repetitions)')
    ap.add_argument('--threads', type=int, default=0); ap.add_argument('--iters', type=int, default=20)
    ap.add_argument('--keep', default='', help='copy the (first variant) test binary here and do not run it (cross builds: --cc, --cflags)')
    ap.add_argument('--cflags', default='')
    ap.add_argument('--guest-cflags', default='', help='extra flags for the lifted body only (e.g. -Os: the Vita builds code_011 -Os)')
    ap.add_argument('--native-cflags', default='', help='extra flags for the native only')
    a = ap.parse_args(); rec = Path(a.recomp).resolve()
    src = next((p.read_text(errors='replace') for p in sorted(rec.glob('code_*.c')) if re.search(r'^void f_00070110\(xctx', p.read_text(errors='replace'), re.M)), None)
    if src is None: sys.exit('no shard defines f_00070110')
    m = re.search(r'^void f_00070110\(xctx \*restrict c\)\n\{\n', src, re.M)
    body = src[m.start():src.index('\n}\n', m.end()) + 3]
    if 'xv_native_70110' in src[m.start() - 2000:m.start()]:
        pass   # a patched stage: the body is the translated one either way
    preamble = src[:src.index('\nvoid f_')]
    vbody, nsites = P.tapped(body)
    guest = ('#include "xv_x86rt.h"\n#include "xv_phase.h"\n#ifndef XV_EXPERIMENTAL_OBJECT_JOBS\n#define XV_HLE_PROXY(fn) 0   /* the protos define it only with object jobs */\n#endif\n'
             + preamble + '\n' + P.PRE + body + P.VPRE + vbody + P.VPOST)
    kernel = rec / 'kernel'
    variants = {k: v for k, v in VARIANTS.items() if not a.variants or any(x in k for x in a.variants.split(','))}
    native_src = Path(a.native).read_text()
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-70110-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest)
        def build(flags, native_path, exe):
            common = [*flags, *a.cflags.split(), '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_70110=1',
                      '-I' + str(rec), '-I' + str(kernel)]
            objs = []
            for src, extra_flags in ((ROOT / 'tools/tests/native_70110.c', []), (d / 'guest.c', a.guest_cflags.split()), (native_path, a.native_cflags.split())):
                o = d / (Path(src).stem + '.o'); subprocess.run([a.cc, *common, *extra_flags, '-c', str(src), '-o', str(o)], check=True); objs.append(str(o))
            subprocess.run([a.cc, *a.cflags.split(), *objs, '-lm', '-lpthread', '-o', str(exe)], check=True)
        def run(exe, cases, seed, extra=()):
            r = subprocess.run([str(exe), str(cases), str(seed), *extra], capture_output=True, text=True)
            return r.returncode, (r.stdout + r.stderr).strip()
        (d / 'native.c').write_text(native_src)
        extra = ['--no-verify'] if a.no_verify else []
        if a.bench: extra = ['--bench', str(a.bench)]
        if a.threads: extra = ['--threads', str(a.threads), str(a.iters)]
        print(f'guest body: {nsites} sites tapped', flush=True)
        for name, flags in variants.items():
            exe = d / 'test'; build(flags, d / 'native.c', exe)
            if a.keep:
                import shutil; shutil.copy(exe, a.keep); print(f'[{name}] built {a.keep}'); break
            code, out = run(exe, a.cases, a.seed, extra)
            print(f'[{name}] ' + out.replace('\n', f'\n[{name}] '), flush=True)
            rc |= code
        if a.mutants:
            caught = 0
            mutants = [x for x in MUTANTS if a.mutant_filter in x[0]]
            for desc, old, new in mutants:
                if native_src.count(old) != 1: print(f'[mutant] {desc}: pattern matched {native_src.count(old)} times'); rc |= 1; continue
                (d / 'mutant.c').write_text(native_src.replace(old, new))
                exe = d / 'mutant'
                build(['-O2', *HOOKS], d / 'mutant.c', exe)
                code, out = run(exe, a.cases, a.seed, ['--no-verify'])
                mm = re.search(r'(\d+) mismatches', out); n = int(mm.group(1)) if mm else -1
                crashed = code not in (0, 1)
                caught += n > 0 or crashed
                print(f'[mutant] {desc}: {n} of {a.cases} cases mismatch' + (' (crashed)' if crashed else '') + ('' if n > 0 or crashed else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught}/{len(mutants)} caught')
            if caught != len(mutants): rc |= 1
    sys.exit(rc)

if __name__ == '__main__': main()
