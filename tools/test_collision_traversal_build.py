#!/usr/bin/env python3
"""Exercise collision traversal incremental builds through the production Makefile.

Copies Makefile/runtime.mk into a private fixture and records real make recipes.
Small C inputs replace engine code; a recording compiler uses the host compiler
for those inputs, including genuine -MMD dependency generation. No SDK, owned
image, device or generated game code is needed. Semantic/startup execution is
covered separately by test_collision_traversal.py.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FEATURE = 'XV_NATIVE_COLLISION_TRAVERSAL'
DEFAULT = FEATURE + '_DEFAULT'
HOOK = 'recomp/code_000.c'
CONTROL = 'recomp/kernel/xk_collision_traversal_control.c'
HEADER = 'recomp/kernel/xk_collision_traversal.h'
MAIN = 'runtime/main.c'
GAME = 'build/recomp/libxita_game.a'
GUEST = 'build/recomp/libxita_guest.a'
COMBINED = 'build/recomp/librecomp.a'


def build_modes(out, host_cc):
    stage = out / 'make-fixture'
    stage.mkdir()
    makefile = (ROOT / 'Makefile').read_text()
    runtime = (ROOT / 'games/halo_ce_3925/runtime.mk').read_text()
    epoch = 1_000_000_000

    def put(name, text='', generated=False):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, (epoch + 10 * generated, epoch + 10 * generated))

    put('Makefile', makefile)
    put('games/halo_ce_3925/runtime.mk', runtime)
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
                 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', MAIN,
                 'runtime/xv_ui_gxm.c', 'shaders/halo_shaders.json', 'recompiler/gen_layouts.py',
                 'recompiler/shader_recomp_gen.py', 'haloce/default.xbe',
                 'local/halo_ce_3925/game_manifest.json', 'tools/gen_native_bounds.py',
                 'tools/gen_native_clip.py', 'games/halo_ce_3925/clip_region.py',
                 'games/halo_ce_3925/hooks.py', 'recompiler/xita_recomp.py'):
        put(name)
    # Follow the production source list, retaining the combined archive target.
    system = re.search(r'^XITA_SYS_SRCS\s*:=\s*(.*)$', makefile.replace('\\\n', ''), re.M)
    assert system, 'production system source list missing'
    for suffix in re.findall(r'\$\(RECOMP_DIR\)/([\w./]+\.c)', system[1]):
        put('recomp/' + suffix, generated=True)
    for name in re.findall(r'\brecomp/[\w./]+\.c', runtime):
        put(name, generated=True)
    put('shaders/xv_layouts.h', generated=True)
    put(HEADER, '#pragma once\n#define COLLISION_TRAVERSAL_FIXTURE_VALUE 1\n')
    put(HOOK, '#ifdef ' + FEATURE + '\n#include "kernel/xk_collision_traversal.h"\n'
        'int collision_traversal_hook_fixture(void) { return COLLISION_TRAVERSAL_FIXTURE_VALUE; }\n#endif\n')
    put('recomp/code_001.c', '/* unrelated guest unit */\n')
    put(CONTROL, '#ifdef ' + FEATURE + '\n#include "xk_collision_traversal.h"\n'
        'int collision_traversal_control_fixture = ' + DEFAULT + ' + COLLISION_TRAVERSAL_FIXTURE_VALUE;\n#endif\n',
        generated=True)
    recorder = stage / 'record.py'
    recorder.write_text('''#!/usr/bin/env python3
import json, subprocess, sys
from pathlib import Path
args = sys.argv[1:]
with Path('commands.jsonl').open('a') as f:
    f.write(json.dumps(args) + '\\n')
if '-c' in args:
    source = args[args.index('-c') + 1]
    output = args[args.index('-o') + 1]
    # Preserve production preprocessor/dependency flags; only CPU code generation
    # is replaced. The compiler, not this recorder, discovers header dependencies.
    flags = [a for a in args if a.startswith(('-D', '-I')) or a in ('-MMD', '-MP')]
    command = HOST_CC + ['-std=gnu11', *flags, '-c', source, '-o', output]
else:
    assert args[0] == 'rcs', args
    command = [HOST_AR, *args]
subprocess.run(command, check=True)
'''.replace('HOST_CC', repr(host_cc)).replace('HOST_AR', repr(shutil.which('ar'))))
    recorder.chmod(0o755)
    archive = stage / 'fake-gcc-ar'
    shutil.copyfile(recorder, archive)
    archive.chmod(0o755)
    base = ['make', '--no-print-directory', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + shlex.join([sys.executable, str(recorder)]), 'PREFIX=' + str(stage / 'fake')]
    targets = [GAME, GUEST, COMBINED, 'build/runtime/main.o', 'build/runtime/xv_ui_gxm.o']
    log = stage / 'commands.jsonl'
    rows = []

    def run_mode(enabled, default, label):
        log.write_text('')
        settings = ([FEATURE + '=' + str(enabled)] if enabled is not None else [])
        settings += ([DEFAULT + '=' + str(default)] if default is not None else [])
        command = base + settings + targets
        run = subprocess.run(command, cwd=stage, capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c') + 1]: c for c in calls if '-c' in c}
        archives = {c[1] for c in calls if c[0] == 'rcs'}
        on, initial = bool(enabled), default or 0
        for source, flags in compiles.items():
            defaults = [f for f in flags if f.startswith('-D' + DEFAULT)]
            assert defaults == ([f'-D{DEFAULT}={initial}'] if source == CONTROL else []), (source, defaults)
        for source in (HOOK, CONTROL):
            if source in compiles:
                assert ('-D' + FEATURE in compiles[source]) == on, (source, compiles[source])
        for name in (GAME, COMBINED):
            members = subprocess.check_output(['ar', 't', str(stage / name)], text=True).splitlines()
            assert ('xk_collision_traversal_control.o' in members) == on, (name, members)
        assert (stage / 'build/recomp/collision-traversal.config').read_text().strip() == str(int(on))
        if on:
            assert (stage / 'build/recomp/collision-traversal-startup.config').read_text().strip() == str(initial)
            dep = stage / 'build/recomp/kernel/xk_collision_traversal_control.d'
            assert dep.exists() and 'xk_collision_traversal.h' in dep.read_text(), 'real -MMD dependency missing'
        rows.append(dict(label=label, enabled=enabled, default=default, command=command,
                         commands=calls, compiles=sorted(compiles), archives=sorted(archives)))
        return set(compiles), archives

    # Absent flags must be the same default-OFF build as explicit zeros.
    run_mode(None, None, 'repository-defaults')
    assert run_mode(0, 0, 'explicit-defaults-noop') == (set(), set())
    prior = (0, 0)
    for enabled, default in ((1, 0), (1, 1), (1, 1), (1, 0), (0, 0), (0, 1), (1, 1), (1, None)):
        compiles, archives = run_mode(enabled, default, 'mode-transition')
        effective = (enabled, default or 0)
        if effective == prior or (not enabled and not prior[0]):
            assert not compiles and not archives, rows[-1]
        elif enabled == prior[0]:
            assert compiles == {CONTROL} and archives == {GAME, COMBINED}, rows[-1]
        else:
            expected = {HOOK} | ({CONTROL} if enabled else set())
            assert compiles == expected and archives == {GAME, GUEST, COMBINED}, rows[-1]
        prior = effective
    # A changed inline header must invalidate the generated hook even though its
    # generic code_%.o recipe does not emit dependencies, and the native control
    # through the real dependency file (or an explicit production dependency).
    with (stage / HEADER).open('a') as file:
        file.write('\n/* dependency invalidation witness */\n')
    assert run_mode(1, 0, 'header-edit') == ({HOOK, CONTROL}, {GAME, GUEST, COMBINED}), rows[-1]
    assert run_mode(1, 0, 'header-noop') == (set(), set()), rows[-1]
    for bad in ('', '2', '-1', '0 1'):
        run = subprocess.run(base + [DEFAULT + '=' + bad, GAME], cwd=stage,
                             capture_output=True, text=True)
        assert run.returncode and DEFAULT + ' must be 0 or 1' in run.stderr, (bad, run.stdout, run.stderr)
        rows.append(dict(label='invalid-default', value=bad, returncode=run.returncode, stderr=run.stderr))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--host-cc', default='cc', help='host C compiler command for small fixture inputs')
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    assert shutil.which('ar'), 'host ar is required'
    rows = build_modes(args.output_dir, shlex.split(args.host_cc))
    report = dict(result='PASS', builds=rows, sources={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                  for name in ('Makefile', 'games/halo_ce_3925/runtime.mk', 'tools/test_collision_traversal_build.py')},
                  scope='Production make graph and real host -MMD; fixture C inputs, no engine execution or ARM build')
    (args.output_dir / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS collision traversal production Makefile: feature/default transitions, archive membership, '
          'target-only defaults, inline-header invalidation, real -MMD, no-op rebuilds, invalid defaults')


if __name__ == '__main__':
    main()
