#!/usr/bin/env python3
"""Check the full capture/publish adapter on actual pthread workers.

The independent lifted reference is extracted into private output only. Platform
allocation/fiber fixtures do not claim to model the Vita scheduler or timing.
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('reference_root', 'reference_leaf', 'clip_source', 'out'):
        p.add_argument('--' + n.replace('_', '-'), type=Path, required=True)
    p.add_argument('--sanitizer', choices=('address,undefined', 'thread'), default='address,undefined')
    a = p.parse_args(); a.out = a.out.resolve()
    if a.out.is_relative_to(ROOT): p.error('private output required')
    a.out.mkdir(parents=True, exist_ok=False)
    fixture = (ROOT/'tools/tests/clip_region_startup.c').read_text().replace('ARENA_BYTES=2u<<20', 'ARENA_BYTES=8u<<20')
    for name in ('xk_object_jobs.c', 'xk_clip_trial.h'):
        fixture = fixture.replace('../../recomp/kernel/'+name, str(ROOT/'recomp/kernel'/name))
    fixture += '\nuint32_t xk_mem_arena_size(void) { return ARENA_BYTES; }\n'
    (a.out/'visibility_capture_backend.c').write_text(fixture)
    hashes = {}; bodies = []
    for path, name, expected in (
        (a.reference_root, 'f_00052E10', '75406ef2b4e74a0ef9431342e19f0853a8650faff04edf008a4ab984eafdfbbe'),
        (a.reference_leaf, 'f_0005C300', '227a6e6fe6f39300d235767f1bd178f654eb9e8ddc032dec5ba05a855841f86f')):
        s = path.read_text(); lo = s.index('void '+name+'('); hi = s.index('\nvoid f_', lo+1)
        body = s[lo:hi]; digest = hashlib.sha256(body.encode()).hexdigest()
        assert digest == expected, (name, digest)
        hashes[name] = digest; bodies.append(body)
    prefix = a.reference_root.read_text().split('\nvoid f_', 1)[0]
    prefix = prefix.replace('#include "xv_recomp_protos.h"', '#include "xv_x86rt.h"\n#include "xv_phase.h"\nvoid f_0005C300(xctx *);')
    original = a.out/'original.c'; original.write_text(prefix+'\n'+'\n'.join(bodies))
    cmd = [os.environ.get('CC','cc'), '-O2', '-g', '-std=gnu11', '-pthread', '-fno-strict-aliasing',
        '-ffp-contract=off', '-frounding-math', '-ffunction-sections', '-fdata-sections', '-fno-omit-frame-pointer', '-no-pie',
        '-fsanitize='+a.sanitizer, '-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DXV_LIGHT_QUERY_CENSUS',
        '-DXV_NATIVE_CLIP_REGION', '-DXV_CLIP_REGION_TRIAL=1', '-DXV_NATIVE_VISIBILITY_JOBS=1',
        '-I'+str(ROOT/'recomp'), '-I'+str(ROOT/'recomp/kernel'), '-I'+str(a.out),
        str(ROOT/'tools/tests/visibility_pass_owner.c'), str(original), str(a.clip_source.resolve()),
        str(ROOT/'recomp/kernel/xk_clip_region_control.c'), str(ROOT/'recomp/kernel/xk_visibility_pass.c'),
        str(ROOT/'recomp/kernel/xk_subcluster_math.c'), str(ROOT/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections,--wrap=xv_preempt,--wrap=xs_bounds,--wrap=xv_native_clip_region_init,--wrap=xv_native_clip_region_override,--wrap=xv_clip_region_compatible',
        '-lm', '-o', str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(dict(command=cmd, reference_sha256=hashes), indent=2)+'\n')
    with (a.out/'compile.log').open('w') as f: subprocess.run(cmd, check=True, stdout=f, stderr=subprocess.STDOUT)
    r = subprocess.run([str(a.out/'test')], capture_output=True, text=True, timeout=120,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', TSAN_OPTIONS='halt_on_error=1'))
    (a.out/'run.log').write_text(r.stdout+r.stderr)
    (a.out/'result.json').write_text(json.dumps(dict(exit_code=r.returncode, output=r.stdout.strip(), sanitizer=a.sanitizer), indent=2)+'\n')
    r.check_returncode(); print(r.stdout.strip())
if __name__ == '__main__': main()
