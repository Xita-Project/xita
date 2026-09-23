#!/usr/bin/env python3
"""Build a Halo 2 stage as a headless Linux harness (x86 for iteration, static armhf for the Raspberry Pi bench).

  tools/h2_host_build.py --stage <private stage dir> --out <objdir> [--cc gcc] [--m32] [--static] [--jobs 3]

The stage is the directory build_stage.py uses (build-args.json + boot/generated + boot/halo2_image.bin).
The units and their flags come from the stage's own Makefile dry run (`make -n -B ... halo2-boot.elf` with
build-args.json), so every feature define, GUEST_OPT and --wrap matches the Vita build. Changes:
  - recomp/kernel/xk_os_vita.c -> recomp/kernel/xk_os_host.c (POSIX files, ucontext fibers)
  - runtime/xv_cpu.c dropped (Vita CPU-usage sampling; nothing in the H2 target calls it)
  - games/halo2_5849/host/psp2_host.c (the sce* calls: pthreads, POSIX io, paced vblank/audio, null GXM)
    and host/sampler.c (XV_HOST_SAMPLE in-process profiler) added
  - the vitasdk psp2 headers are reached through <out>/psp2inc (symlinks into $VITASDK/arm-vita-eabi/include),
    after the project's own include paths; no vitasdk library is linked
  - Vita CPU flags (-mthumb -mcpu=cortex-a9 -mfpu=neon) are kept for an ARM compiler (plus -mfloat-abi=hard),
    dropped for x86. Generated guest code gets -g0 -w; runtime units get -g and the compiler's default warnings
    (pointer/integer size casts show up there on a 64-bit host; build.log keeps them).
Objects rebuild when their source or any header named in their .d file is newer. Every compile runs under
nice -n 10. Output: <out>/harness (+ build.log). Timing on a host is never a Vita number."""
import argparse, json, os, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SOURCE = Path(__file__).resolve().parent.parent
GAME = SOURCE / 'games/halo2_5849'
SKIP = {'recomp/kernel/xk_os_vita.c', 'runtime/xv_cpu.c'}
EXTRA = ['recomp/kernel/xk_os_host.c', 'games/halo2_5849/host/psp2_host.c', 'games/halo2_5849/host/sampler.c']
VITA_CPU = ('-mthumb', '-mcpu=', '-mfpu=')


def dry_run(stage, out):
    args = json.loads((stage / 'build-args.json').read_text())
    dry = out / 'dry'
    assert args[0] == 'make', args[:3]
    cmd = ['make', '-n', '-B']                              # print the commands, never run them
    for a in args[1:]:
        if a.startswith('-j'): continue
        if a.startswith('BUILD='): a = f'BUILD={dry}'
        cmd.append(a)
    cmd.append(str(dry / 'halo2-boot.elf'))
    r = subprocess.run(cmd, cwd=SOURCE, capture_output=True, text=True)
    if r.returncode: sys.exit(f'make -n failed:\n{r.stderr[-3000:]}')
    return r.stdout


def parse(text, arm):
    units, wraps = {}, []
    for line in text.splitlines():
        toks = shlex.split(line) if 'arm-vita-eabi-gcc' in line else []
        if not toks or not toks[0].endswith('arm-vita-eabi-gcc'): continue
        if '-c' not in toks:
            if any(t.endswith('halo2-boot.elf') for t in toks): wraps = [t for t in toks if t.startswith('-Wl,--wrap=')]
            continue
        src = toks[toks.index('-c') + 1]
        obj = Path(toks[toks.index('-o') + 1]).name
        path = Path(src) if os.path.isabs(src) else GAME / src
        rel = os.path.relpath(path, SOURCE)
        if rel in SKIP: continue
        flags, skip = [], False
        for t in toks[1:]:
            if skip: skip = False; continue
            if t in ('-c', '-o', '-MF'): skip = True; continue
            if t in ('-MMD', '-MP') or t == src: continue
            if t.startswith(VITA_CPU):
                if arm: flags.append(t)
                continue
            if t.startswith(('-D', '-I', '-O', '-std', '-f')): flags.append(t)
        units[obj] = (path, flags)
    return units, wraps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--cc', default='gcc'); ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--m32', action='store_true', help='x86: build 32-bit (same pointer size as the Vita and the Pi)')
    ap.add_argument('--static', action='store_true', help='link statically (cross builds for the Pi)')
    ap.add_argument('--runtime-only', action='store_true', help='compile only the non-generated units (no link): quick check')
    a = ap.parse_args()
    stage, out = Path(a.stage).resolve(), Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    vitasdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk')) / 'arm-vita-eabi/include'
    inc = out / 'psp2inc'; inc.mkdir(exist_ok=True)
    for d in ('psp2', 'psp2common', 'vitasdk'):
        link = inc / d
        if not link.exists(): link.symlink_to(vitasdk / d)
    arm = any(k in Path(a.cc).name for k in ('arm', 'aarch')) or (a.cc == 'gcc' and os.uname().machine.startswith(('arm', 'aarch')))
    units, wraps = parse(dry_run(stage, out), arm)
    base = units['xk_mem.o'][1]
    for extra in EXTRA:
        units[Path(extra).stem + '.o'] = (SOURCE / extra, [f for f in base])
    generated = (stage / 'boot/generated').resolve()
    if a.runtime_only: units = {n: u for n, u in units.items() if u[0].resolve().parent != generated}
    machine = (['-mfloat-abi=hard'] if arm else []) + (['-m32'] if a.m32 else [])
    print(f'{len(units)} units, {len(wraps)} wraps, cc={a.cc} arm={arm} m32={a.m32} static={a.static}', flush=True)

    def stale(obj, dep):
        if not obj.exists() or not dep.exists(): return True
        t = obj.stat().st_mtime
        text = dep.read_text(errors='replace').replace('\\\n', ' ')
        files = text.split(':', 1)[1].split() if ':' in text else []
        return any(not Path(f).exists() or Path(f).stat().st_mtime > t for f in files if not f.endswith(':'))

    def compile_one(item):
        name, (src, flags) = item
        obj, dep = out / name, out / (name[:-2] + '.d')
        if not stale(obj, dep): return name, 0, ''
        guest = src.resolve().parent == generated
        extra = ['-g0', '-w'] if guest else ['-g']
        cmd = ['nice', '-n', '10', a.cc] + flags + machine + extra + ['-pthread', f'-I{inc}', '-MMD', '-MF', str(dep), '-c', str(src), '-o', str(obj)]
        r = subprocess.run(cmd, cwd=GAME, capture_output=True, text=True)
        return name, r.returncode, r.stderr

    failed = 0
    with (out / 'build.log').open('w') as log, ThreadPoolExecutor(a.jobs) as ex:
        for name, rc, err in ex.map(compile_one, sorted(units.items())):
            if err: log.write(f'== {name}\n{err}\n')
            if rc: failed += 1; print(f'FAIL {name}\n{err[-2000:]}', flush=True)
    if failed: print(f'{failed} units failed'); return 1
    if a.runtime_only: print('runtime units compiled (no link)'); return 0
    objs = [str(out / n) for n in sorted(units)]
    cmd = ['nice', '-n', '10', a.cc, '-pthread'] + machine + (['-static'] if a.static else []) + objs + \
          ['-Wl,--gc-sections'] + wraps + ['-o', str(out / 'harness'), '-lm', '-lpthread']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr[-8000:]); return 1
    print('built', out / 'harness'); return 0


if __name__ == '__main__': sys.exit(main())
