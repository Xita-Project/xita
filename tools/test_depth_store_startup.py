#!/usr/bin/env python3
"""Actual depth-store startup/controller extraction and real incremental Makefile.

First-render semantics are separately tested by test_depth_store.py. This script
does not run hardware or a live comparison; controller calls use a fake clock.
"""
import argparse,hashlib,json,os,pathlib,shlex,shutil,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args()
OUT=a.output_dir.resolve();OUT.mkdir(parents=True,exist_ok=False);rows=[]
def run(cmd,cwd=None,expected=0):
    r=subprocess.run([str(v) for v in cmd],cwd=cwd,capture_output=True,text=True)
    rows.append(dict(command=[str(v) for v in cmd],cwd=str(cwd) if cwd else None,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
    assert (r.returncode==0)==(expected==0),r.stdout+r.stderr
    return r
header=(ROOT/'runtime/xv_depth_store.h').read_text();main=(ROOT/'runtime/main.c').read_text()
state=header[header.index('#ifdef XV_DEPTH_STORE'):header.index('static int ds_draw_shader(')]+'\n#endif\n'
start=main.index('#ifdef XV_DEPTH_STORE\n    XV_LOG("[depth-store] process-start');end=main.index('#endif',start)+len('#endif')
banner=main[start:end]
entry=main.index('int main(int argc, char *argv[])');dashboard=main.index('int dashboard_result=xv_dashboard_start();',entry)
configured=main.index('xv_pipeline_configure();',dashboard);pump=main.index('sceKernelStartThread(pump',configured)
assert configured<start<end<pump and 'xv_depth_store_override' not in main[entry:pump]
start=main.index('    if (xv_benchmark_compare_depth_store()) {');end=main.index('    if (xv_benchmark_compare_vertex_blocks()) {',start)
branch=main[start:end]
fixture=r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/xv_benchmark.h"
static unsigned calls,values[16],logs;static int expected_available=1;
static void startup_log(const char *format,...) {
    assert(strstr(format,"[depth-store] process-start mode %d available %d"));
    va_list ap;va_start(ap,format);assert(va_arg(ap,int)==EXPECTED_MODE);assert(va_arg(ap,int)==expected_available);va_end(ap);logs++;
}
#define XV_LOG startup_log
'''+state+r'''
#include "runtime/xv_benchmark.c"
void xv_logf(const char *format,...) {(void)format;}
void xv_benchmark_optimizations(int enabled) {
    assert(enabled==0 || enabled==1);assert(calls<16);values[calls++]=enabled;
'''+branch+r'''
    assert(!"unexpected comparison");
}
static void startup(void) {
'''+banner+r'''
}
static const float view[6]={1,2,3,0,1,0};
static void trial(unsigned stop) {
#ifdef XV_DEPTH_STORE
    assert(xv_depth_store_enabled()==EXPECTED_MODE && xv_depth_store_available());calls=0;
    xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_DEPTH_STORE));xv_benchmark_remote_poll(1);
    uint64_t now=1;unsigned height=xv_benchmark_step(now,544,1,view);assert(height==544 && calls==1 && !xv_depth_store_enabled());xv_benchmark_applied(now,height);
    for(unsigned i=0;i<1000 && xv_benchmark_active();i++) {
        now+=50000;if(i==210 && stop==1)xv_benchmark_compare_toggle();
        height=xv_benchmark_step(now,544,!(i==210 && stop==2),view);if(height)xv_benchmark_applied(now,height);
    }
    assert(!xv_benchmark_active() && !xv_benchmark_remote_busy() && xv_depth_store_enabled()==EXPECTED_MODE);
    assert(values[0]==0 && values[1]==1 && values[calls-1]==EXPECTED_MODE && calls==(stop?3u:4u));
#endif
}
int main(void) {
    unsetenv("XV_SHADER_OVERRIDE");unsetenv("XV_FS_FORCE");startup();assert(!calls);
#ifdef XV_DEPTH_STORE
    assert(logs==1 && xv_depth_store_enabled()==EXPECTED_MODE);
    for(unsigned stop=0;stop<3;stop++)trial(stop);
    const char *keys[]={"XV_SHADER_OVERRIDE","XV_FS_FORCE"};
    for(unsigned i=0;i<2;i++) {
        calls=0;setenv(keys[i],i?"tex0":"1",1);expected_available=0;startup();
        assert(xv_depth_store_enabled()==EXPECTED_MODE && !xv_depth_store_available());
        xv_benchmark_remote_poll(1);assert(xv_benchmark_remote_request(XV_BENCH_DEPTH_STORE)<0 && !calls);unsetenv(keys[i]);
    }
    assert(logs==3);
#else
    assert(!logs && !xv_depth_store_available && !xv_depth_store_enabled);
    xv_benchmark_remote_poll(1);assert(xv_benchmark_remote_request(XV_BENCH_DEPTH_STORE)<0 && !calls);
#endif
    puts("PASS actual static startup/banner, no startup setter, production controller restore and unavailable/absent admission");
}
'''
(OUT/'startup.c').write_text(fixture)
cc=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-Wno-unused-const-variable','-Wno-address','-I'+str(ROOT)]
for feature in (0,1):
    for initial in (None,0,1):
        exe=OUT/f'startup-{feature}-{initial}'
        flags=[f'-DEXPECTED_MODE={(initial or 0) if feature else 0}',*(['-DXV_DEPTH_STORE'] if feature else []),*([f'-DXV_DEPTH_STORE_DEFAULT={initial}'] if initial is not None else [])]
        run(cc+['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',*flags,OUT/'startup.c','-lm','-o',exe]);run([exe])
for bad in ('2','-1'):
    r=run(cc+['-DXV_DEPTH_STORE','-DXV_DEPTH_STORE_DEFAULT='+bad,'-DEXPECTED_MODE=0','-fsyntax-only',OUT/'startup.c'],expected=1)
    assert 'XV_DEPTH_STORE_DEFAULT must be 0 or 1' in r.stderr

# Entire production Makefile; small real C files stand in for expensive assets.
stage=OUT/'make';stage.mkdir()
def put(name,text='',generated=False):
    f=stage/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text);os.utime(f,(1_000_000_000+generated*10,)*2)
put('Makefile',(ROOT/'Makefile').read_text());put('games/halo_ce_3925/runtime.mk',(ROOT/'games/halo_ce_3925/runtime.mk').read_text())
for name in ('recomp/xv_recomp_protos.h','recomp/xv_x86rt.h','recomp/xv_phase.h','recomp/xv_fn_table.c','recomp/xv_stubs_default.c','recomp/code_000.c',
             'runtime/main.c','runtime/xv_d3d.c','runtime/xv_shader.c','runtime/xv_ui_gxm.c','shaders/halo_shaders.json','recompiler/gen_layouts.py',
             'recompiler/shader_recomp_gen.py','haloce/default.xbe','local/halo_ce_3925/game_manifest.json'):put(name)
put('shaders/xv_layouts.h',generated=True)
for name in ('tools/embed_hud_shaders.py','tools/embed_vertex_shaders.py','tools/embed_ps_shaders.py','tools/test_vertex_varyings.py',
             'shaders/xv_ps_table.h','shaders/ps_A972FE61_1D.frag.gxp','shaders/ps_5D70F0B3_1D.frag.gxp','shaders/ps_EB818129_1D.frag.gxp',
             'shaders/xv_color.frag.gxp','shaders/xv_texmod.frag.gxp','shaders/xv_tex0.frag.gxp','shaders/xv_lm.frag.gxp'):put(name)
for name in ('shaders/xv_hud_gxp.h','shaders/xv_vs_gxp.h','shaders/xv_ps_gxp.h'):put(name,generated=True)
recorder=stage/'record.py';recorder.write_text('''#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
if '-c' in args:
    flags=[v for v in args if v.startswith(('-D','-I')) or v in ('-MMD','-MP')]
    subprocess.run(['cc',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]],check=True)
else:
    assert args[0]=='rcs';subprocess.run(['ar',*args],check=True)
''');recorder.chmod(0o755);shutil.copyfile(recorder,stage/'fake-gcc-ar');(stage/'fake-gcc-ar').chmod(0o755)
base=['make','--no-print-directory','RECOMP=1','VITASDK='+str(stage),'CC='+shlex.join([sys.executable,str(recorder)]),'PREFIX='+str(stage/'fake')]
owners={'runtime/main.c','runtime/xv_d3d.c','runtime/xv_shader.c','runtime/xv_ui_gxm.c'}
targets=['build/'+s[:-2]+'.o' for s in sorted(owners)]+['build/recomp/libxita_guest.a'];previous=None
for feature,initial in [(None,None),(0,0),(1,0),(1,1),(1,1),(1,0),(0,0),(0,1),(1,1),(1,None)]:
    log=stage/'commands.jsonl';log.write_text('')
    settings=([f'XV_DEPTH_STORE={feature}'] if feature is not None else [])+([f'XV_DEPTH_STORE_DEFAULT={initial}'] if initial is not None else [])
    run(base+settings+targets,cwd=stage)
    calls=[json.loads(s) for s in log.read_text().splitlines()];compiles={c[c.index('-c')+1]:c for c in calls if '-c' in c}
    effective=(feature or 0,(initial or 0) if feature else 0)
    if previous is not None:
        expected=set() if previous==effective else {'runtime/xv_d3d.c'} if previous[0]==effective[0] else owners
        assert set(compiles)==expected and len(calls)==len(expected),calls
    for source,flags in compiles.items():
        assert ('-DXV_DEPTH_STORE' in flags)==bool(feature and source in owners)
        assert [f for f in flags if f.startswith('-DXV_DEPTH_STORE_DEFAULT=')]==([f'-DXV_DEPTH_STORE_DEFAULT={effective[1]}'] if feature and source=='runtime/xv_d3d.c' else [])
    assert (stage/'build/depth-store.config').read_text().strip()==str(effective[0])
    assert (stage/'build/depth-store-startup.config').read_text().strip()==str(effective[1])
    rows.append(dict(make_feature=feature,make_default=initial,recipes=calls));previous=effective
for bad in ('','2','-1','0 1'):
    r=run(base+['XV_DEPTH_STORE_DEFAULT='+bad,targets[0]],cwd=stage,expected=1);assert 'XV_DEPTH_STORE_DEFAULT must be 0 or 1' in r.stderr
r=run(base+['RECOMP=0','XV_DEPTH_STORE=1',targets[0]],cwd=stage,expected=1);assert 'XV_DEPTH_STORE requires RECOMP=1' in r.stderr
(OUT/'receipt.json').write_text(json.dumps(dict(result='PASS',checks=rows,sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ('Makefile','runtime/main.c','runtime/xv_depth_store.h','runtime/xv_benchmark.c','tools/test_depth_store_startup.py')}),indent=2)+'\n')
print('PASS 6 ASan/UBSan actual startup/controller builds, unavailable shader modes, absent feature, 10 real Makefile transitions, default target isolation and invalid flags')
