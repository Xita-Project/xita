#!/usr/bin/env python3
"""Check native collision hooks survive real generation using private owned inputs."""
import argparse
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as generator
from tools import patch_native_object_query as objects
from tools import patch_native_4b9d0_hooks as solver
from tools import query_f32_primitives as f32


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

    def generate():
        return generator.generate(retained / 'haloce/default.xbe',
            retained / 'local/halo_ce_3925/game_manifest.json', units,
            # Hook retention is independent of the separately pinned query-reuse
            # capture inventory. Its full enabled route is checked by the build.
            out / 'generated.json', 1, 1, 1, 1, 1, 1, 1, 0, 0)

    first = generate()
    query_file, solver_file = units / 'query_fusion.c', units / 'solver_fusion.c'
    query_text, solver_text = query_file.read_text(), solver_file.read_text()
    assert objects.HOOK in query_text and objects.patch(query_text) == query_text
    assert solver.S_CALL_HOOK in solver_text and solver.patch_solver(solver_text) == solver_text
    assert first['guarded_native_object_query'] and first['guarded_native_solver_features']
    assert not generate()['changed']
    # Reproduce the old failure: generated files without post-generation hooks.
    query_file.write_text(query_text.replace(objects.REFERENCE, '').replace(objects.HOOK, objects.BODY))
    solver_file.write_text(solver_text.replace(solver.S_DECL_HOOK, '')
                           .replace(solver.S_CALL_HOOK, solver.S_CALL)
                           .replace(solver.S_CONT_HOOK, solver.S_CONT))
    repaired = generate()
    assert set(repaired['changed']) == {'query_fusion.c', 'solver_fusion.c'}
    assert repaired['output_sha256'] == first['output_sha256']
    assert not generate()['changed']
    (out / 'retention-result.json').write_text(json.dumps(dict(
        result='PASS', restored=repaired['changed'],
        output_sha256=repaired['output_sha256']), indent=2) + '\n')
    print('PASS: native hooks present, repeated generation stable, erased hooks restored exactly')


if __name__ == '__main__':
    main()
