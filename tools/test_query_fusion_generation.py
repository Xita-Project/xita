#!/usr/bin/env python3
"""Check production installation against qualified owned-source outputs.

Copies only the three retained source inputs into --output-dir. Checks exact
qualified output identity, repeat generation, fail-closed input drift, and OFF
preprocessing/ARM text identity. Does not execute or requalify query semantics.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as generator


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--retained-dir', type=Path, required=True)
    parser.add_argument('--qualified-dir', type=Path, required=True,
                        help='qualified proposed-separate-integration/integration directory')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--arm-cc', type=Path, required=True)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    units = out / 'recomp'
    units.mkdir()
    before = {}
    for name in ('code_013.c', 'code_016.c', 'code_028.c', 'xv_recomp_protos.h'):
        data = (args.retained_dir / name).read_bytes()
        (units / name).write_bytes(data)
        before[name] = data
    receipt = out / 'generated.json'
    result = generator.generate(args.xbe, args.manifest, units, receipt)
    assert result['frames'] == 32
    after = {name: (units / name).read_bytes() for name in ('code_028.c', 'query_fusion.c')}
    mtimes = {name: (units / name).stat().st_mtime_ns for name in after}
    for name, data in after.items():
        qualified = (args.qualified_dir / name).read_text().replace('XV_QUERY_FUSION_PROTOTYPE', generator.FEATURE)
        qualified = generator.GUARD + generator.CHECKS + qualified[len(generator.GUARD):]
        assert data == qualified.encode(), 'qualified output drift: ' + name
    again = generator.generate(args.xbe, args.manifest, units, receipt)
    assert again['changed'] == []
    assert mtimes == {name: (units / name).stat().st_mtime_ns for name in after}
    assert generator.generic_caller(after['code_028.c'].decode()).encode() == before['code_028.c']
    for name in ('code_013.c', 'code_016.c'):
        assert (units / name).read_bytes() == before[name]

    failures = []

    def reject(label, operation):
        saved = {name: (units / name).read_bytes() for name in after}
        try:
            operation()
        except (ValueError, AssertionError, RuntimeError):
            failures.append(label)
        else:
            raise AssertionError('accepted invalid input: ' + label)
        assert all((units / name).read_bytes() == data for name, data in saved.items()), label

    caller = units / 'code_028.c'
    caller.write_bytes(after['code_028.c'].replace(b'nq_query_at_171f94(c,0x172c95u);', b'nq_query_at_171f94(c,0u);'))
    reject('modified-specialized-clone', lambda: generator.generate(args.xbe, args.manifest, units, receipt))
    caller.write_bytes(after['code_028.c'])
    original = units / 'code_013.c'
    original.write_bytes(before['code_013.c'].replace(b'xv_object_motion_begin(c, 6u)', b'xv_object_motion_begin(c, 7u)', 1))
    reject('retained-scope-drift', lambda: generator.generate(args.xbe, args.manifest, units, receipt))
    original.write_bytes(before['code_013.c'])
    wrong_image = out / 'wrong.xbe'
    data = bytearray(args.xbe.read_bytes())
    data[-1] ^= 1
    wrong_image.write_bytes(data)
    reject('owned-image-sha', lambda: generator.generate(wrong_image, args.manifest, units, receipt))
    command = [sys.executable, '-O', str(ROOT / 'tools/gen_native_query_fusion.py'), '--help']
    proc = subprocess.run(command, capture_output=True, text=True)
    assert proc.returncode and 'Refusing optimized Python' in proc.stderr
    failures.append('python-O')

    # Same compile options as the qualified separate production unit. Startup
    # values belong to the linked controls and are intentionally not injected
    # into this generated unit. No fast-math or new FP contraction flag.
    flags = ['-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-w', '-std=gnu11',
             '-I' + str(units), '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'),
             '-I' + str(ROOT / 'runtime'), '-I' + str(ROOT),
             *['-D' + name for name in ('XV_NATIVE_BSP_SPHERE', 'XV_NATIVE_COLLISION_VERTICES',
                 'XV_NATIVE_SEGMENT_SPHERE', 'XV_NATIVE_COLLISION_TRAVERSAL', 'XV_NATIVE_MODEL_PALETTE',
                 'XV_NATIVE_MODEL_HIERARCHY', 'XV_NATIVE_OBJECT_BASIS', 'XV_NATIVE_OBJECT_SCAN',
                 'XV_NATIVE_OBJECT_COLLECT', 'XV_FLARE_QUERY_OVERLAP', 'XV_EXPERIMENTAL_OBJECT_JOBS',
                 'XV_LIGHT_QUERY_CENSUS', 'XV_OBJECT_HOLD_PROFILE', 'XV_OBJECT_POSE_EXPERIMENT')]]
    commands = []

    def compile(name, source, enabled=None):
        output = out / (name + '.o')
        command = [str(args.arm_cc), *flags]
        if enabled is not None:
            command += ['-D' + generator.FEATURE + '=' + str(enabled)]
        command += ['-fstack-usage', '-c', str(source), '-o', str(output)]
        subprocess.run(command, check=True)
        commands.append(command)
        return output

    baseline = out / 'code_028_original.c'
    baseline.write_bytes(before['code_028.c'])
    reference = compile('caller-original', baseline)
    off = compile('caller-off', caller, 0)
    on = compile('caller-on', caller, 1)
    query = compile('query-on', units / 'query_fusion.c', 1)
    query_off = compile('query-off', units / 'query_fusion.c', 0)
    objcopy = str(args.arm_cc).replace('gcc', 'objcopy')

    def text_section(obj):
        target = obj.with_suffix('.text')
        subprocess.run([objcopy, '-O', 'binary', '--only-section=.text', str(obj), str(target)], check=True)
        return target.read_bytes()

    assert text_section(reference) == text_section(off), 'OFF caller machine code differs'
    assert not text_section(query_off), 'OFF query TU is not empty'
    nm = str(args.arm_cc).replace('gcc', 'nm')
    undefined = subprocess.check_output([nm, '-u', str(on)], text=True)
    assert 'nq_query_at_171f94' in undefined
    assert 'nq_query_at_171f94' not in subprocess.check_output([nm, '-u', str(off)], text=True)
    imports = subprocess.check_output([nm, '-u', str(query)], text=True)
    assert 'original_' not in imports and 'ct_observe' not in imports and 'ct_site' not in imports
    assert 'nq_collection' not in imports
    # Direct non-Make builds also fail closed if a compiled prerequisite is missing.
    command = [str(args.arm_cc), *[f for f in flags if f != '-DXV_NATIVE_COLLISION_TRAVERSAL'],
               '-D' + generator.FEATURE + '=1', '-fsyntax-only', str(units / 'query_fusion.c')]
    proc = subprocess.run(command, capture_output=True, text=True)
    assert proc.returncode and 'query fusion requires' in proc.stderr
    failures.append('compiled-prerequisite')
    stacks = {p.name: p.read_text() for p in out.glob('*.su')}
    summary = dict(result='PASS', generator_result=result, qualified_output_identity=True,
        repeat_generation_source_mtimes_unchanged=True, generic_source_identity=True,
        original_vs_off_caller_text_identity=True, off_query_empty=True,
        expected_failures=failures, commands=commands, stack_usage=stacks,
        query_imports=imports, caller_imports=undefined,
        semantic_evidence='Reuses qualified combined oracle; no broad semantic suites rerun.')
    (out / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('PASS production query generation: exact qualified outputs, idempotence, negative inputs, OFF ARM identity, ON ARM objects and stack')


if __name__ == '__main__':
    main()
