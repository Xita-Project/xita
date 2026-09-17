#!/usr/bin/env python3
"""Production RTT/pump extraction tests for the compile-only scene census.

GPU execution is a deterministic notification mock; no GPU durations are tested.
Emits reproducible sources and a receipt in a required private output directory.
"""
import argparse, ast, hashlib, json, os, pathlib, re, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
SDK=pathlib.Path(os.environ.get('VITASDK',str(pathlib.Path.home()/'vitasdk')))
ap=argparse.ArgumentParser();ap.add_argument('--output-dir',type=pathlib.Path,required=True)
ap.add_argument('--shader-dir',type=pathlib.Path,help='Owned generated shader headers for optional actual ARM translation-unit compile')
args=ap.parse_args()
OUT=args.output_dir.resolve();OUT.mkdir(parents=True,exist_ok=False)
src=(ROOT/'runtime/xv_d3d.c').read_text();main=(ROOT/'runtime/main.c').read_text()
receipt=[]
def run(command,env=None):
    p=subprocess.run([str(v) for v in command],capture_output=True,text=True,env=env)
    receipt.append(dict(command=[str(v) for v in command],returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
    if p.returncode: print(p.stdout,p.stderr);raise AssertionError(command)
    return p.stdout
def compile_host(source,output,flags=(),profile=False):
    run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
        '-Wno-unused-parameter','-Wno-misleading-indentation','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-no-pie','-I'+str(OUT),'-I'+str(ROOT),'-I'+str(ROOT/'runtime'),
        '-idirafter',SDK/'arm-vita-eabi/include',*flags,source,
        *([ROOT/'runtime/xv_render_profile.c'] if profile else []),'-o',output])

# Shared mature replay fixture supplies GXM stubs, not a second replay algorithm.
base=(ROOT/'tools/tests/visibility_placement.c').read_text().split('static void reset(unsigned frame)')[0]
base=base.replace('"../../runtime/','"'+str(ROOT/'runtime')+'/')
base=base.replace('#ifdef XV_QUERY_BOUNDARY\nvoid test_query_notification(const SceGxmNotification *f);\n#endif',
                  'static void test_query_notification(const SceGxmNotification *f);')
base=base.replace('#ifdef XV_QUERY_BOUNDARY\n if(f && ends!=fail_end)test_query_notification(f);\n#else\n assert(!f);\n#endif',
                  'if(f && ends!=fail_end)test_query_notification(f);')
base=base.replace('#include "placement_replay.inc"',r'''
static unsigned sceGxmDepthStencilSurfaceGetForceLoadMode(const SceGxmDepthStencilSurface *d) {return d->load;}
static unsigned sceGxmDepthStencilSurfaceGetForceStoreMode(const SceGxmDepthStencilSurface *d) {return d->store;}
static uint64_t sceKernelGetProcessTimeWide(void) {return clock_us++;}
#define XV_SCENE_CENSUS_IMPLEMENTATION
#include "runtime/xv_scene_census.h"
#include "runtime/xv_scene_census_plan.h"
#include "placement_replay.inc"
''')
(OUT/'scene_replay_fixture.inc').write_text(base)
a=src.index('void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)');b=src.index('/* The clear quad',a)
(OUT/'placement_replay.inc').write_text(src[a:b])
a=main.index('static int xv_gfx_end_scenes(');b=main.index('static int xv_gfx_render_frame(',a)
(OUT/'scene_end.inc').write_text(main[a:b])
traces=[]
for enabled in (0,1):
    exe=OUT/f'replay-{enabled}'
    compile_host(ROOT/'tools/tests/scene_census.c',exe,['-DXV_QUERY_BOUNDARY',*(['-DXV_SCENE_CENSUS'] if enabled else [])],True)
    text=run([exe],{**os.environ,'XV_RT_QUEUE':'1'});print(text,end='')
    traces.append(re.search(r'scene-replay-trace=(\w+)',text)[1])
assert traces[0]==traces[1],traces

# Execute actual production pump/retire code with the existing delayed-GPU
# fixture. Only mock render submission gains diagnostic scene callbacks; the
# actual RTT callbacks and outer final EndScene were exercised above.
tree=ast.parse((ROOT/'tools/test_frame_completion.py').read_text())
parts={n.targets[0].id:ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign)
       and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('prefix','fixture','suffix')}
prefix=parts['prefix'].replace('void *scaled_target;','void *scaled_target; unsigned render_width,render_height;')
prefix=prefix.replace('g_gfx={NULL,1,2,0,NULL}','g_gfx={NULL,1,2,0,NULL,960,544}')
prefix=prefix.replace('notification_words[8]','notification_words[56]')
prefix+='\nstatic int g_net_dialog;\nuint64_t sceKernelGetProcessTimeWide(void);\n#define XV_SCENE_CENSUS_IMPLEMENTATION\n#include "runtime/xv_scene_census.h"\n'
fixture='static SceGxmNotification scene_extra[3];\n'+parts['fixture']
fixture=fixture.replace('void xv_cpu_log_thread(',r'''
void xv_d3d_scene_census_plan(uint32_t mesh,unsigned w,unsigned h,int scaled) {
    xv_sc_shape s={0};s.width=w;s.height=h;xv_sc_plan(&s,UINT32_MAX);
    if(scaled){s.target=9;s.width=960;s.height=544;xv_sc_plan(&s,UINT32_MAX);}
    xv_sc_planned();
}
void xv_cpu_log_thread(''')
fixture=fixture.replace('    if(i+1==failed_frame)return -7;',r'''
    xv_sc_open(0,0,0);
    const SceGxmNotification *sc_f=xv_sc_end(0,UINT32_MAX,UINT32_MAX,g_gfx.scaled_target?world:fence,g_gfx.scaled_target?SC_SCALED:SC_FINAL);
    xv_sc_ended(i+1==failed_frame?-7:0);
    if(sc_f && sc_f!=fence && sc_f!=world)scene_extra[i]=*sc_f;
    if(i+1==failed_frame)return -7;
    if(g_gfx.scaled_target){xv_sc_open(9,0,0);assert(xv_sc_end(9,UINT32_MAX,UINT32_MAX,fence,SC_FINAL)==fence);xv_sc_ended(0);}
''')
fixture=fixture.replace('    for(unsigned i=0;i<rendered;i++) {\n        if(gpu[i].world.address',
    '    for(unsigned i=0;i<rendered;i++) {\n        if(scene_extra[i].address && now>=gpu[i].world_due)*scene_extra[i].address=scene_extra[i].value;\n        if(gpu[i].world.address')
suffix=parts['suffix'].replace('    xv_pump_thread(0,NULL);','    xv_sc_init(notification_words,56);\n    xv_pump_thread(0,NULL);\n    assert(g_sc_totals.retired==3 && !g_sc_totals.orphans && !g_sc_totals.invalid);\n    assert(g_sc_totals.failed==(failed_frame?1u:0u));\n    for(unsigned i=0;i<4;i++)assert(!g_sc_packets[i].live);')
a=main.index('static unsigned g_retired_count');b=main.index('/* ======================================================================================\n *  Mock guest workload',a)
pump=main[a:b]
a=main.index('static struct {',main.index('static volatile int      g_running'));b=main.index('static xv_slot_owner',a)
(OUT/'pump.c').write_text(prefix+main[a:b]+fixture+pump+suffix)
compile_host(OUT/'pump.c',OUT/'pump',['-DXV_SCENE_CENSUS','-DXV_RUN_RECOMP','-DXV_QUERY_BOUNDARY','-DXV_GPU_PACKET_TIMING=0'],True)
for mode in ([],['TEST_NATIVE'],['TEST_EARLY'],['TEST_BOUNDARY','TEST_NATIVE'],['TEST_BOUNDARY','TEST_EARLY'],['TEST_MISSING_WORLD'],['TEST_BOUNDARY','TEST_NO_QUERIES'],['TEST_COMPLETE_DURING_SUBMIT']):
    for extra in ([],['0','wrap'],['0','wrap','error']):
        if 'TEST_COMPLETE_DURING_SUBMIT' in mode and extra and extra[-1]=='error':continue
        run([OUT/'pump',*extra],{**os.environ,**{key:'1' for key in mode}})
print('PASS production pump: 23 delayed/native/scaled/query/missing/error/wrap/submit-time completion cases')

# Actual texture resolver with its existing feedback/cube/fallback/cache cases.
# The added output pointer cannot change a binding, including prior successful
# binds before a later rejection. Extra cases assert resolved masks explicitly.
a=src.index('static int bind_draw_textures(');b=src.index('static int texture_state_override',a)
(OUT/'draw_textures.inc').write_text(src[a:b]);(OUT/'psp2').mkdir();(OUT/'psp2/gxm.h').write_text('/* fixture types */\n')
texture=(ROOT/'tools/tests/draw_textures.c').read_text()
texture=re.sub(r'bind_draw_textures\(([^\n;]*?)\)',lambda m:'bind_draw_textures('+m[1]+',&reads)',texture)
texture=texture.replace('int main(void)\n{','int main(void)\n{\n    uint32_t reads=0;')
texture=texture.replace('    puts("PASS:',r'''
    fs=(xv_fshader_t){{2,-1,-1,-1}};c=(cmd_t){.pass=0,.ntex=1};
    g_rt[1].mem=&cf;c.tex[0].data=&cf;reads=0;
    assert(bind_draw_textures(NULL,NULL,&c,&fs,0,&reads) && reads==2);
    c.previous_frame=1;reads=0;
    assert(bind_draw_textures(NULL,NULL,&c,&fs,0,&reads) && reads==(1u<<9));
    c.pass=1;reads=0;
    assert(bind_draw_textures(NULL,NULL,&c,&fs,0,&reads) && reads==(1u<<8));
    c.previous_frame=0;c.tex[0].data=&source;reads=0;
    assert(bind_draw_textures(NULL,NULL,&c,&fs,0,&reads) && reads==(1u<<10));
    puts("PASS:''')
(OUT/'textures.c').write_text(texture);compile_host(OUT/'textures.c',OUT/'textures',['-DXV_SCENE_CENSUS']);print(run([OUT/'textures']),end='')

# The exact production post-Draw tuple, including requested PS vs the actual
# fragment program and route, must not publish skipped/failed draws.
a=src.index('        if(census_sample && draw_result>=0)');b=src.index('#else',a)
sample_prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {uint32_t *address;uint32_t value;} SceGxmNotification;
#include "runtime/xv_scene_census.h"
#define XV_PS_TABLE_COUNT 2
static const struct {uint32_t ps_key;} xv_ps_table[]={{0x12345678},{0x87654321}};
static xv_sc_sample got;static unsigned calls;
void xv_sc_sample_draw(const xv_sc_sample *s){got=*s;calls++;}
struct desc {uint32_t func_hash;};
struct vertex {struct {struct desc *desc;} vs;};
struct command {int ps_entry;unsigned blend,fs_kind;};
struct fragment {void *id;unsigned alpha_test_mode;};
static void record(unsigned i,int census_sample,int draw_result,struct command *c,struct vertex *v,struct fragment *fs,void *fp,int depth_only,void *linked,unsigned census_reads) {
'''
sample_suffix=r'''}
int main(void) {
    struct desc d={0xdeadbeef};struct vertex v={{&d}};struct command c={1,3,4};struct fragment f={(void*)0xabc,2};
    for(unsigned route=0;route<3;route++) {
        record(7,1,0,&c,&v,&f,(void*)0xdef,route==2,route?(void*)1:NULL,0x120);
        assert(got.valid && got.command==7 && got.ps_key==0x87654321 && got.vs_hash==0xdeadbeef);
        assert(got.program==0xdef && got.patcher==0xabc && got.entry==1 && got.reads==0x120);
        assert(got.route==(route|(2u<<8)|(3u<<16)|(4u<<24)));
    }
    unsigned n=calls;record(8,0,0,&c,&v,&f,NULL,0,NULL,0);record(8,1,-1,&c,&v,&f,NULL,0,NULL,0);assert(calls==n);
    c.ps_entry=-1;v.vs.desc=NULL;record(9,1,0,&c,&v,&f,NULL,0,NULL,0);assert(!got.ps_key && got.entry==UINT32_MAX && !got.vs_hash);
    c.ps_entry=2;record(9,1,0,&c,&v,&f,NULL,0,NULL,0);assert(!got.ps_key && got.entry==2);
    puts("PASS actual shader sample tuple, fallback/depth routes and skipped/failed draw exclusion");
}
'''
(OUT/'sample.c').write_text(sample_prefix+src[a:b]+sample_suffix)
compile_host(OUT/'sample.c',OUT/'sample',['-DXV_SCENE_CENSUS']);print(run([OUT/'sample']),end='')

a=main.index('    SceKernelMemBlockInfo notification_info={0};');b=main.index('\n#endif',a)
(OUT/'metadata.c').write_text(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/sysmem.h>
static volatile unsigned word,*g_notifications=&word;
static int failure;static unsigned queries,inits,logs;
#define XV_SCENE_CENSUS_NOTIFICATION_WORDS 0
int sceKernelGetMemBlockInfoByAddr(void *address,SceKernelMemBlockInfo *info) {
    assert(address==(void*)&word && info->size==sizeof *info);
    assert(!info->mappedBase && !info->mappedSize && !info->type && !info->access);queries++;
    if(failure)return -1;
    info->mappedBase=(void*)&word;info->mappedSize=1024*1024;info->access=3;return 0;
}
static void log_test(const char *format,...) {assert(strstr(format,"capacity not inferred"));logs++;}
#define XV_LOG log_test
static void xv_sc_init(volatile unsigned *address,unsigned capacity){assert(address==&word && capacity==0 && !word);inits++;}
static void startup(void) {
'''+main[a:b]+r'''
}
int main(void){for(failure=0;failure<2;failure++)startup();assert(queries==2 && logs==2 && inits==2);puts("PASS metadata query success/failure, initialized size, no inferred capacity or extra-word access");}
''')
compile_host(OUT/'metadata.c',OUT/'metadata');print(run([OUT/'metadata']),end='')

# Compile actual modified translation units for ARM, feature OFF and ON. This
# checks real SDK types and inline includes; no executable/device is involved.
arm=SDK/'bin/arm-vita-eabi-gcc'
if arm.exists() and args.shader_dir:
    for enabled in (0,1):
        for unit in ('main','xv_d3d'):
            command=[arm,'-std=gnu11','-O2','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-Wall','-Wextra','-Wno-unused-parameter',
                '-DXV_RUN_RECOMP','-DXV_QUERY_BOUNDARY','-I'+str(ROOT),'-I'+str(ROOT/'runtime'),'-I'+str(args.shader_dir.resolve()),'-I'+str(ROOT/'shaders'),
                '-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel'),*(['-DXV_SCENE_CENSUS','-DXV_SCENE_CENSUS_NOTIFICATION_WORDS=56'] if enabled else []),
                '-c',ROOT/f'runtime/{unit}.c','-o',OUT/f'{unit}-{enabled}.o']
            run(command)
    symbols=run([SDK/'bin/arm-vita-eabi-nm','-S',OUT/'main-1.o'])
    sizes={name:int(size,16) for size,name in re.findall(r'^[0-9a-f]+ ([0-9a-f]+) [bB] (g_sc_\w+)$',symbols,re.M)}
    assert sizes['g_sc_packets'] and sizes['g_sc_totals'];receipt.append(dict(arm_static_sizes=sizes))
    off=run([SDK/'bin/arm-vita-eabi-nm',OUT/'main-0.o'])
    assert not re.search(r'\b(?:g_sc_|xv_sc_)',off)
    print('PASS actual ARM main/D3D OFF/ON object compilation; sizeof storage:',sizes)
(OUT/'receipt.json').write_text(json.dumps(dict(result='PASS',commands=receipt,sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ('runtime/main.c','runtime/xv_d3d.c','runtime/xv_scene_census.h','runtime/xv_scene_census_plan.h','tools/test_scene_census.py','tools/tests/scene_census.c')}),indent=2)+'\n')
