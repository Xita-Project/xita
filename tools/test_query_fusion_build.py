#!/usr/bin/env python3
"""Exercise the real Makefile on small C inputs; no owned image or SDK needed.

Only the owned-image generator is replaced in this graph fixture. The separate
source-contract test executes the actual generator against retained owned data.
The recording compiler runs real host C compilation and -MMD discovery.
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
FEATURE = 'XV_NATIVE_QUERY_FUSION'
PREREQS = ('XV_NATIVE_BSP_SPHERE', 'XV_NATIVE_COLLISION_VERTICES',
           'XV_NATIVE_SEGMENT_SPHERE', 'XV_NATIVE_COLLISION_TRAVERSAL')
ARCHIVES = {'build/recomp/libxita_game.a', 'build/recomp/libxita_guest.a', 'build/recomp/librecomp.a'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    stage = args.output_dir.resolve()
    stage.mkdir(parents=True, exist_ok=False)
    epoch = 1_000_000_000

    def put(name, text='', generated=False):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, (epoch + 10 * generated,) * 2)

    makefile = (ROOT / 'Makefile').read_text()
    runtime = (ROOT / 'games/halo_ce_3925/runtime.mk').read_text()
    put('Makefile', makefile)
    put('version.json', (ROOT / 'version.json').read_text())
    put('tools/gen_build_version.py', (ROOT / 'tools/gen_build_version.py').read_text())
    put('games/halo_ce_3925/runtime.mk', runtime)
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
                 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', 'runtime/main.c', 'runtime/xv_packed_vertex.h',
                 'shaders/halo_shaders.json', 'recompiler/gen_layouts.py', 'recompiler/shader_recomp_gen.py',
                 'haloce/default.xbe', 'local/halo_ce_3925/game_manifest.json',
                 'tools/gen_native_bounds.py', 'tools/gen_native_clip.py', 'tools/prototype_collision_query.py', 'tools/query_f32_primitives.py', 'tools/query_semantic_leaf.py','tools/query_membership_scalar.py','tools/query_ancestor_scalar.py','tools/query_object_space.py',
                 'tools/tests/collision_query_fusion.c', 'tools/gen_native_solver_fusion.py', 'games/halo_ce_3925/clip_region.py',
                 'games/halo_ce_3925/hooks.py','games/halo_ce_3925/discovery.py',
                 'recomp/kernel/xk_owner_phase.h','recomp/kernel/xk_model_fog.h', 'recompiler/xita_recomp.py',
                 'recomp/kernel/xk_collision_vertices.h', 'recomp/kernel/xk_segment_sphere.h',
                 'recomp/kernel/xk_collision_traversal.h'):
        put(name)
    system = re.search(r'^XITA_SYS_SRCS\s*:=\s*(.*)$', makefile.replace('\\\n', ''), re.M)
    for suffix in re.findall(r'\$\(RECOMP_DIR\)/([\w./]+\.c)', system[1]):
        put('recomp/' + suffix, generated=True)
    for name in re.findall(r'\brecomp/[\w./]+\.c', runtime):
        if name != 'recomp/query_fusion.c':
            put(name, generated=True)
    put('shaders/xv_layouts.h', generated=True)
    put('recomp/code_013.c', 'int generic_query13(void) { return 13; }\n', True)
    put('recomp/code_016.c', 'int generic_query16(void) { return 16; }\n', True)
    put('recomp/code_028.c', 'int generic_caller28(void) { return 28; }\n', True)
    put('tools/gen_native_query_fusion.py', '''import argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--xbe');p.add_argument('--manifest');p.add_argument('--recomp-dir');p.add_argument('--receipt');a=p.parse_args()
r=Path(a.recomp_dir)
with Path('generations.log').open('a') as f:f.write('generate\\n')
texts={'code_028.c': '#if XV_NATIVE_QUERY_FUSION\\nextern int fusion_fixture(void);\\nint generic_caller28(void){return fusion_fixture();}\\n#else\\nint generic_caller28(void) { return 28; }\\n#endif\\n',
'query_fusion.c': '#if XV_NATIVE_QUERY_FUSION\\n#include "kernel/xk_collision_traversal.h"\\nint fusion_fixture(void){return 42;}\\n#endif\\n'}
for name,text in texts.items():
 out=r/name
 if not out.exists() or out.read_text()!=text:out.write_text(text)
stamp=Path(a.receipt);stamp.parent.mkdir(parents=True,exist_ok=True);stamp.write_text('{}\\n')
''')
    recorder = stage / 'record.py'
    recorder.write_text('''#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
if '-c' in args:
 flags=[x for x in args if x.startswith(('-D','-I')) or x in ('-MMD','-MP')]
 command=['cc','-O2','-std=gnu11',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]]
else:command=['ar',*args]
subprocess.run(command,check=True)
''')
    recorder.chmod(0o755)
    shutil.copy2(recorder, stage / 'fake-gcc-ar')
    base = ['make', '--no-print-directory', '-j4', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + str(recorder), 'PREFIX=' + str(stage / 'fake'), 'PYTHON=' + sys.executable]
    settings = [p + '=1' for p in PREREQS]
    targets = sorted(ARCHIVES) + ['build/runtime/main.o']
    rows = []
    log = stage / 'commands.jsonl'

    def run(mode, label):
        log.write_text('')
        command = base + settings + ([] if mode is None else [FEATURE + '=' + str(mode)]) + targets
        proc = subprocess.run(command, cwd=stage, capture_output=True, text=True)
        assert proc.returncode == 0, proc.stdout + proc.stderr
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c') + 1]: c for c in calls if '-c' in c}
        archives = {c[1] for c in calls if c[0] == 'rcs'}
        for name, flags in compiles.items():
            assert ('-D' + FEATURE + '=1' in flags) == bool(mode and name in ('recomp/code_028.c', 'recomp/query_fusion.c')), (name, flags)
        for archive in ('build/recomp/libxita_game.a', 'build/recomp/librecomp.a'):
            members = subprocess.check_output(['ar', 't', str(stage / archive)], text=True)
            assert ('query_fusion.o' in members) == bool(mode), members
        assert (stage / 'build/recomp/query-fusion.config').read_text() == str(int(bool(mode))) + '\n'
        rows.append(dict(label=label, command=command, compiles=compiles, archives=sorted(archives)))
        return set(compiles), archives

    run(None, 'repository-default')
    assert not (stage / 'generations.log').exists(), 'OFF must not need owned generation'
    generic = {n: ((stage / n).stat().st_mtime_ns, hashlib.sha256((stage / n).read_bytes()).hexdigest())
               for n in ('build/recomp/code_013.o', 'build/recomp/code_016.o', 'build/runtime/main.o')}
    original_off = (stage / 'build/recomp/code_028.o').read_bytes()
    assert run(0, 'explicit-off-noop') == (set(), set())
    assert run(1, 'enable') == ({'recomp/code_028.c', 'recomp/query_fusion.c'}, ARCHIVES)
    assert (stage / 'generations.log').read_text().count('generate') == 1
    assert 'xk_collision_traversal.h' in (stage / 'build/recomp/query_fusion.d').read_text()
    assert run(1, 'on-noop') == (set(), set())
    assert run(0, 'disable') == ({'recomp/code_028.c'}, ARCHIVES)
    assert original_off == (stage / 'build/recomp/code_028.o').read_bytes(), 'OFF object changed'
    assert run(0, 'off-noop') == (set(), set())
    assert run(1, 'reenable') == ({'recomp/code_028.c', 'recomp/query_fusion.c'}, ARCHIVES)
    assert (stage / 'generations.log').read_text().count('generate') == 1
    for missing in ('recomp/query_fusion.c', 'build/recomp/query-fusion.generated.json'):
        (stage / missing).unlink()
        assert run(1, 'missing-' + missing) == ({'recomp/code_028.c', 'recomp/query_fusion.c'}, ARCHIVES)
        assert run(1, 'repaired-noop') == (set(), set())
    with (stage / 'tools/prototype_collision_query.py').open('a') as f:
        f.write('# changed generation input\n')
    assert run(1, 'generator-change') == ({'recomp/code_028.c', 'recomp/query_fusion.c'}, ARCHIVES)
    assert run(1, 'generator-noop') == (set(), set())
    for name, prior in generic.items():
        path = stage / name
        assert (path.stat().st_mtime_ns, hashlib.sha256(path.read_bytes()).hexdigest()) == prior, name
    for bad in ('', '2', '-1', '0 1'):
        proc = subprocess.run(base + settings + [FEATURE + '=' + bad, targets[0]], cwd=stage, capture_output=True, text=True)
        assert proc.returncode and FEATURE + ' must be 0 or 1' in proc.stderr
    for prerequisite in PREREQS:
        proc = subprocess.run(base + settings + [FEATURE + '=1', prerequisite + '=0', targets[0]], cwd=stage, capture_output=True, text=True)
        assert proc.returncode and FEATURE + ' requires' in proc.stderr
    (stage / 'result.json').write_text(json.dumps(dict(result='PASS', rows=rows,
        generic_and_main_objects_unchanged=True, restored_off_object_identical=True,
        scope='actual Makefile + real compiler/archive/dependency recipes; synthetic owned-generator inputs'), indent=2) + '\n')
    print('PASS query fusion: OFF/ON/OFF, separate archive member, unchanged generic objects, no-op, missing outputs, generator dependencies, strict flags')


if __name__ == '__main__':
    main()
