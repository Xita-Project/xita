#!/usr/bin/env python3
"""Test production query-boundary startup state/log, restoration and make rules.

Host fixtures extract the actual main.c state, startup log and selector-39 write
branch, and execute the real comparison controller. A separate private fixture
runs the production Makefile with real host compilation of small C inputs. No
device, SDK build, owned image or generated engine execution is needed.
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
FEATURE = 'XV_QUERY_BOUNDARY'
DEFAULT = FEATURE + '_DEFAULT'


def conditional(text, start):
    depth = 0
    for match in re.finditer(r'^#\s*(if\w*|endif)\b.*$', text[start:], re.M):
        depth += -1 if match[1] == 'endif' else 1
        if depth == 0:
            return text[start:start + match.end()] + '\n'
    raise AssertionError('unterminated production conditional')


def startup(out, cc):
    source = (ROOT / 'runtime/main.c').read_text()
    state = conditional(source, source.index('#ifdef XV_QUERY_BOUNDARY\n#ifndef XV_QUERY_BOUNDARY_DEFAULT'))
    at = source.index('#ifdef XV_QUERY_BOUNDARY\n    XV_LOG("[query-boundary] process-start')
    log = conditional(source, at)
    entry = source.index('int main(int argc, char *argv[])')
    dashboard = source.index('int dashboard_result=xv_dashboard_start();', entry)
    configured = source.index('xv_pipeline_configure();', dashboard)
    pump = source.index('sceKernelStartThread(pump', configured)
    assert configured < at < pump and source.count('[query-boundary] process-start mode') == 1
    begin = source.index('    if (xv_benchmark_compare_query_boundary()) {')
    end = source.index('    if (xv_benchmark_compare_polygon_edge()) {', begin)
    branch = source[begin:end]
    assert '__atomic_store_n(&g_query_boundary_override,enabled>0,__ATOMIC_RELEASE)' in branch
    fixture = out / 'startup.c'
    fixture.write_text(r'''#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "runtime/xv_benchmark.h"
static unsigned startup_logs, drains, values[16];
static void startup_log(const char *format, ...) {
    assert(strstr(format,"[query-boundary] process-start mode %d"));
    va_list args; va_start(args,format); assert(va_arg(args,int)==EXPECTED_MODE); va_end(args);
    startup_logs++;
}
#define XV_LOG startup_log
''' + state + '\n#include "runtime/xv_benchmark.c"\n' + r'''
void xv_logf(const char *format, ...) { (void)format; }
void xv_benchmark_optimizations(int enabled) {
    assert(enabled==0 || enabled==1);
    assert(drains<16); values[drains++]=(unsigned)enabled;
''' + branch + r'''
    assert(!"unexpected comparison path");
}
static const float view[6]={1,2,3,0,1,0};
static void trial(unsigned stop) {
#ifdef XV_QUERY_BOUNDARY
    assert(xv_query_boundary_enabled()==EXPECTED_MODE);
    drains=0; xv_benchmark_remote_poll(1);
    assert(!xv_benchmark_remote_request(XV_BENCH_QUERY_BOUNDARY));
    xv_benchmark_remote_poll(1);
    uint64_t now=1; unsigned height=xv_benchmark_step(now,544,1,view);
    assert(height==544 && !xv_query_boundary_enabled() && drains==1);
    xv_benchmark_applied(now,height);
    for(unsigned i=0;i<1000 && xv_benchmark_active();i++) {
        now+=50000;
        if(i==210 && stop==1)xv_benchmark_compare_toggle();
        height=xv_benchmark_step(now,544,!(i==210 && stop==2),view);
        if(height)xv_benchmark_applied(now,height);
    }
    assert(!xv_benchmark_active() && !xv_benchmark_remote_busy());
    assert(xv_query_boundary_enabled()==EXPECTED_MODE);
    assert(values[0]==0 && values[1]==1 && values[drains-1]==EXPECTED_MODE);
    assert(drains==(stop?3u:4u));
#else
    (void)stop;
#endif
}
int main(void) {
#ifdef XV_QUERY_BOUNDARY
    /* No startup setter: this tests the production static initializer. */
    assert(xv_query_boundary_available() && xv_query_boundary_enabled()==EXPECTED_MODE);
#endif
''' + log + r'''
#ifdef XV_QUERY_BOUNDARY
    assert(startup_logs==1 && !drains);
    for(unsigned stop=0;stop<3;stop++)trial(stop);
    if(EXPECTED_MODE) {
        drains=0; xv_benchmark_remote_poll(1);
        assert(!xv_benchmark_remote_request(XV_BENCH_EARLY_VISIBILITY));
        xv_benchmark_remote_poll(1);
        assert(!xv_benchmark_step(1,544,1,view) && !drains && xv_query_boundary_enabled());
    }
#else
    assert(!startup_logs && !drains);
    xv_benchmark_remote_poll(1);
    assert(xv_benchmark_remote_request(XV_BENCH_QUERY_BOUNDARY)<0);
    assert(!xv_query_boundary_available && !xv_query_boundary_enabled);
#endif
    puts("PASS actual startup state/log and selector39 normal/cancel/lost-view restoration");
}
''')
    rows = []
    for feature in (0, 1):
        for default in (None, 0, 1):
            expected = (default or 0) if feature else 0
            binary = out / f'startup-{feature}-{default}'
            command = cc + ['-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-const-variable', '-Wno-address',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-no-pie', '-I' + str(ROOT),
                f'-DEXPECTED_MODE={expected}', *(['-D' + FEATURE] if feature else []),
                *([f'-D{DEFAULT}={default}'] if default is not None else []),
                str(fixture), '-lm', '-o', str(binary)]
            subprocess.run(command, check=True)
            run = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            rows.append(dict(feature=feature, default=default, expected=expected,
                             command=command, stdout=run.stdout.strip()))
    for bad in ('2', '-1'):
        run = subprocess.run(cc + ['-std=gnu11', '-I' + str(ROOT), '-D' + FEATURE,
            '-D' + DEFAULT + '=' + bad, '-DEXPECTED_MODE=0', '-fsyntax-only', str(fixture)],
            capture_output=True, text=True)
        assert run.returncode and DEFAULT + ' must be 0 or 1' in run.stderr
    return rows


def build_modes(out, cc):
    stage = out / 'make-fixture'
    stage.mkdir()
    epoch = 1_000_000_000

    def put(name, text='', generated=False):
        p = stage / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        os.utime(p, (epoch + 10 * generated, epoch + 10 * generated))

    put('Makefile', (ROOT / 'Makefile').read_text())
    put('games/halo_ce_3925/runtime.mk', (ROOT / 'games/halo_ce_3925/runtime.mk').read_text())
    for name in ('recomp/xv_recomp_protos.h', 'recomp/xv_x86rt.h', 'recomp/xv_phase.h',
                 'recomp/xv_fn_table.c', 'recomp/xv_stubs_default.c', 'recomp/code_000.c',
                 'runtime/main.c', 'runtime/xv_d3d.c', 'runtime/xv_ui_gxm.c',
                 'shaders/halo_shaders.json', 'recompiler/gen_layouts.py',
                 'recompiler/shader_recomp_gen.py', 'haloce/default.xbe',
                 'local/halo_ce_3925/game_manifest.json'):
        put(name)
    put('shaders/xv_layouts.h', generated=True)
    recorder = stage / 'record.py'
    recorder.write_text('''#!/usr/bin/env python3
import json, subprocess, sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as file:file.write(json.dumps(args)+'\\n')
if '-c' in args:
    flags=[a for a in args if a.startswith(('-D','-I')) or a in ('-MMD','-MP')]
    command=HOST_CC+['-std=gnu11',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]]
else:
    assert args[0]=='rcs'
    command=[HOST_AR,*args]
subprocess.run(command,check=True)
'''.replace('HOST_CC', repr(cc)).replace('HOST_AR', repr(shutil.which('ar'))))
    recorder.chmod(0o755)
    ar = stage / 'fake-gcc-ar'
    shutil.copyfile(recorder, ar)
    ar.chmod(0o755)
    base = ['make', '--no-print-directory', 'RECOMP=1', 'VITASDK=' + str(stage),
            'CC=' + shlex.join([sys.executable, str(recorder)]), 'PREFIX=' + str(stage / 'fake')]
    targets = ['build/runtime/main.o', 'build/runtime/xv_d3d.o', 'build/runtime/xv_ui_gxm.o',
               'build/recomp/libxita_guest.a']
    log = stage / 'commands.jsonl'
    rows = []
    previous = None
    modes = [(None, None), (0, 0), (1, 0), (1, 1), (1, 1), (1, 0), (0, 0), (0, 1), (1, 1), (1, None)]
    for feature, default in modes:
        log.write_text('')
        settings = ([FEATURE + '=' + str(feature)] if feature is not None else [])
        settings += ([DEFAULT + '=' + str(default)] if default is not None else [])
        command = base + settings + targets
        run = subprocess.run(command, cwd=stage, capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c') + 1]: c for c in calls if '-c' in c}
        effective = (feature or 0, (default or 0) if feature else 0)
        for source, flags in compiles.items():
            wanted = [f'-D{DEFAULT}={effective[1]}'] if feature and source == 'runtime/main.c' else []
            assert [f for f in flags if f.startswith('-D' + DEFAULT)] == wanted, (source, flags)
            assert ('-D' + FEATURE in flags) == bool(feature and source in ('runtime/main.c','runtime/xv_d3d.c'))
        assert (stage / 'build/query-boundary.config').read_text().strip() == str(effective[0])
        assert (stage / 'build/query-boundary-startup.config').read_text().strip() == str(effective[1])
        if previous is not None:
            wanted = (set() if effective == previous else
                      {'runtime/main.c'} if effective[0] == previous[0] else
                      {'runtime/main.c', 'runtime/xv_d3d.c'})
            assert set(compiles) == wanted and len(calls) == len(compiles), calls
        rows.append(dict(feature=feature, default=default, commands=calls, command=command))
        previous = effective
    for bad in ('', '2', '-1', '0 1'):
        run = subprocess.run(base + [DEFAULT + '=' + bad, targets[0]], cwd=stage,
                             capture_output=True, text=True)
        assert run.returncode and DEFAULT + ' must be 0 or 1' in run.stderr
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--host-cc', default='cc')
    args = parser.parse_args()
    args.output_dir = args.output_dir.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    assert shutil.which('ar'), 'host ar is required'
    cc = shlex.split(args.host_cc)
    report = dict(result='PASS', startup=startup(args.output_dir, cc),
                  build_modes=build_modes(args.output_dir, cc), sources={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                    for name in ('Makefile','runtime/main.c','runtime/xv_benchmark.c','tools/test_query_boundary_startup.py')})
    (args.output_dir / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS 6 actual startup/controller ASan+UBSan builds, 10 production Makefile transitions, invalid defaults')


if __name__ == '__main__':
    main()
