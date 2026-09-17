#!/usr/bin/env python3
"""Check actual startup logging/configuration and production Makefile invalidation.

No owned image is needed. The full generated-path oracle is separately run by
test_object_collect.py with --startup-default and the four startup modes.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def startup(out):
    main = (ROOT / 'runtime/main.c').read_text()
    block = re.search(r'#ifdef XV_NATIVE_OBJECT_COLLECT\n'
                      r'    \{ extern int xv_object_collect_enabled\(void\);.*?\n#endif',
                      main, re.S)
    assert block and main.count('[object-collect] process-start mode') == 1
    entry = main.index('int main(int argc, char *argv[])')
    dashboard = main.index('int dashboard_result=xv_dashboard_start();', entry)
    configured = main.index('xv_pipeline_configure();', dashboard)
    threads = main.index('sceKernelStartThread(pump', configured)
    assert configured < block.start() < threads
    assert 'xv_load_settings(); /* All game consumers initialize after this hand-off. */' in main
    fixture = out / 'startup.c'
    fixture.write_text('''#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int xv_object_collect_enabled(void);
static int observed=-1, logs;
static void startup_log(const char *format,...)
{
    assert(strstr(format,"[object-collect] process-start mode %d"));
    va_list args;va_start(args,format);observed=va_arg(args,int);va_end(args);logs++;
}
#define XV_LOG startup_log
void __wrap_xv_object_collect_override(int value){(void)value;abort();}
int main(int argc,char **argv)
{
    assert(argc==3);int expected=atoi(argv[2]);
    assert(!unsetenv("XV_NATIVE_OBJECT_COLLECT"));
    /* This represents the dashboard's final configuration handoff. */
    if(strcmp(argv[1],"absent"))assert(!setenv("XV_NATIVE_OBJECT_COLLECT",argv[1],1));
''' + block.group() + '''
    assert(logs==1 && observed==expected && xv_object_collect_enabled()==expected);
    /* Resolution is cached; this is not a mode change or a runtime control. */
    assert(!setenv("XV_NATIVE_OBJECT_COLLECT",expected?"0":"1",1));
    assert(xv_object_collect_enabled()==expected);
    printf("PASS startup default/environment/log: %s -> %d\\n",argv[1],observed);
}
''')
    rows = []
    for default in (None, 0, 1):
        binary = out / ('default-' + str(default))
        cmd = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11',
               '-fno-strict-aliasing', '-ffp-contract=off', '-ffunction-sections',
               '-fdata-sections', '-DXV_NATIVE_OBJECT_COLLECT',
               *([f'-DXV_NATIVE_OBJECT_COLLECT_DEFAULT={default}'] if default is not None else []),
               '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'),
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-no-pie', str(fixture),
               str(ROOT / 'recomp/kernel/xk_object_collect.c'),
               '-Wl,--gc-sections,--wrap=xv_object_collect_override', '-lm', '-o', str(binary)]
        subprocess.run(cmd, check=True)
        for setting in ('absent', '0', '1', ''):
            expected = (default or 0) if setting == 'absent' else int(setting == '1')
            run = subprocess.run([str(binary), setting, str(expected)], check=True,
                                 capture_output=True, text=True)
            rows.append(dict(default=default, environment=setting, expected=expected,
                             stdout=run.stdout.strip(), command=cmd))
    return rows


def build_modes(out):
    stage = out / 'make-fixture'
    stage.mkdir()
    epoch = 1_000_000_000

    def put(name, text='', generated=False):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, (epoch + 10 * generated, epoch + 10 * generated))

    put('Makefile', (ROOT / 'Makefile').read_text())
    put('games/halo_ce_3925/runtime.mk', (ROOT / 'games/halo_ce_3925/runtime.mk').read_text())
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
                 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', 'runtime/main.c',
                 'runtime/xv_ui_gxm.c', 'shaders/halo_shaders.json', 'recompiler/gen_layouts.py',
                 'recompiler/shader_recomp_gen.py', 'haloce/default.xbe',
                 'local/halo_ce_3925/game_manifest.json', 'tools/gen_native_bounds.py',
                 'tools/gen_native_clip.py', 'games/halo_ce_3925/clip_region.py',
                 'games/halo_ce_3925/hooks.py', 'recompiler/xita_recomp.py'):
        put(name)
    for name in ('xk_quality', 'xk_math', 'xk_clip', 'xk_bounds', 'xk_flare',
                 'xk_geometry', 'xk_object_collect'):
        put('recomp/kernel/' + name + '.c', generated=True)
    put('shaders/xv_layouts.h', generated=True)
    put('recomp/code_000.c', '#ifdef XV_NATIVE_OBJECT_COLLECT\n#endif\n')
    put('recomp/code_001.c', '/* unchanged ordinary guest unit */\n')
    record = stage / 'record.py'
    record.write_text('''#!/usr/bin/env python3
import json,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
if '-o' in args:
    output=Path(args[args.index('-o')+1]);record={'compile':args}
else:
    assert args[0]=='rcs';output=Path(args[1]);record={'members':args[2:]}
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(record))
''')
    record.chmod(0o755)
    archive = stage / 'fake-gcc-ar'
    shutil.copyfile(record, archive)
    archive.chmod(0o755)
    base = ['make', '--no-print-directory', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + shlex.join([sys.executable, str(record)]), 'PREFIX=' + str(stage / 'fake')]
    targets = ['build/recomp/libxita_game.a', 'build/recomp/libxita_guest.a',
               'build/runtime/main.o', 'build/runtime/xv_ui_gxm.o']
    log = stage / 'commands.jsonl'
    rows = []
    transitions = [(0, 0), (1, 0), (1, 1), (1, 1), (1, 0), (0, 0), (0, 1), (1, 1)]
    prior = None
    for enabled, default in transitions:
        log.write_text('')
        cmd = base + [f'XV_NATIVE_OBJECT_COLLECT={enabled}',
                      f'XV_NATIVE_OBJECT_COLLECT_DEFAULT={default}', *targets]
        run = subprocess.run(cmd, cwd=stage, capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c') + 1]: c for c in calls if '-c' in c}
        for source, flags in compiles.items():
            defaults = [f for f in flags if f.startswith('-DXV_NATIVE_OBJECT_COLLECT_DEFAULT')]
            assert defaults == ([f'-DXV_NATIVE_OBJECT_COLLECT_DEFAULT={default}']
                                if source == 'recomp/kernel/xk_object_collect.c' else [])
            assert ('-DXV_NATIVE_OBJECT_COLLECT' in flags) == bool(enabled)
            if source == 'recomp/kernel/xk_object_collect.c':
                assert '-ffp-contract=off' in flags
        members = json.loads((stage / 'build/recomp/libxita_game.a').read_text())['members']
        assert ('build/recomp/kernel/xk_object_collect.o' in members) == bool(enabled)
        assert (stage / 'build/recomp/object-collect.config').read_text().strip() == str(enabled)
        if enabled:
            assert (stage / 'build/recomp/object-collect-startup.config').read_text().strip() == str(default)
        if prior is not None:
            if prior == (enabled, default) or (not enabled and not prior[0]):
                assert not calls, calls
            elif prior[0] == enabled:
                assert set(compiles) == {'recomp/kernel/xk_object_collect.c'}, compiles
                assert [c[1] for c in calls if c[0] == 'rcs'] == ['build/recomp/libxita_game.a']
            else:
                assert {'recomp/code_000.c', 'runtime/main.c', 'runtime/xv_ui_gxm.c'} <= set(compiles)
                assert 'recomp/code_001.c' not in compiles
        rows.append(dict(enabled=enabled, default=default, commands=calls))
        prior = enabled, default
    for bad in ('', '2', '-1', '0 1'):
        run = subprocess.run(base + ['XV_NATIVE_OBJECT_COLLECT_DEFAULT=' + bad, targets[0]],
                             cwd=stage, capture_output=True, text=True)
        assert run.returncode and 'XV_NATIVE_OBJECT_COLLECT_DEFAULT must be 0 or 1' in run.stderr
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    report = dict(startup=startup(args.output_dir), build_modes=build_modes(args.output_dir))
    (args.output_dir / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS 12 actual helper/startup-log cases; production Makefile feature/default transitions and isolated helper flags')


if __name__ == '__main__':
    main()
