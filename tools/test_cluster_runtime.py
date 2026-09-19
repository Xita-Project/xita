#!/usr/bin/env python3
"""Exercise the typed query through the actual worker pool and production hook.

Original game code is generated only in the caller's private output directory.
These tests establish guarded publication, not physical-Vita performance.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r
from tools.test_cluster_query import generate


def generate_worker_reference(xbe, manifest, out):
    raw = generate(xbe, manifest, out, full=True, extra_pcs=(0x565E0, 0xA92C0))
    hooks = HaloHooks(r.Image(str(xbe), str(manifest)))
    assert hooks.enabled
    text = '#include "kernel/xk_object_jobs.h"\n'
    text += ''.join(f'void f_{pc:08X}(xctx *);\nvoid ref_{pc:08X}(xctx *);\n' for pc in raw)
    for pc, body in raw.items():
        reference = re.sub(r'\bf_([0-9A-F]{8})', r'ref_\1', body)
        if pc == 0x56670:
            reference = reference.replace('{\n', '{\n#ifdef XV_QUERY_WORK_TEST\n'
                '    extern void query_work_reference_begin(xctx *); query_work_reference_begin(c);\n#endif\n', 1)
            reference = reference.replace('L_000566DE:\n', 'L_000566DE:\n#ifdef XV_QUERY_WORK_TEST\n'
                '    { extern void query_work_reference_end(xctx *); query_work_reference_end(c); }\n#endif\n')
        text += reference
        text += hooks.transform_body(pc, body.replace('{\n', '{\n' + '\n'.join(hooks.function_entry(pc)) + '\n', 1))
    (out / 'worker-reference.c').write_text(text)
    (out / 'worker_query_axes.h').write_text((out / 'cluster_axes.h').read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--sanitize', choices=('address', 'thread'))
    p.add_argument('--overlap', action='store_true')
    p.add_argument('--arm', action='store_true')
    p.add_argument('--census', action='store_true', help='verify original-prefix workload counters through the actual pool')
    p.add_argument('--mode', action='append')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    generate_worker_reference(a.xbe, a.manifest, a.out)
    flags = ['-O2', '-g', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
             '-frounding-math', '-ffunction-sections', '-fdata-sections',
             '-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DXV_WORKER_QUERY', '-DXV_TYPED_CLUSTER_QUERY',
             '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'), '-I' + str(a.out)]
    runtime = [ROOT / 'recomp/kernel' / (name + '.c') for name in
               ('xk_cluster_runtime', 'xk_cluster_snapshot', 'xk_cluster_query', 'xk_cluster_query_replay')]
    if a.overlap:
        flags += ['-DXV_QUERY_OVERLAP_DEFAULT=1', '-DXV_QUERY_OVERLAP_TEST']
    if a.census:
        flags += ['-DXV_LIGHT_QUERY_CENSUS', '-DXV_QUERY_WORK_TEST']
        runtime.append(ROOT / 'recomp/kernel/xk_light_census.c')
    if a.arm:
        cc = os.environ.get('ARM_CC', '/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
        for path in runtime + [ROOT / 'recomp/kernel/xk_object_jobs.c']:
            subprocess.run([cc, *flags, '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
                            '-Wall', '-Wextra', '-fstack-usage', '-c', str(path),
                            '-o', str(a.out / (path.stem + '.o'))], check=True)
        return
    flags += ['-DXV_WORKER_QUERY_TEST']
    if a.sanitize:
        flags += ['-fsanitize=' + ('address,undefined' if a.sanitize == 'address' else 'thread'),
                  '-fno-omit-frame-pointer', '-no-pie']
    binary = a.out / 'test'
    command = [os.environ.get('CC', 'cc'), *flags, str(a.out / 'worker-reference.c'),
               str(ROOT / 'tools/tests/worker_query.c'), *map(str, runtime),
               str(ROOT / 'recomp/xv_x86rt.c'), '-pthread', '-Wl,--gc-sections', '-lm', '-o', str(binary)]
    (a.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    subprocess.run(command, check=True)
    for mode in a.mode or ('normal', 'disabled', 'alias', 'mutation', 'parking', 'concurrent', 'source', 'inflight', 'budget') + (('overlap',) if a.overlap else ()):
        run = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=120,
                             env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        (a.out / (mode + '.log')).write_text(run.stdout + run.stderr)
        if mode == 'budget':
            assert run.returncode != 0 and 'job instruction budget exceeded' in run.stderr, run
        else:
            assert run.returncode == 0, run.stdout + run.stderr
        print('PASS:', mode, run.stdout.strip(), flush=True)


if __name__ == '__main__':
    main()
