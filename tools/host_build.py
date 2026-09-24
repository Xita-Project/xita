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
HOST_SKIP = {'recomp/kernel/xk_net.c', 'recomp/kernel/xk_os_vita.c', 'recomp/xv_trace_stub.c'}   # Vita network HLE and the Vita OS layer
HOST_EXTRA = ['recomp/kernel/xk_os_host.c', 'recomp/host/harness.c', 'recomp/host/trace.c', 'recomp/host/softgfx.c', 'recomp/host/runtime_stubs.c', 'recomp/host/host_reports.c', 'recomp/host/write_watch.c', 'recomp/host/sampler.c']
# --runtime: the Vita runtime's D3D recording path (xv_d3d.c bridge, xv_ui_gxm.c hooks, vertex capture/upload and
# texture workers) against recomp/host/vita_runtime_shim.c (pthread sceKernel objects, GXM no-ops + GXP reflection,
# a synchronous present). softgfx.c's software UI hooks are replaced by xv_ui_gxm.c's. Recording only, no replay.
RUNTIME_UNITS = ['runtime/xv_d3d.c', 'runtime/xv_ui_gxm.c', 'runtime/xv_draw_profile.c', 'runtime/xv_vertex_capture.c',
                 'runtime/xv_vertex_prepare.c', 'runtime/xv_vertex_upload.c', 'runtime/xv_upload_worker.c', 'runtime/xv_gpu_upload.c',
                 'runtime/xv_texture_worker.c', 'runtime/xv_geometry_worker.c', 'runtime/xv_shader.c', 'runtime/xv_cpu.c',
                 'runtime/xv_settings.c', 'runtime/xv_render_profile.c']
RUNTIME_SHIM = 'recomp/host/vita_runtime_shim.c'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', required=True); ap.add_argument('--commands', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--cc', default='gcc'); ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--static', action='store_true', help='link statically (cross builds: no glibc version match needed on the target)')
    ap.add_argument('--runtime', action='store_true', help='link the runtime D3D recording path (vita_runtime_shim.c) instead of softgfx.c')
    ap.add_argument('--vitasdk', default=os.environ.get('VITASDK', os.path.expanduser('~/vitasdk')), help='psp2 headers for --runtime units')
    a = ap.parse_args()
    stage = Path(a.stage).resolve(); out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
    arm = 'arm' in a.cc or 'aarch' in a.cc or (a.cc == 'gcc' and os.uname().machine.startswith(('arm', 'aarch')))
    units = {}   # src -> flags
    for line in Path(a.commands).read_text(errors='replace').splitlines():
        if not line.startswith('arm-vita-eabi-gcc') or ' -c ' not in line: continue
        toks = shlex.split(line)
        src = toks[toks.index('-c') + 1]
        if not src.startswith('recomp/') and not (a.runtime and src in RUNTIME_UNITS): continue   # runtime/ and dashboard/ are Vita GXM/UI
        if src in HOST_SKIP: continue
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
    # xd3d_count's on-helper fast path (the Vita's stack-range check) on the host too: the scene-thread hooks return at
    # once on the helper, so the Pi no longer pays two calls per HLE call that the Vita skips (xk_scene_thread.c sets the range)
    if 'recomp/kernel/xd3d.c' in units: units['recomp/kernel/xd3d.c'] = units['recomp/kernel/xd3d.c'] + ['-DXV_HOST_HELPER_SP=1']
    kernel_flags = units.get('recomp/kernel/xk_object_jobs.c') or units.get('recomp/kernel/xk_mem.c') or []
    for h in HOST_EXTRA:
        if (stage / h).exists(): units[h] = [f for f in kernel_flags]
    runtime_opt = {}
    if a.runtime:
        units.pop('recomp/host/softgfx.c', None)
        stale = out / 'softgfx.o'
        if stale.exists(): stale.unlink()
        missing = [u for u in RUNTIME_UNITS if u not in units]
        if missing: print('--runtime: not in the dry run:', missing); return 1
        if 'recomp/host/runtime_stubs.c' in units: units['recomp/host/runtime_stubs.c'] = units['recomp/host/runtime_stubs.c'] + ['-DXV_HOST_RUNTIME=1']
        units[RUNTIME_SHIM] = list(units['runtime/xv_d3d.c'])   # same struct layouts (XV_PACKED_VERTEX_LAYOUT ...) as the bridge
        inc = ['-idirafter', str(Path(a.vitasdk) / 'arm-vita-eabi' / 'include')]
        if arm: inc += ['-include', 'recomp/host/neon_x4_compat.h']   # GCC < 14 lacks AArch32 vld1q_u8_x4 (the Vita's GCC 15 has it)
        for u in RUNTIME_UNITS + [RUNTIME_SHIM]:
            units[u] = units[u] + inc
            runtime_opt[u] = ['-O2', '-g']      # the Vita builds runtime/ at -O2; -g for line-level sample attribution
        # The D3D HLE is half of the recording path: build it at the Vita's -O2 too (other recomp units stay at -O1,
        # where e.g. inline helpers and __builtin_strncmp on literals are not always folded as they are on the Vita).
        runtime_opt['recomp/kernel/xd3d.c'] = ['-O2', '-g']
    # 32-bit ARM Linux (Raspberry Pi OS 32-bit, or a cross build for it): same ISA/FPU class as the Vita build
    # (-mcpu=cortex-a9 -mfpu=neon-fp16 come from make-n and are dropped by DROP_PREFIX); an A72 runs A9-tuned code.
    arm_flags = ['-marm', '-march=armv7-a', '-mfpu=neon', '-mfloat-abi=hard'] if arm and 'aarch' not in a.cc and not os.uname().machine.startswith('aarch') else []
    print(f'{len(units)} units, arm={arm}, cc={a.cc}, static={a.static}', flush=True)
    # every unit includes these; an older object than any of them is stale (a TLS change in xv_x86rt.h once produced 'bad value' at link)
    HEADER_MTIME = max((stage / h).stat().st_mtime for h in ('recomp/xv_x86rt.h', 'recomp/kernel/xk.h', 'recomp/kernel/xk_os.h', 'recomp/xv_recomp_protos.h', 'recomp/kernel/xk_object_jobs.h') if (stage / h).exists())
    def compile_one(item):
        src, flags = item
        obj = out / (Path(src).stem + '.o'); s = stage / src
        if obj.exists() and obj.stat().st_mtime > max(s.stat().st_mtime, HEADER_MTIME): return (src, 0, '')   # headers: see HEADER_MTIME
        cmd = [a.cc] + HOST_FLAGS + runtime_opt.get(src, []) + arm_flags + flags + ['-c', str(s), '-o', str(obj)]
        r = subprocess.run(cmd, cwd=stage, capture_output=True, text=True)
        return (src, r.returncode, r.stderr[-1500:])
    failed = 0
    with ThreadPoolExecutor(a.jobs) as ex:
        for src, rc, err in ex.map(compile_one, sorted(units.items())):
            if rc: failed += 1; print(f'FAIL {src}\n{err}', flush=True)
    if failed: print(f'{failed} units failed'); return 1
    objs = sorted(str(p) for p in out.glob('*.o'))
    r = subprocess.run([a.cc, '-pthread'] + arm_flags + (['-static'] if a.static else []) + objs + ['-o', str(out / 'harness'), '-lm', '-lpthread'], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-6000:]); return 1
    print('built', out / 'harness'); return 0

if __name__ == '__main__': sys.exit(main())
