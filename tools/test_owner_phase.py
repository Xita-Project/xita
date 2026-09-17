#!/usr/bin/env python3
"""Actual coarse observer boundary/startup tests and real Make graph transitions."""
import argparse,hashlib,json,os,shlex,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);a=p.parse_args()
    out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
    def run(command,*,cwd=None,env=None,ok=True):
        command=list(map(str,command));r=subprocess.run(command,cwd=cwd,env=env,text=True,capture_output=True)
        rows.append(dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
        if (r.returncode==0)!=ok:raise RuntimeError(r.stdout+r.stderr)
        return r
    cc=['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-DXV_OWNER_PHASE','-DXV_EXPERIMENTAL_OBJECT_JOBS','-pthread','-fno-omit-frame-pointer','-no-pie']
    fixture=ROOT/'tools/tests/owner_phase.c';clean={k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    cases=0
    for default in (None,0,1):
        exe=out/f'host-{default}';defs=[] if default is None else [f'-DXV_OWNER_PHASE_DEFAULT={default}']
        run(cc+defs+['-fsanitize=address,undefined',fixture,'-o',exe])
        for setting,want in ((None,default or 0),('0',0),('1',1),('',0),('invalid',0),('-1',1)):
            env=dict(clean)
            if setting is not None:env['XV_OWNER_PHASE']=setting
            run([exe,want],env=env);cases+=1
    for bad in ('2','-1'):
        assert 'XV_OWNER_PHASE_DEFAULT must be 0 or 1' in run(cc+[f'-DXV_OWNER_PHASE_DEFAULT={bad}','-fsyntax-only',fixture],ok=False).stderr
    # Actual concurrency fixture under TSAN. Some Linux ASLR arrangements cannot
    # reserve TSAN shadow space; record that limitation without calling it a pass.
    exe=out/'tsan';run(cc+['-DXV_OWNER_PHASE_DEFAULT=1','-fsanitize=thread',fixture,'-o',exe])
    tsan=subprocess.run([str(exe),'1'],env=clean,text=True,capture_output=True)
    tsan_result=dict(returncode=tsan.returncode,stdout=tsan.stdout,stderr=tsan.stderr)
    if tsan.returncode and 'unexpected memory mapping' not in tsan.stderr:raise RuntimeError(tsan.stderr)
    (out/'tsan.json').write_text(json.dumps(tsan_result,indent=2)+'\n')
    # OFF macro has no argument evaluation, external references, or clock calls.
    off=out/'off.c';off.write_text('#include "recomp/kernel/xk_owner_phase.h"\nint main(void){int n=0;XV_OWNER_PHASE_SCOPE((void *)(unsigned long)++n,++n);return n;}\n')
    exe=out/'off';run(['cc','-std=c11','-O2','-Werror','-I'+str(ROOT),off,'-o',exe]);run([exe])
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
    # Keep the real native adapter membership rule; use tiny stand-ins for its
    # unrelated members and prerequisites. Only xd3d is needed in the system
    # archive for this bounded graph fixture.
    for name in ('xk_quality','xk_math','xk_clip','xk_bounds','xk_flare','xk_geometry','xk_owner_phase','xd3d'):
        put('recomp/kernel/'+name+'.c',generated=True)
    for name in ('recomp/kernel/xk_owner_phase.h','tools/gen_native_clip.py','tools/gen_native_bounds.py',
                 'games/halo_ce_3925/hooks.py','games/halo_ce_3925/clip_region.py','recompiler/xita_recomp.py'):
        put(name)
    for number in (17,22):put(f'recomp/code_{number:03d}.c','/* XV_OWNER_PHASE_SCOPE: primary fixture */\n',generated=True)
    base+=['XITA_SYS_SRCS=recomp/kernel/xd3d.c']
    targets+=['build/recomp/libxita_game.a','build/recomp/libxita_sys.a']
    previous=None;builds=[]
    for feature,default in ((0,0),(0,1),(1,0),(1,1),(1,1),(1,0),(0,0),(0,0)):
        log=stage/'commands.jsonl';log.write_text('')
        run(base+[f'XV_OWNER_PHASE={feature}',f'XV_OWNER_PHASE_DEFAULT={default}',*targets],cwd=stage)
        commands=[json.loads(x) for x in log.read_text().splitlines()];compiles={c[c.index('-c')+1]:c for c in commands if '-c'in c}
        arcs=[c[1] for c in commands if c[0]=='rcs']
        effective=default if feature else 0
        if previous is not None:
            if previous[0]!=feature:
                want={'runtime/main.c','runtime/xv_ui_gxm.c','recomp/kernel/xd3d.c','recomp/code_017.c','recomp/code_022.c'}|({'recomp/kernel/xk_owner_phase.c'} if feature else set())
                assert set(compiles)==want,(feature,default,commands)
                assert set(arcs)=={'build/recomp/libxita_game.a','build/recomp/libxita_sys.a','build/recomp/libxita_guest.a'}
            elif previous[1]!=effective:
                assert set(compiles)=={'recomp/kernel/xk_owner_phase.c'} and arcs==['build/recomp/libxita_game.a'],commands
            else:assert not commands,commands
        for name,flags in compiles.items():
            assert [v for v in flags if v.startswith('-DXV_OWNER_PHASE_DEFAULT=')]==([f'-DXV_OWNER_PHASE_DEFAULT={effective}'] if name=='recomp/kernel/xk_owner_phase.c' and feature else []),(name,flags)
            owner=name in {'runtime/main.c','runtime/xv_ui_gxm.c','recomp/kernel/xd3d.c','recomp/kernel/xk_owner_phase.c','recomp/code_017.c','recomp/code_022.c'}
            assert ('-DXV_OWNER_PHASE'in flags)==bool(feature and owner),(name,flags)
        members=run(['ar','t','build/recomp/libxita_game.a'],cwd=stage).stdout.splitlines()
        assert ('xk_owner_phase.o'in members)==bool(feature),members
        assert (stage/'build/owner-phase.config').read_text()==str(feature)+'\n'
        # The startup stamp is owned only by the optional observer object. An
        # OFF build neither creates nor refreshes it; re-enabling evaluates it.
        if feature:assert (stage/'build/owner-phase-startup.config').read_text()==str(effective)+'\n'
        builds.append(dict(feature=feature,default=default,recipes=commands,members=members));previous=(feature,effective)
    for flag in ('XV_OWNER_PHASE','XV_OWNER_PHASE_DEFAULT'):
        for bad in ('','2','-1','0 1','invalid'):
            assert flag+' must be 0 or 1'in run(base+[flag+'='+bad,targets[0]],cwd=stage,ok=False).stderr
    (stage/'recomp/code_022.c').write_text('/* missing hook */\n')
    assert 'requires the two regenerated'in run(base+['XV_OWNER_PHASE=1',targets[0]],cwd=stage,ok=False).stderr
    sources=['Makefile','games/halo_ce_3925/hooks.py','games/halo_ce_3925/runtime.mk','recomp/kernel/xk_owner_phase.c','recomp/kernel/xk_owner_phase.h','recomp/kernel/xd3d.c','runtime/main.c','runtime/xv_ui_gxm.c','tools/tests/owner_phase.c','tools/test_owner_phase.py']
    receipt=dict(result='PASS',fresh_processes=cases,tsan='PASS' if not tsan.returncode else 'UNAVAILABLE: shadow mapping',builds=builds,sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources},scope='Actual observer, mocked owner/clock/log, concurrent native foreign/worker calls; real Make cc/ar transitions. No hardware, benchmark or FPS result.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(f'PASS {cases} fresh-process ASan/UBSan boundary cases; compile-OFF; eight actual Make transitions; TSAN '+receipt['tsan'])

if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without Python -O')
    main()
