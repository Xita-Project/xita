#!/usr/bin/env python3
"""Build the whole-game host harness (recomp/host/harness.c) with the SAME per-unit feature defines the Vita
build uses, on Linux x86-64 or on an ARM Linux board (Raspberry Pi) as a CPU-side dev kit.

  1. In the retained stage: make -n -B build/xita.elf <same args as build-command.json> > make-n.txt
     (the stage's Makefile needs the bin/rg shim on PATH)
  2. tools/host_build.py --stage <stage>/build --commands make-n.txt --out <objdir> [--cc gcc]

Units: every `-c recomp/...` command from the dry run (generated code, kernel, native helpers) plus
recomp/host/{harness,trace,softgfx}.c. Vita-only flags (-mthumb/-mcpu/-mfpu, Vita-specific includes)
are dropped; XV_NATIVE_MATRIX_NEON is kept only for an ARM compiler. Links with -lm -lpthread.
No timing claim: absolute frame times on a host are not Vita numbers."""
import argparse, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

DROP_PREFIX = ('-mthumb', '-mcpu=', '-mfpu=', '-mfloat-abi', '-MMD', '-MP', '-MF', '-o')
HOST_FLAGS = ['-O1', '-g0', '-w', '-std=gnu11', '-fno-strict-aliasing', '-pthread']

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', required=True); ap.add_argument('--commands', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--cc', default='gcc'); ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    stage = Path(a.stage).resolve(); out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
    arm = 'arm' in a.cc or 'aarch' in a.cc or (a.cc == 'gcc' and os.uname().machine.startswith(('arm', 'aarch')))
    units = {}   # src -> flags
    for line in Path(a.commands).read_text(errors='replace').splitlines():
        if not line.startswith('arm-vita-eabi-gcc') or ' -c ' not in line: continue
        toks = shlex.split(line)
        src = toks[toks.index('-c') + 1]
        if not src.startswith('recomp/'): continue           # runtime/ and dashboard/ are Vita GXM/UI
        flags = []
        skip = False
        for t in toks[1:]:
            if skip: skip = False; continue
            if t in ('-c', '-o', '-MF'): skip = True; continue
            if t.startswith(DROP_PREFIX) or t == src: continue
            if t.startswith('-O'): continue
            if t == '-include': skip = False; flags.append(t); continue
            if t.startswith('-DXV_NATIVE_MATRIX_NEON') and not arm: continue
            if t.startswith('-D') or t.startswith('-I') or t.startswith('-std') or t.startswith('-f') or flags and flags[-1] == '-include':
                flags.append(t)
        units[src] = flags
    kernel_flags = units.get('recomp/kernel/xk_object_jobs.c') or units.get('recomp/kernel/xk_mem.c') or []
    for h in ('harness', 'trace', 'softgfx'):
        units[f'recomp/host/{h}.c'] = [f for f in kernel_flags]
    print(f'{len(units)} units, arm={arm}, cc={a.cc}', flush=True)
    def compile_one(item):
        src, flags = item
        obj = out / (Path(src).stem + '.o'); s = stage / src
        if obj.exists() and obj.stat().st_mtime > s.stat().st_mtime: return (src, 0, '')
        cmd = [a.cc] + HOST_FLAGS + flags + ['-c', str(s), '-o', str(obj)]
        r = subprocess.run(cmd, cwd=stage, capture_output=True, text=True)
        return (src, r.returncode, r.stderr[-1500:])
    failed = 0
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, rc, err in ex.map(compile_one, sorted(units.items())):
            if rc: failed += 1; print(f'FAIL {src}\n{err}', flush=True)
    if failed: print(f'{failed} units failed'); return 1
    objs = sorted(str(p) for p in out.glob('*.o'))
    r = subprocess.run([a.cc, '-pthread'] + objs + ['-o', str(out / 'harness'), '-lm', '-lpthread'], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-6000:]); return 1
    print('built', out / 'harness'); return 0

if __name__ == '__main__': sys.exit(main())
