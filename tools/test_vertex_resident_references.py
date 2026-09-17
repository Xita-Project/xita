#!/usr/bin/env python3
"""Fresh-process production retired referenced-group selection and real Make dependency checks.

Uses the production uploader/worker with existing pthread-backed Vita stubs.
The Make fixture executes the entire real build graph with small C inputs; it
does not build guest code, shaders, a Vita package, or run a live benchmark.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--vitasdk', type=Path,
                        default=Path(os.environ.get('VITASDK', Path.home() / 'vitasdk')))
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rows = []

    def run(command, *, cwd=None, env=None, ok=True):
        command = [str(v) for v in command]
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
        rows.append(dict(command=command, cwd=str(cwd) if cwd else None,
                         returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        if (result.returncode == 0) != ok:
            raise RuntimeError(result.stdout + result.stderr)
        return result

    fixture = ROOT / 'tools/tests/vertex_resident_references.c'
    cc = ['cc', '-std=gnu11', '-O2', '-g', '-fno-strict-aliasing', '-Wall', '-Wextra',
          '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-I' + str(ROOT),
          '-I' + str(ROOT / 'runtime'), '-idirafter', str(args.vitasdk / 'arm-vita-eabi/include')]
    clean = {k: v for k, v in os.environ.items() if not k.startswith('XV_')}
    cases = [(None, None), ('0', 0), ('1', 1), ('', 0), ('garbage', 0), ('-1', 1),
             ('2', 1), ('1junk', 1), ('  -2suffix', 1), ('0junk', 0)]
    process_count = 0
    for initial in (None, 0, 1):
        binary = out / f'upload-{initial}'
        flags = [] if initial is None else [f'-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT={initial}']
        # The retained grouped comparator setting is compatible with either mode.
        run(cc + ['-DXV_PACKED_VERTEX_LAYOUT=1', '-DXV_VERTEX_BLOCK_LOADS_DEFAULT=1', *flags, fixture, '-pthread', '-o', binary])
        for setting, parsed in cases:
            configured = (initial or 0) if setting is None else parsed
            env = dict(clean)
            if setting is not None:
                env['XV_VERTEX_RESIDENT_REFERENCES'] = setting
            run([binary, configured], env=env)
            rows[-1].update(default=initial, environment=setting, configured=configured)
            process_count += 1
    for bad in ('2', '-1'):
        result = run(cc + ['-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT=' + bad, '-fsyntax-only', fixture], ok=False)
        assert 'XV_VERTEX_RESIDENT_REFERENCES_DEFAULT must be 0 or 1' in result.stderr

    # Real Makefile, real host compiler/ar, cheap inputs instead of owned assets.
    stage = out / 'make'
    stage.mkdir()

    def put(name, text='', generated=False):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, (1_000_000_000 + 10 * generated,) * 2)

    put('Makefile', (ROOT / 'Makefile').read_text())
    put('games/halo_ce_3925/runtime.mk', (ROOT / 'games/halo_ce_3925/runtime.mk').read_text())
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
                 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', 'recomp/code_000.c',
                 'runtime/main.c', 'runtime/xv_d3d.c', 'runtime/xv_shader.c', 'runtime/xv_ui_gxm.c',
                 'runtime/xv_vertex_upload.c', 'runtime/xv_packed_vertex.h', 'shaders/halo_shaders.json', 'recompiler/gen_layouts.py',
                 'recompiler/shader_recomp_gen.py', 'haloce/default.xbe',
                 'local/halo_ce_3925/game_manifest.json'):
        put(name)
    put('shaders/xv_layouts.h', generated=True)
    for name in ('tools/embed_hud_shaders.py', 'tools/embed_vertex_shaders.py', 'tools/embed_ps_shaders.py',
                 'tools/test_vertex_varyings.py', 'shaders/xv_ps_table.h',
                 'shaders/ps_A972FE61_1D.frag.gxp', 'shaders/ps_5D70F0B3_1D.frag.gxp',
                 'shaders/ps_EB818129_1D.frag.gxp', 'shaders/xv_color.frag.gxp',
                 'shaders/xv_texmod.frag.gxp', 'shaders/xv_tex0.frag.gxp', 'shaders/xv_lm.frag.gxp'):
        put(name)
    for name in ('shaders/xv_hud_gxp.h', 'shaders/xv_vs_gxp.h', 'shaders/xv_ps_gxp.h'):
        put(name, generated=True)
    recorder = stage / 'record.py'
    recorder.write_text('''#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
if '-c' in args:
    flags=[v for v in args if v.startswith(('-D','-I')) or v in ('-MMD','-MP')]
    subprocess.run(['cc',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]],check=True)
else:
    assert args[0]=='rcs';subprocess.run(['ar',*args],check=True)
''')
    recorder.chmod(0o755)
    shutil.copyfile(recorder, stage / 'fake-gcc-ar')
    (stage / 'fake-gcc-ar').chmod(0o755)
    base = ['make', '--no-print-directory', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + shlex.join([sys.executable, str(recorder)]), 'PREFIX=' + str(stage / 'fake'),
            'XV_VERTEX_BLOCK_LOADS_DEFAULT=1', 'XV_RGBA_SWIZZLED_DEFAULT=1']
    owners = {'runtime/' + s + '.c' for s in ('main', 'xv_d3d', 'xv_shader', 'xv_ui_gxm', 'xv_vertex_upload')}
    targets = ['build/' + s[:-2] + '.o' for s in sorted(owners)] + ['build/recomp/libxita_guest.a']
    previous = None
    builds = []
    for initial in (None, 0, 1, 1, 0, 0):
        log = stage / 'commands.jsonl'
        log.write_text('')
        settings = [] if initial is None else [f'XV_VERTEX_RESIDENT_REFERENCES_DEFAULT={initial}']
        run(base + settings + targets, cwd=stage)
        commands = [json.loads(s) for s in log.read_text().splitlines()]
        compiles = {c[c.index('-c') + 1]: c for c in commands if '-c' in c}
        effective = initial or 0
        if previous is not None:
            expected = set() if previous == effective else {'runtime/xv_vertex_upload.c'}
            assert set(compiles) == expected and len(commands) == len(expected), commands
        else:
            assert set(compiles) == owners | {'recomp/code_000.c', 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c'}, commands
        for source, flags in compiles.items():
            assert [v for v in flags if v.startswith('-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT=')] == (
                [f'-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT={effective}'] if source == 'runtime/xv_vertex_upload.c' else []), (source, flags)
            if source == 'runtime/xv_vertex_upload.c':
                assert '-DXV_VERTEX_BLOCK_LOADS_DEFAULT=1' in flags
        assert (stage / 'build/vertex-resident-references-startup.config').read_text() == str(effective) + '\n'
        builds.append(dict(default=initial, recipes=commands))
        previous = effective
    for bad in ('', '2', '-1', '0 1', 'false'):
        result = run(base + ['XV_VERTEX_RESIDENT_REFERENCES_DEFAULT=' + bad, targets[0]], cwd=stage, ok=False)
        assert 'XV_VERTEX_RESIDENT_REFERENCES_DEFAULT must be 0 or 1' in result.stderr
    sources = ('Makefile', 'runtime/xv_vertex_upload.c', 'runtime/xv_upload_worker.c', 'runtime/xv_vertex_refs.h', 'runtime/xv_index_copy.h',
               'recomp/host/vertex_upload_test.c', 'recomp/host/upload_worker_test.c',
               'tools/tests/vertex_resident_references.c', 'tools/test_vertex_resident_references.py')
    (out / 'receipt.json').write_text(json.dumps(dict(
        result='PASS', fresh_processes=process_count, checks=rows, builds=builds,
        sources={s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in sources},
        scope='Production uploader/worker, current indexed masks, host Vita stubs, six real Make transitions; no hardware or performance claim.'), indent=2) + '\n')
    print(f'PASS {process_count} fresh-process actual upload/worker cases; invalid C/Make defaults; six real Make builds with uploader-only default invalidation')


if __name__ == '__main__':
    if not __debug__:
        raise SystemExit('Run without Python -O: verification requires assertions')
    main()
