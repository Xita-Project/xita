#!/usr/bin/env python3
"""Exercise the real shared-generation Make graph with synthetic C inputs.

The actual generator/ARM identity is checked separately. Here a recording host
compiler and real archives check incremental modes, flags, missing outputs,
dependency files, profiling transitions, and restoration of the query-only app.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FEATURE = 'XV_NATIVE_SOLVER_FUSION'
ARCHIVES = {'build/recomp/libxita_game.a', 'build/recomp/libxita_guest.a', 'build/recomp/librecomp.a'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-dir', type=Path, required=True)
    stage = p.parse_args().output_dir.resolve()
    stage.mkdir(parents=True, exist_ok=False)
    epoch = 1_000_000_000
    def put(name, text='', generated=False):
        path = stage / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text); os.utime(path, (epoch + 10*generated,) * 2)
    makefile = (ROOT / 'Makefile').read_text()
    runtime = (ROOT / 'games/halo_ce_3925/runtime.mk').read_text()
    put('Makefile', makefile); put('games/halo_ce_3925/runtime.mk', runtime)
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
        'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', 'runtime/main.c',
        'shaders/halo_shaders.json', 'recompiler/gen_layouts.py', 'recompiler/shader_recomp_gen.py',
        'haloce/default.xbe', 'local/halo_ce_3925/game_manifest.json',
        'tools/gen_native_bounds.py', 'tools/gen_native_clip.py', 'tools/prototype_collision_query.py',
        'tools/prototype_collision_solver.py', 'tools/gen_native_solver_fusion.py',
        'tools/tests/collision_query_fusion.c', 'games/halo_ce_3925/clip_region.py',
        'games/halo_ce_3925/hooks.py', 'recompiler/xita_recomp.py',
        'recomp/kernel/xk_collision_vertices.h', 'recomp/kernel/xk_segment_sphere.h',
        'recomp/kernel/xk_collision_traversal.h', 'recomp/kernel/xk_object_solver.h'):
        put(name)
    system = re.search(r'^XITA_SYS_SRCS\s*:=\s*(.*)$', makefile.replace('\\\n', ''), re.M)
    for suffix in re.findall(r'\$\(RECOMP_DIR\)/([\w./]+\.c)', system[1]):
        put('recomp/' + suffix, generated=True)
    for name in re.findall(r'\brecomp/[\w./]+\.c', runtime):
        if name not in ('recomp/query_fusion.c', 'recomp/solver_fusion.c'):
            put(name, generated=True)
    put('shaders/xv_layouts.h', generated=True)
    for i in (0, 13, 16, 28):
        put(f'recomp/code_{i:03d}.c', f'int generic_{i}(void) {{ return {i}; }}\n', True)
    put('tools/gen_native_query_fusion.py', '''import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--xbe');p.add_argument('--manifest');p.add_argument('--recomp-dir');p.add_argument('--receipt');p.add_argument('--solver-fusion',type=int,default=0);a=p.parse_args()
r=Path(a.recomp_dir)
with Path('generations.log').open('a') as f:f.write(str(a.solver_fusion)+'\\n')
body='extern int query_fixture(void);\\nint generic_28(void){return query_fixture();}\\n'
if a.solver_fusion:
 body='#if XV_NATIVE_SOLVER_FUSION\\nextern int solver_fixture(void);\\nextern int query_fixture(void);\\nint generic_28(void){return query_fixture()+solver_fixture();}\\n#else\\n'+body+'#endif\\n'
texts={'code_028.c':body,'query_fusion.c':'#include "kernel/xk_collision_traversal.h"\\nint query_fixture(void){return 42;}\\n'}
if a.solver_fusion:
 texts.update({'solver_fusion.c':'#if XV_NATIVE_SOLVER_FUSION\\n#include "solver_primitives.h"\\nint solver_fixture(void){return primitive();}\\n#endif\\n','solver_primitives.h':'static inline int primitive(void){return 17;}\\n'})
for name,text in texts.items():
 out=r/name
 if not out.exists() or out.read_text()!=text:out.write_text(text)
stamp=Path(a.receipt);stamp.parent.mkdir(parents=True,exist_ok=True);stamp.write_text('{}\\n')
''')
    recorder = stage / 'record.py'
    recorder.write_text('''#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
a=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(a)+'\\n')
if '-c' in a:
 flags=[x for x in a if x.startswith(('-D','-I')) or x in ('-MMD','-MP','-ffunction-sections','-fdata-sections','-fstack-usage')]
 command=['cc','-O2','-std=gnu11',*flags,'-c',a[a.index('-c')+1],'-o',a[a.index('-o')+1]]
else:command=['ar',*a]
subprocess.run(command,check=True)
''')
    recorder.chmod(0o755); shutil.copy2(recorder, stage / 'fake-gcc-ar')
    base = ['make', '--no-print-directory', '-j4', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + str(recorder), 'PREFIX=' + str(stage / 'fake'), 'PYTHON=' + sys.executable]
    settings = [name + '=1' for name in ('XV_NATIVE_QUERY_FUSION', 'XV_NATIVE_BSP_SPHERE',
        'XV_NATIVE_COLLISION_VERTICES', 'XV_NATIVE_SEGMENT_SPHERE', 'XV_NATIVE_COLLISION_TRAVERSAL',
        'XV_EXPERIMENTAL_OBJECT_JOBS')]
    targets = sorted(ARCHIVES) + ['build/runtime/main.o']
    rows = []; log = stage / 'commands.jsonl'
    def run(mode, label, profile=1):
        log.write_text('')
        command = base + settings + ['XV_OBJECT_HOLD_PROFILE=' + str(profile)]
        if mode is not None: command += [FEATURE + '=' + str(mode)]
        command += targets
        proc = subprocess.run(command, cwd=stage, capture_output=True, text=True)
        assert not proc.returncode, proc.stdout + proc.stderr
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c')+1]: c for c in calls if '-c' in c}
        archives = {c[1] for c in calls if c[0] == 'rcs'}
        for name, flags in compiles.items():
            assert ('-D' + FEATURE + '=1' in flags) == bool(mode and name in ('recomp/code_028.c', 'recomp/solver_fusion.c')), (name, flags)
            if name in ('recomp/query_fusion.c', 'recomp/solver_fusion.c'):
                assert ('-DXV_OBJECT_HOLD_PROFILE' in flags) == bool(profile)
        for archive in ('build/recomp/libxita_game.a', 'build/recomp/librecomp.a'):
            members = subprocess.check_output(['ar', 't', str(stage / archive)], text=True)
            assert 'query_fusion.o' in members and ('solver_fusion.o' in members) == bool(mode)
        assert (stage / 'build/recomp/solver-fusion.config').read_text() == str(int(bool(mode))) + '\n'
        rows.append(dict(label=label, command=command, compiles=compiles, archives=sorted(archives)))
        return set(compiles), archives
    run(None, 'repository-default')
    original = (stage / 'build/recomp/code_028.o').read_bytes()
    stable = {name: ((stage / name).stat().st_mtime_ns, hashlib.sha256((stage / name).read_bytes()).hexdigest())
        for name in ('build/recomp/code_000.o', 'build/recomp/code_013.o', 'build/recomp/code_016.o', 'build/runtime/main.o')}
    on = {'recomp/code_028.c', 'recomp/query_fusion.c', 'recomp/solver_fusion.c'}
    off = {'recomp/code_028.c', 'recomp/query_fusion.c'}
    assert run(0, 'explicit-off-noop') == (set(), set())
    assert run(1, 'enable') == (on, ARCHIVES)
    assert 'solver_primitives.h' in (stage / 'build/recomp/solver_fusion.d').read_text()
    assert run(1, 'on-noop') == (set(), set())
    assert run(0, 'disable') == (off, ARCHIVES)
    assert (stage / 'build/recomp/code_028.o').read_bytes() == original
    assert run(0, 'off-noop') == (set(), set())
    assert run(1, 'reenable') == (on, ARCHIVES)
    for name in ('recomp/solver_fusion.c', 'recomp/solver_primitives.h', 'build/recomp/query-fusion.generated.json'):
        (stage / name).unlink()
        assert run(1, 'missing-' + name) == (on, ARCHIVES)
        assert run(1, 'repaired-noop') == (set(), set())
    for name in ('tools/prototype_collision_solver.py',):
        with (stage / name).open('a') as f: f.write('/* changed generation input */\n' if name.endswith('.h') else '# changed generation input\n')
        assert run(1, 'changed-' + name) == (on, ARCHIVES)
        assert run(1, 'input-noop') == (set(), set())
    for name, expected in stable.items():
        path = stage / name
        assert (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()) == expected, name
    # A real shared primitive-header edit intentionally invalidates every guest
    # unit; solver mode changes above must not do so.
    with (stage / 'recomp/xv_x86rt.h').open('a') as f:
        f.write('/* changed shared primitive header */\n')
    generic = {'recomp/code_000.c', 'recomp/code_013.c', 'recomp/code_016.c'}
    assert run(1, 'changed-shared-primitive-header') == (on | generic, ARCHIVES)
    assert run(1, 'header-noop') == (set(), set())
    scoped = on | {'recomp/code_000.c', 'recomp/code_013.c', 'recomp/code_016.c', 'recomp/kernel/xk_object_jobs.c'}
    assert run(1, 'profile-off', 0) == (scoped, ARCHIVES)
    assert run(1, 'profile-off-noop', 0) == (set(), set())
    assert run(1, 'profile-on', 1) == (scoped, ARCHIVES)
    for setting in ([FEATURE+'='], [FEATURE+'=2'], [FEATURE+'=-1'], [FEATURE+'=0 1'],
                    [FEATURE+'=1', 'XV_NATIVE_QUERY_FUSION=0'], [FEATURE+'=1', 'XV_OBJECT_SOLVER_EXPERIMENT=1']):
        proc = subprocess.run(base + settings + setting + [targets[0]], cwd=stage, capture_output=True, text=True)
        assert proc.returncode and FEATURE in proc.stderr, setting
    (stage / 'result.json').write_text(json.dumps(dict(result='PASS', rows=rows,
        generic_and_main_unchanged_across_solver_modes=True, query_only_off_object_restored=True,
        profile_transitions_rebuild_both_fused_scopes=True,
        scope='real Makefile/compiler/archive/dependency recipes with synthetic owned generator'), indent=2) + '\n')
    print('PASS solver Make graph: default OFF, ON/OFF, one generator, no-op, missing outputs, precise objects, profile transitions, strict flags')


if __name__ == '__main__':
    main()
