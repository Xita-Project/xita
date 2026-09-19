#!/usr/bin/env python3
"""Check reversible object-query routing on private, owned generated inputs.

This checks installation contracts, not collision semantics. Full-parent ARM
comparisons are recorded separately; no guest source is bundled with this test.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as generator
from tools import query_f32_primitives as f32
from tools import query_object_space as route


def main():
    if not __debug__:
        raise SystemExit('Refusing optimized Python')
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('retained-build', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    retained, out = a.retained_build.resolve(), a.out.resolve()
    if out.is_relative_to(ROOT):
        p.error('owned output must remain outside source worktree')
    out.mkdir(parents=True, exist_ok=False)
    units = out / 'recomp'
    units.mkdir()
    names = {'code_000.c', 'code_013.c', 'code_016.c', 'code_028.c', *f32.HEADERS}
    for name in names:
        target = units / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(retained / 'recomp' / name, target)
    receipt = out / 'generated.json'

    def generate(mode=0):
        return generator.generate(retained / 'haloce/default.xbe',
            retained / 'local/halo_ce_3925/game_manifest.json', units, receipt,
            1, 1, 1, 1, 1, mode)

    default = generate()
    before = {name: (units / name).read_bytes() for name in default['output_sha256']}
    for name in ('code_028.c', 'query_fusion.c', 'solver_fusion.c'):
        assert before[name] == (retained / 'recomp' / name).read_bytes(), name
    selected = generate(1)
    assert set(selected['changed']) == {'code_028.c', 'query_fusion.c'}
    caller, query = [(units / name).read_text() for name in ('code_028.c', 'query_fusion.c')]
    assert route.generic_caller(caller).encode() == before['code_028.c']
    assert query == route.GUARD + before['query_fusion.c'].decode() + route.ADAPTER
    assert not generate(1)['changed'], 'repeat generation changed sources'
    print('PASS exact route, unchanged fused body, repeat generation', flush=True)

    rejected = []

    def reject(label, operation):
        saved = {n: n.read_bytes() for n in (*units.rglob('*'), receipt) if n.is_file()}
        try:
            operation()
        except (ValueError, AssertionError, RuntimeError):
            pass
        else:
            raise AssertionError('accepted ' + label)
        assert all(n.read_bytes() == data for n, data in saved.items()), label
        rejected.append(label)

    reject('invalid-selector', lambda: generate(2))
    reject('missing-guard', lambda: route.generic_caller(caller[len(route.PREFIX):]))
    reject('extra-hook', lambda: route.generic_caller(caller + '\nnq_query_at_17301b(c);'))
    reject('changed-callsite', lambda: route.generic_caller(caller.replace('0x173020u', '0x173021u')))
    original = before['code_028.c'].decode()
    old_query = before['query_fusion.c'].decode()
    reject('changed-primary', lambda: route.generate(original.replace(route.CALL, route.CALL + '\n;'), old_query))
    reject('missing-core', lambda: route.generate(original, ''))
    reject('already-applied', lambda: route.generate(caller, query))
    restored = generate(0)
    assert set(restored['changed']) == {'code_028.c', 'query_fusion.c'}
    assert all((units / n).read_bytes() == data for n, data in before.items())
    assert not generate(0)['changed']

    # Reject invalid Make options before any target can run.
    for value in ('', '2', '-1', '0 1'):
        proc = subprocess.run(['make', '-n', 'XV_QUERY_OBJECT_SPACE=' + value,
                               'query-fusion-generate'], cwd=ROOT, capture_output=True, text=True)
        assert proc.returncode and 'XV_QUERY_OBJECT_SPACE must be 0 or 1' in proc.stderr
        rejected.append('make-selector-' + repr(value))
    proc = subprocess.run(['make', '-n', 'XV_QUERY_OBJECT_SPACE=1', 'XV_NATIVE_QUERY_FUSION=0',
                           'query-fusion-generate'], cwd=ROOT, capture_output=True, text=True)
    assert proc.returncode and 'XV_QUERY_OBJECT_SPACE requires' in proc.stderr
    rejected.append('make-prerequisite')
    (out / 'result.json').write_text(json.dumps(dict(result='PASS',
        default_off=True, off_restores_all_sources=True, fused_query_body_unchanged=True,
        repeat_generation_unchanged=True, rejected=rejected, contract=selected['query_object_space_contract']), indent=2) + '\n')
    print('PASS default OFF, exact restoration and fail-closed input/build contracts', flush=True)


if __name__ == '__main__':
    main()
