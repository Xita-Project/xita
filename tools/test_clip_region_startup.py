#!/usr/bin/env python3
"""Fresh-process qualification of the private clip startup selection.

Links the actual production pool/controller and retained generated clip config;
does not run a timing benchmark or pretend pthreads prove Vita scheduling.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--clip-source', type=Path, default=ROOT/'recomp/kernel/xk_clip.c')
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    clip = a.clip_source.resolve()
    if not clip.is_file():
        p.error('Provide the retained generated xk_clip.c with --clip-source')
    cmd = [os.environ.get('CC', 'cc'), '-O2', '-std=gnu11', '-pthread',
           '-fno-strict-aliasing', '-ffp-contract=off', '-ffunction-sections', '-fdata-sections',
           '-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DXV_LIGHT_QUERY_CENSUS',
           '-DXV_NATIVE_CLIP_REGION', '-DXV_CLIP_REGION_TRIAL=1',
           '-I'+str(ROOT/'recomp'), '-I'+str(ROOT/'recomp/kernel')]
    if a.sanitize:
        cmd += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    cmd += [str(ROOT/'tools/tests/clip_region_startup.c'), str(clip),
            str(ROOT/'recomp/kernel/xk_clip_region_control.c'),
            '-Wl,--gc-sections,--wrap=xv_native_clip_region_init,--wrap=xv_native_clip_region_override,--wrap=xv_clip_region_compatible',
            '-lm', '-o', str(out/'startup')]
    (out/'compile-command.json').write_text(json.dumps(cmd, indent=2)+'\n')
    r = subprocess.run(cmd, text=True, capture_output=True)
    (out/'compile.log').write_text(r.stdout+r.stderr); r.check_returncode()
    cases = ['admission', 'native-off', 'registers-off', 'override-off',
             'preinit-off', 'preinit-on', 'preinit-active', 'register-override-on']
    results = []
    for case in cases:
        r = subprocess.run([str(out/'startup'), case], text=True, capture_output=True, timeout=30)
        (out/(case+'.log')).write_text(r.stdout+r.stderr)
        results.append({'case': case, 'returncode': r.returncode, 'output': r.stdout.strip()})
        if r.returncode:
            (out/'failure.json').write_text(json.dumps(results, indent=2)+'\n')
            r.check_returncode()
    inputs = [ROOT/'tools/tests/clip_region_startup.c', Path(__file__).resolve(),
              ROOT/'recomp/kernel/xk_clip_trial.h', ROOT/'recomp/kernel/xk_object_jobs.c',
              ROOT/'recomp/kernel/xk_object_jobs.h', ROOT/'recomp/kernel/xk_light_census.h',
              ROOT/'recomp/kernel/xk_clip_region_control.c', ROOT/'recomp/kernel/xk_clip_region.h',
              ROOT/'recomp/kernel/xk_object_mutex.h', ROOT/'recomp/xv_x86rt.h', clip]
    receipt = {'pass': True, 'sanitize': a.sanitize, 'cases': results,
               'inputs': {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},
               'binary_sha256': hashlib.sha256((out/'startup').read_bytes()).hexdigest(),
               'scope': 'actual host pool/admission/controller/startup helper; explicit platform/diagnostic doubles; no Vita scheduling or gameplay claim'}
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({'pass': True, 'cases': len(results), 'receipt': str(out/'receipt.json')}))


if __name__ == '__main__':
    main()
