#!/usr/bin/env python3
"""Fresh-process startup selection and retained ARM Make ownership qualification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
FEATURE='XV_MODEL_BATCHES_TRIAL'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if not __debug__:raise SystemExit('Run without Python -O')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('output-dir','retained-build','build-command'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    commands=[]
    def run(command,*,cwd=None,env=None,ok=True):
        p=subprocess.run(list(map(str,command)),cwd=cwd,env=env,capture_output=True,text=True)
        commands.append(dict(command=list(map(str,command)),cwd=str(cwd) if cwd else None,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if (p.returncode==0)!=ok:raise RuntimeError(p.stdout+p.stderr)
        return p
    source=(ROOT/'runtime/main.c').read_text()
    begin=source.index('static void xv_load_settings(void)')
    end=source.index('\n#ifdef XV_RUN_RECOMP\ntypedef struct {',begin)
    (out/'model_batches_startup.inc').write_text(source[begin:end])
    dashboard=source.index('    int dashboard_result=xv_dashboard_start();')
    config=source.index('    xv_pipeline_configure();',dashboard)
    selection=source.index('    xv_model_batches_trial_startup();',config)
    thread=source.index('sceKernelCreateThread("xv_pump"',config)
    assert dashboard<config<selection<thread
    assert source.count('    xv_model_batches_trial_startup();')==1
    dash_begin=source.index('static int xv_dashboard_start(void)')
    assert source.index('    xv_load_settings();',dash_begin)<source.index('    return 0;',dash_begin)
    clean={k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    cc=['cc','-std=gnu11','-O1','-g','-fno-strict-aliasing','-ffp-contract=off','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-no-pie','-DXV_RUN_RECOMP','-DXV_NATIVE_MODEL_PALETTE','-DXV_NATIVE_MODEL_HIERARCHY',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-I'+str(ROOT/'recomp'),'-I'+str(out)]
    sources=[ROOT/'tools/tests/model_batches_startup.c',ROOT/'recomp/kernel/xk_palette.c',ROOT/'recomp/kernel/xk_hierarchy.c']
    options=[(None,None,None,'0'),('0','0',None,'0'),('1','0',None,'0'),('0','1',None,'1'),
             ('1','1',None,'0'),('invalid','',None,'0'),('-1','2',None,'0'),('0','0','0','0'),
             ('1','1','invalid','0'),('0','0','-1','0')]
    fresh=[]
    def enabled(v,default=False):
        if v is None:return default
        try:return int(v)!=0
        except ValueError:return False
    for selector in (None,0,1):
        binary=out/('startup-'+str(selector))
        defs=[] if selector is None else ['-D'+FEATURE+'='+str(selector)]
        run(cc+defs+sources+['-Wl,--wrap=xv_model_palette_override,--wrap=xv_model_hierarchy_override','-lm','-o',binary])
        for index,(palette,hierarchy,math,neon) in enumerate(options):
            case=out/f'case-{selector}-{index}';(case/'ux0:data/xita').mkdir(parents=True)
            settings={'XV_RENDER_HEIGHT':'544','XV_TEX_QUALITY':'0','XV_TRIPLE_BUFFER':'1',
                      'XV_NATIVE_MATRIX_NEON':neon,'XV_QUERY_PREFIX_PUBLISH':'1','XV_TEXTURE_STATE_CACHE':'1',
                      'XV_DEPTH_PREPARE':'1'}
            for key,val in [('XV_NATIVE_MODEL_PALETTE',palette),('XV_NATIVE_MODEL_HIERARCHY',hierarchy),('XV_NATIVE_MATH',math)]:
                if val is not None:settings[key]=val
            final='# dashboard saved values; preserve this file\n'+''.join(k+'='+v+'\n' for k,v in settings.items())
            # The boot config intentionally differs. Dashboard reload must win.
            initial='XV_NATIVE_MODEL_PALETTE=0\nXV_NATIVE_MODEL_HIERARCHY=0\n'
            if palette is None:initial=''
            (case/'ux0:data/xita/env.txt').write_text('XV_RENDER_HEIGHT=360\n')
            (case/'ux0:data/xita/xita.cfg').write_text(initial)
            (case/'dashboard.cfg').write_text(final)
            gate=enabled(math,True);p=gate and enabled(palette);h=gate and enabled(hierarchy)
            wanted=(gate,gate) if selector else (p,h)
            result=run([binary,*map(int,wanted),int(p),int(h)],cwd=case,env=clean)
            assert (case/'ux0:data/xita/xita.cfg').read_text()==final
            assert (case/'ux0:data/xita/env.txt').read_text()=='XV_RENDER_HEIGHT=360\n'
            if selector:
                assert 'configured palette='+(''+palette if palette is not None else '<unset:off>') in result.stdout
                assert 'effective palette='+str(int(gate))+' hierarchy='+str(int(gate)) in result.stdout
            else:assert '[model-batches-trial]' not in result.stdout
            fresh.append(dict(selector=selector,palette=palette,hierarchy=hierarchy,math=math,neon=neon,effective=list(map(int,wanted))))
    for invalid in ('2','-1'):
        p=run(cc+['-D'+FEATURE+'='+invalid,'-fsyntax-only',sources[0]],ok=False)
        assert FEATURE+' must be 0 or 1' in p.stderr
    for missing in ('XV_RUN_RECOMP','XV_NATIVE_MODEL_PALETTE','XV_NATIVE_MODEL_HIERARCHY'):
        p=run([v for v in cc if v!='-D'+missing]+['-D'+FEATURE+'=1','-fsyntax-only',sources[0]],ok=False)
        assert FEATURE+' requires' in p.stderr

    # The real retained build and real ARM compiler. No guest regeneration or
    # broad semantic suites: unchanged arithmetic objects reuse prior review.
    stage=out/'build';run(['cp','-a','--reflink=auto',a.retained_build,stage])
    baseline=sha(stage/'build/runtime/main.o')
    for n in ('Makefile','runtime/main.c'):shutil.copy2(ROOT/n,stage/n)
    kept={str(p.relative_to(stage)):sha(p) for p in (stage/'build').rglob('*.o') if p!=stage/'build/runtime/main.o'}
    base=json.loads(a.build_command.read_text())['command'][:-1]
    base=[v for v in base if not v.startswith(FEATURE+'=')]
    if 'XV_QUERY_ANCESTOR_SCALAR=1' not in base:base+=['XV_QUERY_ANCESTOR_SCALAR=1']
    targets=['build/runtime/main.o','build/runtime/xv_d3d.o','build/runtime/xv_vertex_upload.o',
             'build/recomp/kernel/xk_palette.o','build/recomp/kernel/xk_hierarchy.o']
    builds=[];previous=None;off=None
    for label,selector in [('default',None),('off-noop',0),('on',1),('on-noop',1),('off',0),('off-repeat',0)]:
        mode=[] if selector is None else [FEATURE+'='+str(selector)]
        result=run(base+mode+targets,cwd=stage)
        compiles=[s for s in result.stdout.splitlines() if ' -c ' in s]
        if label!='default':assert len(compiles)==int(bool(selector)!=bool(previous)),(label,compiles)
        assert all(' -c runtime/main.c ' in s for s in compiles),(label,compiles)
        for name,value in kept.items():assert sha(stage/name)==value,name
        assert (stage/'build/model-batches-trial.config').read_text()==str(int(bool(selector)))+'\n'
        value=sha(stage/'build/runtime/main.o')
        if not selector:
            if off is None:off=value
            assert value==off==baseline
        else:assert value!=off
        shutil.copy2(stage/'build/runtime/main.o',out/(label+'-main.o'))
        builds.append(dict(label=label,selector=selector,compiles=compiles,main_sha256=value));previous=selector
    rejected=[]
    for invalid in ('','2','-1','0 1','false'):
        p=run(base+[FEATURE+'='+invalid,targets[0]],cwd=stage,ok=False)
        assert FEATURE+' must be 0 or 1' in p.stderr;rejected.append(invalid)
    # Isolate these guards from unrelated retained options that also reject
    # RECOMP/profile changes earlier in Makefile parsing.
    selection_base=['make','VITASDK='+os.environ['VITASDK'],'RECOMP=1',
                    'XV_NATIVE_MODEL_PALETTE=1','XV_NATIVE_MODEL_HIERARCHY=1']
    for setting in ('RECOMP=0','GAME_PROFILE=halo2_11081','XV_NATIVE_MODEL_PALETTE=0','XV_NATIVE_MODEL_HIERARCHY=0'):
        p=run(selection_base+[FEATURE+'=1',setting,targets[0]],cwd=stage,ok=False)
        assert FEATURE+' requires' in p.stderr;rejected.append(setting)
    receipt=dict(result='PASS',fresh_processes=len(fresh),fresh=fresh,builds=builds,rejections=rejected,
        only_main_object_changed=True,off_full_object_identical_to_retained=off==baseline,
        kept_objects_sha256=kept,source_sha256={n:sha(ROOT/n) for n in ('Makefile','runtime/main.c',
        'tools/test_model_batches_startup.py','tools/tests/model_batches_startup.c')},
        arithmetic_fixture_reused='f583a0fd480fed7ff8b9ab5f2417b3f7abcbffc4',no_device_access=True)
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS',len(fresh),'fresh processes; actual ARM OFF/ON/OFF/no-op; main-only owner; config unchanged')

if __name__=='__main__':main()
