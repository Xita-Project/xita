#!/usr/bin/env python3
"""Check world-run installation on private owned inputs, not game semantics."""
import argparse
import hashlib
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as generator
from tools import query_f32_primitives as f32
from tools import query_world_run as run


def main():
    if not __debug__:
        raise SystemExit('Refusing optimized Python')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retained-build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    retained, out = args.retained_build.resolve(), args.out.resolve()
    if out.is_relative_to(ROOT):
        parser.error('owned outputs must remain outside the source worktree')
    out.mkdir(parents=True, exist_ok=False)
    units = out / 'recomp'
    for name in {'code_000.c', 'code_013.c', 'code_016.c', 'code_028.c', *f32.HEADERS}:
        target = units / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(retained / 'recomp' / name, target)

    def generate(mode=0):
        return generator.generate(retained / 'haloce/default.xbe',
            retained / 'local/halo_ce_3925/game_manifest.json', units,
            out / 'generated.json', 1, 1, 1, 1, 1, 1, mode)

    original = generate()
    before = {name: (units / name).read_bytes() for name in original['output_sha256']}
    assert before['query_fusion.c'] == (retained / 'recomp/query_fusion.c').read_bytes()
    enabled = generate(1)
    after = {name: (units / name).read_bytes() for name in enabled['output_sha256']}
    assert {name for name in before if before[name] != after[name]} == {'query_fusion.c'}
    assert set(after) - set(before) == {run.OUTPUT}
    assert hashlib.sha256(after[run.OUTPUT]).hexdigest() == enabled['query_world_run_contract']['helper_sha256']
    assert not generate(1)['changed']
    generate()
    assert all((units / name).read_bytes() == value for name, value in before.items())
    # A drifted input must fail before outputs are published.
    try:
        run.generate(before['query_fusion.c'].decode().replace(run.LABEL, 'L_DRIFTED:\n'))
    except ValueError:
        pass
    else:
        raise AssertionError('drifted backedge accepted')
    for invalid in (-1, 2):
        try:
            generate(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid selector accepted')
    assert all((units / name).read_bytes() == value for name, value in before.items())
    print('PASS world-run generation: default identity, query-only change, repeat no-op, OFF restoration, drift/invalid rejection')


if __name__ == '__main__':
    main()
