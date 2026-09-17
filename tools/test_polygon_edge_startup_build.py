#!/usr/bin/env python3
"""Two-object ARM transition check in an already copied private retained stage.

Never builds/packages the game or regenerates owned functions. --stage must be
an independent disposable copy of --retained-stage, with its existing objects.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NAMES = ['Makefile', 'recomp/kernel/xd3d.c', 'recomp/kernel/xk_polygon_edge_trial.h',
         'runtime/xv_benchmark.c']
TARGETS = ['build/recomp/kernel/xd3d.o', 'build/runtime/xv_benchmark.o']


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--retained-stage', type=Path, required=True)
    p.add_argument('--retained-command', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    stage, retained, out = a.stage.resolve(), a.retained_stage.resolve(), a.out.resolve()
    assert stage != retained and stage != ROOT and stage not in retained.parents
    assert retained not in stage.parents and not (out/'receipt.json').exists()
    out.mkdir(parents=True, exist_ok=True)
    before = {str(f.relative_to(stage)): sha(f) for f in (stage/'build').rglob('*.o')}
    assert all(sha(retained/n) == h for n, h in before.items())
    source_hashes = {n: sha(ROOT/n) for n in NAMES}
    for n in NAMES:
        dst = stage/n
        assert not dst.is_symlink()
        if dst.exists():
            assert not os.path.samefile(dst, retained/n)
        shutil.copyfile(ROOT/n, dst)
    base = json.loads(a.retained_command.read_text())['command']
    assert base[-1] == 'xita.vpk' and 'XV_CLIP_REGION_TRIAL=1' in base
    base = base[:-1]
    assert not any(x.startswith('XV_POLYGON_EDGE_TRIAL=') for x in base)
    env = dict(os.environ)
    env.setdefault('VITASDK', str(Path.home()/'vitasdk'))
    env['PATH'] = env['VITASDK']+'/bin:'+env['PATH']
    rows = []
    for name, value in [('default', None), ('on', '1'), ('repeat-on', '1'),
                        ('off', '0'), ('repeat-off', '0'), ('on-final', '1')]:
        cmd = base+([] if value is None else ['XV_POLYGON_EDGE_TRIAL='+value])+TARGETS
        prior = {n: (stage/n).stat().st_mtime_ns for n in TARGETS}
        r = subprocess.run(cmd, cwd=stage, env=env, text=True, capture_output=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        r.check_returncode()
        # Fail closed if the narrow command ever starts owned-code generation.
        assert 'gen_native_' not in r.stdout and 'xita_recomp.py' not in r.stdout
        changed = sorted(n for n, h in before.items() if sha(stage/n) != h)
        row = dict(name=name, value=value, command=cmd,
                   sha256={n: sha(stage/n) for n in TARGETS},
                   rebuilt={n: (stage/n).stat().st_mtime_ns != prior[n] for n in TARGETS},
                   changed_from_retained=changed)
        assert changed == ([] if value in (None, '0') else sorted(TARGETS)), row
        if name.startswith('repeat-'):
            assert not any(row['rebuilt'].values()), row
        assert (stage/'build/polygon-edge-trial.config').read_text() == (value or '0')+'\n'
        rows.append(row)
        (out/'rows.json').write_text(json.dumps(rows, indent=2)+'\n')
        print(name, changed, flush=True)
    assert {n: sha(ROOT/n) for n in NAMES} == source_hashes
    assert all(sha(retained/n) == h for n, h in before.items()), 'retained input changed'
    receipt = dict(pass_all=True, source_sha256=source_hashes, rows=rows,
                   retained_objects=len(before), unchanged_other_objects=len(before)-len(TARGETS),
                   scope='Only two actual ARM objects compiled; other object hashes preserved, not freshly rebuilt. No game link/package or owned-code regeneration.')
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')


if __name__ == '__main__':
    main()
