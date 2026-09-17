#!/usr/bin/env python3
"""Fresh-process production selection/replay and real incremental Make checks."""
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
FLAGS = ('XV_TEXTURE_STATE_CACHE_DEFAULT', 'XV_DEPTH_PREPARE_DEFAULT')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rows = []

    def run(command, *, cwd=None, env=None, ok=True):
        command = [str(v) for v in command]
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
        rows.append(dict(command=command, returncode=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        (out/'commands.json').write_text(json.dumps(rows, indent=2)+'\n')
        if (result.returncode == 0) != ok:
            raise RuntimeError(result.stdout + result.stderr)
        return result

    source = (ROOT/'runtime/xv_d3d.c').read_text()
    def section(start, end):
        a = source.index(start)
        return source[a:source.index(end, a)]
    code = section('#ifndef XV_DEPTH_PREPARE_DEFAULT', 'static int opaque_material_override')
    a = source.rindex('typedef struct {', 0, source.index('    uint8_t   kind;'))
    code += source[a:source.index('/* Commands retain', a)]
    code += section('static unsigned record_material(', 'static int vertex_reference_layout(')
    code += section('static int material_alpha_mode(', 'static SceGxmTexture g_previous_frame_texture')
    (out/'depth_prepare.inc').write_text(code)
    (out/'depth_replay.inc').write_text(section('        vs_slot_t *v = &g_vs[c->vs];', '        if (!bind_draw_textures('))
    (out/'texture_config.inc').write_text(section('#ifndef XV_TEXTURE_STATE_CACHE_DEFAULT', '/* Dashboard/environment handoff'))
    (out/'render_startup.inc').write_text(section('void xv_d3d_configure_render_preparation(', '/* A failed reservation'))
    (out/'draw_textures.inc').write_text(section('static int bind_draw_textures(', '#ifndef XV_TEXTURE_STATE_CACHE_DEFAULT'))
    (out/'psp2').mkdir()
    (out/'psp2/gxm.h').write_text('/* GXM fixture supplies opaque descriptors/functions. */\n')
    fixture = ROOT/'tools/tests/render_preparation_startup.c'
    cc = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
          '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
          '-fno-omit-frame-pointer', '-no-pie', '-pthread', '-DXV_PACKED_VERTEX_LAYOUT=1',
          '-I'+str(out), '-I'+str(ROOT), '-I'+str(ROOT/'runtime')]
    clean = {k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    values = [(None,None), ('0',0), ('1',1), ('',0), ('invalid',0), ('-1',1), ('2',1), ('1junk',1)]
    processes = 0
    for defaults in (None, (0,0), (0,1), (1,0), (1,1)):
        effective = defaults or (0,0)
        defines = [] if defaults is None else ['-D'+f+'='+str(v) for f,v in zip(FLAGS, defaults)]
        binary = out/('startup-'+str(defaults))
        run(cc+defines+[fixture, '-o', binary])
        cases = [(value, parsed, value, parsed) for value,parsed in values]
        cases += [('0',0,None,None), (None,None,'0',0), ('1',1,'0',0), ('0',0,'1',1)]
        for tv,tp,dv,dp in cases:
            env = dict(clean)
            for name,value in [('XV_TEXTURE_STATE_CACHE',tv),('XV_DEPTH_PREPARE',dv)]:
                if value is not None: env[name]=value
            want_t = effective[0] if tv is None else tp
            want_d = effective[1] if dv is None else dp
            r = run([binary, want_t, want_d, 1], env=env)
            assert f'process-start texture-cache {want_t} depth-prepare {want_d} available 1' in r.stdout
            processes += 1
        for name,value in [('XV_SHADER_OVERRIDE','1'),('XV_SHADER_OVERRIDE','-1'),
                           ('XV_DEPTH_ONLY_SHADER','0'),('XV_DEPTH_ONLY_SHADER',''),
                           ('XV_ALPHA_SPECIALIZE','0'),('XV_ALPHA_SPECIALIZE','invalid')]:
            env = dict(clean, XV_TEXTURE_STATE_CACHE='1', XV_DEPTH_PREPARE='1')
            env[name] = value
            r = run([binary, 1, 0, 0], env=env)
            assert 'process-start texture-cache 1 depth-prepare 0 available 0' in r.stdout
            processes += 1
    for flag in FLAGS:
        for bad in ('2','-1'):
            r = run(cc+['-D'+flag+'='+bad,'-fsyntax-only',fixture],ok=False)
            assert flag+' must be 0 or 1' in r.stderr

    # Full current Makefile and real cc/ar with small C inputs instead of assets.
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
    for defaults in (None, (0,0), (1,0), (1,1), (1,1), (0,1), (0,0), (0,0)):
        log = stage/'commands.jsonl'
        log.write_text('')
        effective = defaults or (0,0)
        settings = [] if defaults is None else [f+'='+str(v) for f,v in zip(FLAGS,defaults)]
        run(base+settings+targets,cwd=stage)
        commands = [json.loads(line) for line in log.read_text().splitlines()]
        compiles = {c[c.index('-c')+1]:c for c in commands if '-c' in c}
        expected = (owners | {'recomp/code_000.c','recomp/xv_fn_table.c','recomp/xv_stubs_default.c'}) if previous is None else ({'runtime/xv_d3d.c'} if previous != effective else set())
        assert set(compiles)==expected,(defaults,commands,expected)
        if previous is not None: assert len(commands)==len(expected),commands
        for name,flags in compiles.items():
            for flag,value in zip(FLAGS,effective):
                assert [v for v in flags if v.startswith('-D'+flag+'=')] == (['-D'+flag+'='+str(value)] if name=='runtime/xv_d3d.c' else [])
        for name,value in zip(('texture-state','depth-prepare'),effective):
            assert (stage/('build/'+name+'-startup.config')).read_text()==str(value)+'\n'
        builds.append(dict(defaults=defaults,recipes=commands))
        previous=effective
    for flag in FLAGS:
        for bad in ('','2','-1','0 1','false'):
            r=run(base+[flag+'='+bad,targets[0]],cwd=stage,ok=False)
            assert flag+' must be 0 or 1' in r.stderr
    # Startup call is after dashboard configuration and before either worker.
    main_source=(ROOT/'runtime/main.c').read_text()
    begin=main_source.index('    xv_pipeline_configure(); /* Dashboard edits loaded;')
    startup=main_source.index('    xv_d3d_configure_render_preparation();',begin)
    pump=main_source.index('sceKernelCreateThread("xv_pump"',begin)
    assert begin<startup<pump
    sources=('Makefile','runtime/main.c','runtime/xv_d3d.c','runtime/xv_d3d.h',
             'runtime/xv_depth_prepare.h','runtime/xv_texture_state.h',
             'tools/tests/render_preparation_startup.c','tools/tests/depth_prepare.c',
             'tools/test_render_preparation_startup.py')
    (out/'receipt.json').write_text(json.dumps(dict(result='PASS',fresh_processes=processes,
        builds=builds,sources={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},
        scope='Production selection/record/replay/resolver/cache with host GXM stubs; actual Make graph, real cc/ar. No GPU execution, hardware timing or gameplay claim.'),indent=2)+'\n')
    print(f'PASS {processes} ASan/UBSan fresh processes; packed/query/depth command retention; explicit environment and incompatibility/override restoration; eight real Make builds; D3D-only default invalidation')


if __name__=='__main__':
    if not __debug__: raise SystemExit('Run without Python -O: verification requires assertions')
    main()
