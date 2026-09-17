#!/usr/bin/env python3
"""Validate shared production generation against retained owned solver outputs.

Runs no gameplay/emulator. Owned sources/objects are copied only into the new
private output directory. Semantic receipts are reused; this checks wiring,
strict generation failures, machine-code identity, and production stack usage.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as query
from tools import gen_native_solver_fusion as solver
from elftools.elf.elffile import ELFFile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    if not __debug__:
        raise SystemExit('Refusing optimized Python')
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'retained-dir', 'qualified-dir', 'output-dir', 'arm-cc'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    out = a.output_dir.resolve()
    if out.is_relative_to(ROOT):
        p.error('owned outputs must remain outside the source worktree')
    out.mkdir(parents=True, exist_ok=False)
    units = out / 'recomp'
    units.mkdir()
    names = ('code_000.c', 'code_013.c', 'code_016.c', 'code_028.c', 'xv_recomp_protos.h')
    before = {name: (a.retained_dir / name).read_bytes() for name in names}
    for name, data in before.items():
        (units / name).write_bytes(data)
    receipt = out / 'generated.json'
    query.generate(a.xbe, a.manifest, units, receipt, 0)
    query_only = (units / 'code_028.c').read_bytes()
    original_query = (units / 'query_fusion.c').read_bytes()
    result = query.generate(a.xbe, a.manifest, units, receipt, 1)
    outputs = ('code_028.c', 'query_fusion.c', 'solver_fusion.c', 'solver_primitives.h')
    after = {name: (units / name).read_bytes() for name in outputs}
    mtimes = {name: (units / name).stat().st_mtime_ns for name in outputs}
    assert result['solver_enabled'] == 1 and result['solver_contract']['bounded_frames'] == 4
    assert solver.generic_caller(after['code_028.c'].decode()).encode() == query_only
    assert after['query_fusion.c'] == original_query
    for name in ('code_000.c', 'code_013.c', 'code_016.c'):
        assert (units / name).read_bytes() == before[name]
    assert query.generate(a.xbe, a.manifest, units, receipt, 1)['changed'] == []
    assert mtimes == {name: (units / name).stat().st_mtime_ns for name in outputs}
    query.generate(a.xbe, a.manifest, units, receipt, 0)
    assert (units / 'code_028.c').read_bytes() == query_only
    query.generate(a.xbe, a.manifest, units, receipt, 1)
    assert all((units / name).read_bytes() == data for name, data in after.items())

    failures = []
    def reject(label, operation):
        saved = {name: (units / name).read_bytes() for name in outputs}
        try:
            operation()
        except (ValueError, AssertionError, RuntimeError):
            failures.append(label)
        else:
            raise AssertionError('accepted invalid input: ' + label)
        assert all((units / name).read_bytes() == data for name, data in saved.items()), label
    caller = units / 'code_028.c'
    caller.write_bytes(after['code_028.c'].replace(b'ns_solver_at_172cb8(c,0x172cb8u);', b'ns_solver_at_172cb8(c,0u);'))
    reject('caller-eligibility-drift', lambda: query.generate(a.xbe, a.manifest, units, receipt, 1))
    caller.write_bytes(after['code_028.c'])
    caller.write_bytes(after['code_028.c'].replace(b'xv_object_motion_begin(c, 4u)', b'xv_object_motion_begin(c, 5u)', 1))
    reject('solver-profile-scope-drift', lambda: query.generate(a.xbe, a.manifest, units, receipt, 1))
    caller.write_bytes(after['code_028.c'])
    wrong = out / 'wrong.xbe'
    data = bytearray(a.xbe.read_bytes()); data[-1] ^= 1; wrong.write_bytes(data)
    reject('owned-image-sha', lambda: query.generate(wrong, a.manifest, units, receipt, 1))
    reject('invalid-feature-value', lambda: query.generate(a.xbe, a.manifest, units, receipt, 2))
    proc = subprocess.run([sys.executable, '-O', str(ROOT / 'tools/gen_native_query_fusion.py'), '--help'], capture_output=True, text=True)
    assert proc.returncode and 'Refusing optimized Python' in proc.stderr
    failures.append('python-O')

    flags = ['-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-w', '-std=gnu11',
             '-I' + str(units), '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'),
             '-I' + str(ROOT / 'runtime'), '-I' + str(ROOT),
             *['-D' + name for name in ('XV_NATIVE_BSP_SPHERE', 'XV_NATIVE_COLLISION_VERTICES',
                 'XV_NATIVE_SEGMENT_SPHERE', 'XV_NATIVE_COLLISION_TRAVERSAL', 'XV_NATIVE_MODEL_PALETTE',
                 'XV_NATIVE_MODEL_HIERARCHY', 'XV_NATIVE_OBJECT_BASIS', 'XV_NATIVE_OBJECT_SCAN',
                 'XV_NATIVE_OBJECT_COLLECT', 'XV_FLARE_QUERY_OVERLAP', 'XV_EXPERIMENTAL_OBJECT_JOBS',
                 'XV_LIGHT_QUERY_CENSUS', 'XV_OBJECT_HOLD_PROFILE')]]
    commands = []
    def compile(name, source, mode, separate=False):
        target = out / (name + '.o')
        extra = ['-ffunction-sections', '-fdata-sections'] if separate else []
        if not separate:
            extra += ['-DXV_NATIVE_QUERY_FUSION=1']
        command = [str(a.arm_cc), *flags, *extra,
                   '-D' + solver.FEATURE + '=' + str(mode), '-fstack-usage', '-c', str(source), '-o', str(target)]
        subprocess.run(command, check=True); commands.append(command)
        return target
    baseline = out / 'caller-before.c'; baseline.write_bytes(query_only)
    jobs = [('caller-before', baseline, 0), ('caller-off', caller, 0),
            ('caller-on', caller, 1), ('solver-on', units / 'solver_fusion.c', 1, True),
            ('solver-off', units / 'solver_fusion.c', 0, True)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(compile, *job) for job in jobs]
        before_obj, off_obj, on_obj, candidate, empty = [f.result() for f in futures]
    commands.sort(key=lambda command: command[-1])
    def section(path, name):
        with path.open('rb') as f:
            s = ELFFile(f).get_section_by_name(name)
            return s.data() if s else b''
    def function(path, name):
        with path.open('rb') as f:
            elf = ELFFile(f)
            symbol = elf.get_section_by_name('.symtab').get_symbol_by_name(name)[0]
            start = symbol['st_value'] & ~1
            return elf.get_section(symbol['st_shndx']).data()[start:start + symbol['st_size']]
    assert section(before_obj, '.text') == section(off_obj, '.text')
    for address in ('00170C10', '00170980', '001709D0'):
        assert function(before_obj, 'f_' + address) == function(on_obj, 'f_' + address), address
    assert section(candidate, '.text.ns_solver_fused') == section(a.qualified_dir / 'unit-0.o', '.text.ns_solver_fused')
    assert not section(empty, '.text.ns_solver_fused') and not section(empty, '.text')
    nm = str(a.arm_cc).replace('gcc', 'nm')
    undefined = subprocess.check_output([nm, '-u', str(on_obj)], text=True)
    imports = subprocess.check_output([nm, '-u', str(candidate)], text=True)
    assert 'ns_solver_at_172cb8' in undefined
    assert 'ns_solver_at_172cb8' not in subprocess.check_output([nm, '-u', str(off_obj)], text=True)
    assert not any(name in imports for name in ('ns_site', 'ns_fallbacks', 'ns_current_context', 'arm_'))
    assert 'f_00085720' in imports and 'f_00085A00' in imports
    command = [str(a.arm_cc), *flags, '-D' + solver.FEATURE + '=1', '-DXV_OBJECT_SOLVER_EXPERIMENT',
               '-fsyntax-only', str(units / 'solver_fusion.c')]
    proc = subprocess.run(command, capture_output=True, text=True)
    assert proc.returncode and 'must retain the actor transaction' in proc.stderr
    failures.append('unsafe-unlock-compile-guard')
    stacks = {p.name: p.read_text() for p in out.glob('*.su')}
    (out / 'result.json').write_text(json.dumps(dict(result='PASS', generator_result=result,
        source_mtimes_stable_on_repeat=True, exact_query_only_restore=True,
        generic_sources_unchanged=True, generic_solver_caller_unit_functions_identical=True,
        off_caller_text_identical=True, qualified_candidate_text_identical=True,
        candidate_text_sha256=digest(section(candidate, '.text.ns_solver_fused')),
        failures_rejected=failures, commands=commands, stack_usage=stacks,
        candidate_imports=imports, caller_imports=undefined), indent=2) + '\n')
    print('PASS combined production generation: strict signatures, repeat/OFF restore, qualified ARM text, generic identities, stack and scope guards')


if __name__ == '__main__':
    main()
