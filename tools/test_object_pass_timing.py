#!/usr/bin/env python3
"""Focused actual object-pool observer tests; real incremental Make and Vita OFF identity."""
import argparse,hashlib,json,os,re,resource,shlex,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-dir',type=Path,required=True)
    p.add_argument('--sdk',type=Path,default=Path('/home/birchwoodgod/vitasdk/bin'))
    p.add_argument('--owner-source',type=Path,help='Optional final observer implementation with the three-argument ancestry API')
    p.add_argument('--base',default='3bb0654628f308dc5c07fc4100c360151b3c8f3d')
    a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
    def run(command,*,cwd=None,env=None,ok=True):
        command=list(map(str,command));r=subprocess.run(command,cwd=cwd,env=env,text=True,capture_output=True,timeout=60)
        rows.append(dict(command=command,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        (out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
        if (r.returncode==0)!=ok:raise RuntimeError(r.stdout+r.stderr)
        return r
    clean={k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    cc=['cc','-std=gnu11','-O1','-g','-fno-strict-aliasing','-fno-omit-frame-pointer','-no-pie',
        '-fsanitize=address,undefined','-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(ROOT/'recomp'),
        '-ffunction-sections','-fdata-sections']
    fixture=ROOT/'tools/tests/object_pass_timing.c';cases=0
    for mode in ('off','on','missing'):
        exe=out/mode;defs=[] if mode=='off' else ['-DXV_OBJECT_PASS_TIMING=1']
        if mode=='missing':defs+=['-DOBJECT_PASS_MISSING_API']
        run(cc+defs+[fixture,ROOT/'recomp/xv_x86rt.c','-pthread','-Wl,--gc-sections','-lm','-o',exe])
        for workers in (0,1,2):
            env=dict(clean,XV_OBJECT_JOB_WORKERS=str(workers),XV_OBJECT_LOCK_PROFILE='0')
            result=run([exe],env=env);cases+=1
            (out/f'{mode}-{workers}.log').write_text(result.stderr+result.stdout)
            if mode=='off':assert '[object-pass]' not in result.stderr and '[object-pass-stop]' not in result.stderr
            elif mode=='missing':
                assert '[object-pass-stop] completed 0 interrupted 0 unknown 1 open 0' in result.stderr
                assert 'unknown 1 invalid 0 clocks 0 owner-valid 0;' in result.stderr
            else:
                assert '[object-pass] 60 frames begun 8 completed 3 interrupted 5 open 0 unknown 1 invalid 5 clocks 13 owner-valid 1;' in result.stderr
                assert '[object-pass] 60 frames begun 0 completed 0 interrupted 0 open 0 unknown 0 invalid 0 clocks 0 owner-valid 1;' in result.stderr
                reports=re.findall(r'\[object-worker-clock\] 60 frames lane (\d).*? delta (\d+) wall-us (\d+) valid (\d+) reason (\d+) errors (\d+);',result.stderr)
                assert [tuple(map(int,r[1:])) for r in reports]==[(0,0,0,2,0),(7,30,1,0,0),(0,0,0,1,1),(0,0,0,2,1),(0,0,0,3,1),(0,0,0,4,1),(0,0,0,4,1),(0,0,0,1,2),(0,0,0,2,0),(0,0,0,4,0)],reports
        run([exe,'init-fail'],env=clean);cases+=1
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        fail=run([exe,'budget'],env=dict(clean,XV_OBJECT_JOB_WORKERS='2'),ok=False)
        assert fail.returncode<0 and 'STOP job instruction budget exceeded' in fail.stderr
        assert '[object-pass-stop]' not in fail.stderr # STOP still aborts; no forged cleanup
    for bad in ('-1','2'):
        badcc=[v for v in cc if v!='-fsanitize=address,undefined']
        assert 'XV_OBJECT_PASS_TIMING must be 0 or 1' in run(badcc+[f'-DXV_OBJECT_PASS_TIMING={bad}','-fsyntax-only',fixture],ok=False).stderr
    linked_owner=None
    if a.owner_source:
        owner_source=a.owner_source.resolve();exe=out/'linked-owner'
        run(cc+['-DXV_OBJECT_PASS_TIMING=1','-DXV_OWNER_PHASE','-DXV_OWNER_PHASE_DEFAULT=1',
                '-DOBJECT_PASS_LINKED_OWNER',fixture,ROOT/'recomp/xv_x86rt.c',owner_source,
                '-pthread','-Wl,--gc-sections','-lm','-o',exe])
        result=run([exe],env=dict(clean,XV_OBJECT_JOB_WORKERS='2'))
        (out/'linked-owner.log').write_text(result.stderr+result.stdout)
        linked_owner=dict(source=str(owner_source),sha256=hashlib.sha256(owner_source.read_bytes()).hexdigest(),result='PASS')
    # Exercise the exact Vita API-read loop without an SDK kernel: the real
    # thread struct is zeroed and sized, IDs are current worker slots, reads are
    # sequential, failures/empty records cannot supply a usable delta.
    source=(ROOT/'recomp/kernel/xk_object_jobs.c').read_text()
    record=re.search(r'^static void pass_clock_record\(.*?^}',source,re.M|re.S).group()
    record_type=re.search(r'^static struct pass_clock_sample \{.*?^} pass_clocks\[WORKERS\];',source,re.M|re.S).group()
    start=source.index('    if(admitted)for(unsigned lane=0;lane<WORKERS;lane++)')
    stop=source.index('\n#else',start)
    adapter=out/'vita-read.c'
    adapter.write_text('#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
        '#define WORKERS 2\n#define XK_LOG printf\n'
        'typedef struct {unsigned size; char name[32];uint64_t runClocks;} SceKernelThreadInfo;\n'
        'static int threads[2]={11,22};static unsigned active_workers=1,round_,calls;\n'
        'static uint64_t xk_os_monotonic_us(void){return 1000+round_*100+calls;}\n'
        'static int sceKernelGetThreadInfo(int id,SceKernelThreadInfo *p){\n'
        ' assert(p->size==sizeof *p&&!p->name[0]&&!p->runClocks);\n'
        ' assert(id==threads[calls%2]);calls++;p->runClocks=round_*10+id;\n'
        ' if(round_==1&&id==22)return -9;\n'
        ' if(round_!=2||id!=11)p->name[0]=\'x\';return 0;}\n'+record_type+'\n'+record+
        '\nstatic void sample(unsigned admitted,unsigned frames){\n'+source[start:stop]+'\n}\n'
        'int main(void){sample(0,60);assert(!calls);for(round_=0;round_<5;round_++)sample(1,60);assert(calls==10);return 0;}\n')
    exe=out/'vita-read';run(['cc','-std=gnu11','-O1','-g','-no-pie','-fsanitize=address,undefined',adapter,'-o',exe])
    adapter_result=run([exe]);(out/'vita-read.log').write_text(adapter_result.stdout)
    observed=re.findall(r'lane (\d).*?valid (\d+) reason (\d+) errors (\d+);',adapter_result.stdout)
    assert observed==[('0','0','2','0'),('1','0','2','0'),('0','1','0','0'),('1','0','1','1'),('0','0','1','1'),('1','0','2','1'),('0','0','2','1'),('1','1','0','1'),('0','1','0','1'),('1','1','0','1')],observed
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
            'XV_VERTEX_BLOCK_LOADS_DEFAULT=1', 'XV_RGBA_SWIZZLED_DEFAULT=1',
            'XV_OWNER_PHASE=1', 'XV_EXPERIMENTAL_OBJECT_JOBS=1']
    owners = {'runtime/' + s + '.c' for s in ('main', 'xv_d3d', 'xv_shader', 'xv_ui_gxm', 'xv_vertex_upload')}
    targets = ['build/' + s[:-2] + '.o' for s in sorted(owners)] + ['build/recomp/libxita_guest.a']
    # Keep the real native adapter membership rule; use tiny stand-ins for its
    # unrelated members and prerequisites. Only xd3d is needed in the system
    # archive for this bounded graph fixture.
    for name in ('xk_quality','xk_math','xk_clip','xk_bounds','xk_flare','xk_geometry','xk_owner_phase','xk_object_jobs','xd3d'):
        put('recomp/kernel/'+name+'.c',generated=True)
    for name in ('recomp/kernel/xk_owner_phase.h','recomp/kernel/xk_object_solver.h','tools/gen_native_clip.py','tools/gen_native_bounds.py',
                 'games/halo_ce_3925/hooks.py','games/halo_ce_3925/clip_region.py','recompiler/xita_recomp.py'):
        put(name)
    for number in (17,22):put(f'recomp/code_{number:03d}.c','/* XV_OWNER_PHASE_SCOPE: primary fixture */\n',generated=True)
    base+=['XITA_SYS_SRCS=recomp/kernel/xd3d.c']
    targets+=['build/recomp/libxita_game.a','build/recomp/libxita_sys.a']
    previous=None;builds=[]
    for feature in (0,1,1,0,0):
        log=stage/'commands.jsonl';log.write_text('')
        run(base+[f'XV_OBJECT_PASS_TIMING={feature}',*targets],cwd=stage)
        commands=[json.loads(x) for x in log.read_text().splitlines()]
        compiles={c[c.index('-c')+1]:c for c in commands if '-c' in c}
        arcs=[c[1] for c in commands if c[0]=='rcs']
        if previous is not None:
            if previous!=feature:
                assert set(compiles)=={'recomp/kernel/xk_object_jobs.c'},commands
                assert arcs==['build/recomp/libxita_game.a'],commands
            else:assert not commands,commands
        for name,flags in compiles.items():
            assert ('-DXV_OBJECT_PASS_TIMING=1' in flags)==bool(feature and name=='recomp/kernel/xk_object_jobs.c'),(name,flags)
        assert (stage/'build/recomp/object-pass-timing.config').read_text()==str(feature)+'\n'
        builds.append(dict(feature=feature,recipes=commands));previous=feature
    for bad in ('','2','-1','0 1','invalid'):
        assert 'XV_OBJECT_PASS_TIMING must be 0 or 1' in run(base+['XV_OBJECT_PASS_TIMING='+bad,targets[0]],cwd=stage,ok=False).stderr
    for flag in ('RECOMP=0','XV_OWNER_PHASE=0','XV_EXPERIMENTAL_OBJECT_JOBS=0'):
        assert 'requires' in run(base+['XV_OBJECT_PASS_TIMING=1',flag,targets[0]],cwd=stage,ok=False).stderr
    # Actual Vita compiler and previous production TU, same flags. OFF must have
    # identical allocated code/data plus relocations, not merely equal source.
    arm=out/'arm';arm.mkdir();src=ROOT/'recomp/kernel/xk_object_jobs.c'
    original=run(['git','show',a.base+':recomp/kernel/xk_object_jobs.c'],cwd=ROOT).stdout
    old=arm/'original.c';old.write_text(original)
    flags=['-O2','-fno-strict-aliasing','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-w','-std=gnu11','-fstack-usage',
        '-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel'),'-DXV_EXPERIMENTAL_OBJECT_JOBS',
        '-DXV_LIGHT_QUERY_CENSUS','-DXV_OBJECT_HOLD_PROFILE','-DXV_OBJECT_POSE_EXPERIMENT']
    # Relative ../xv_phase.h in the private copy still resolves via kernel include.
    objects={};dumps={};sizes={};stacks={}
    for mode,path,defs in (('base',old,[]),('off',src,[]),('zero',src,['-DXV_OBJECT_PASS_TIMING=0']),('on',src,['-DXV_OBJECT_PASS_TIMING=1'])):
        obj=arm/(mode+'.o');run([a.sdk/'arm-vita-eabi-gcc',*flags,*defs,'-c',path,'-o',obj]);objects[mode]=hashlib.sha256(obj.read_bytes()).hexdigest()
        dumps[mode]=run([a.sdk/'arm-vita-eabi-objdump','-dr',obj]).stdout.split('\n',2)[2]
        sizes[mode]=run([a.sdk/'arm-vita-eabi-size','-A',obj]).stdout
        stacks[mode]=obj.with_suffix('.su').read_text()
        (arm/(mode+'.dis')).write_text(dumps[mode])
    assert dumps['base']==dumps['off']==dumps['zero'],'OFF code or relocations changed'
    def allocated_size(text):return '\n'.join(text.splitlines()[1:])
    assert allocated_size(sizes['base'])==allocated_size(sizes['off'])==allocated_size(sizes['zero'])
    sources=['Makefile','recomp/kernel/xk_object_jobs.c','tools/tests/object_pass_timing.c','tools/test_object_pass_timing.py']
    receipt=dict(result='PASS',asan_ubsan_processes=cases,budget_stop_negative_controls=3,builds=builds,
        linked_owner=linked_owner,arm=dict(objects=objects,section_sizes=sizes,stack=stacks,off_instructions_relocations_sizes_identical=True),
        sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources},
        limits='Mock owner-scope API; production API independently qualified by scene-partition observer. Real pthread workers and runtime budget STOP. No Vita hardware run-clock units or FPS claim.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(f'PASS {cases} actual-pool ASan/UBSan processes; three budget STOPs; five real Make transitions; Vita OFF identity/ON compilation')

if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without Python -O')
    main()
