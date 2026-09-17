#!/usr/bin/env python3
"""Bounded observer accounting, actual emitted CFG and retained ARM build checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from games.halo_ce_3925.scene_partition import hook, strip

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    if not __debug__:raise SystemExit('Run without Python -O')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('output-dir','retained-build','build-command','generated-body'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False);commands=[]
    def run(args,*,cwd=None,env=None,ok=True):
        args=list(map(str,args));p=subprocess.run(args,cwd=cwd,env=env,text=True,capture_output=True)
        commands.append(dict(command=args,cwd=str(cwd) if cwd else None,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if (p.returncode==0)!=ok:raise RuntimeError(p.stdout+p.stderr)
        return p
    cc=['cc','-std=gnu11','-O1','-g','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-label',
        '-DXV_OWNER_PHASE','-DXV_SCENE_PARTITION=1','-DXV_OWNER_PHASE_DEFAULT=1','-DXV_EXPERIMENTAL_OBJECT_JOBS',
        '-pthread','-fno-omit-frame-pointer','-no-pie','-fsanitize=address,undefined']
    fixture=ROOT/'tools/tests/scene_partition.c';exe=out/'boundary';run(cc+[fixture,'-o',exe])
    clean={k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    run([exe,1],env=clean);run([exe,0],env={**clean,'XV_OWNER_PHASE':'0'})
    for bad in ('2','-1'):
        r=run([v for v in cc if v!='-DXV_SCENE_PARTITION=1']+['-DXV_SCENE_PARTITION='+bad,'-fsyntax-only',fixture],ok=False)
        assert 'XV_SCENE_PARTITION must be 0 or 1' in r.stderr
    r=run([v for v in cc if v!='-DXV_OWNER_PHASE']+['-fsyntax-only',fixture],ok=False)
    assert 'XV_SCENE_PARTITION requires XV_OWNER_PHASE' in r.stderr
    units={p.name:p.read_text() for p in (a.retained_build/'recomp').glob('code_*.c')}
    functions={m[1]:m[0] for text in units.values() for m in re.finditer(r'^void f_([0-9A-F]{8})\([^\n]*\)\n\{\n.*?^\}\n',text,re.M|re.S)}
    original=functions['0005D410'];candidate=a.generated_body.read_text()
    assert strip(candidate)==original and hook(original)==candidate
    for bad in (original.replace('0x5D4B0u','0x5D4B1u'),original.replace('f_000539C0(c)','f_000539C1(c)'),candidate):
        try:hook(bad)
        except ValueError:pass
        else:raise AssertionError('drift admitted')
    inc=[]
    for fn in dict.fromkeys(re.findall(r'\bf_([0-9A-F]{8})\(c\)',original)):
        returns=set(re.findall(r'  ret([^*]*?) \*/',functions[fn]))
        if fn=='000800E0':pop=8 # original tail SetRenderState_FillMode stdcall(1)
        else:
            assert len(returns)==1,(fn,returns)
            v=returns.pop().strip();pop=4+(int(v[:-1],16) if v.endswith('h') else int(v or 0))
        inc.append(f'void f_{fn}(xctx *c) {{ callback(c,0x{fn}u,{pop}u); }}')
    # Same captured-root macro binding as retained generated unit. x87/POP and
    # REP helpers retain their original header/global bindings.
    prologue=units['code_010.c'].split('\nvoid f_',1)[0].split('\n',1)[1]
    inc.extend([prologue,original.replace('f_0005D410(','f_scene_original(',1),candidate.replace('f_0005D410(','f_scene_observed(',1)])
    (out/'scene_bodies.inc').write_text('\n'.join(inc))
    exe=out/'body';run(cc+['-ffunction-sections','-fdata-sections','-I'+str(out),ROOT/'tools/tests/scene_partition_body.c',ROOT/'recomp/xv_x86rt.c','-Wl,--gc-sections','-lm','-o',exe]);body=run([exe],env=clean)
    stage=out/'build';run(['cp','-a','--reflink=auto',a.retained_build,stage])
    for name in ('Makefile','recomp/kernel/xk_owner_phase.c','recomp/kernel/xk_owner_phase.h'):
        shutil.copy2(ROOT/name,stage/name)
    (stage/'recomp/code_010.c').write_text(units['code_010.c'].replace(original,candidate,1))
    assert (stage/'recomp/code_010.c').read_text().replace(candidate,original,1)==units['code_010.c']
    base=json.loads(a.build_command.read_text())['command'][:-1]
    base=[x for x in base if not x.startswith('XV_SCENE_PARTITION=')]
    # Other explicit targets detect owner leakage without forcing a full rebuild.
    targets=['build/recomp/code_010.o','build/recomp/kernel/xk_owner_phase.o',
             'build/recomp/code_017.o','build/recomp/code_022.o','build/recomp/kernel/xd3d.o',
             'build/recomp/query_fusion.o','build/recomp/solver_fusion.o','build/runtime/main.o',
             'build/runtime/xv_ui_gxm.o','build/runtime/xv_d3d.o','build/runtime/xv_vertex_upload.o']
    kept={str(p.relative_to(stage)):sha(p) for p in (stage/'build').rglob('*.o') if p.name not in ('code_010.o','xk_owner_phase.o')}
    builds=[];off={};owner_objects=('build/recomp/code_010.o','build/recomp/kernel/xk_owner_phase.o')
    for label,value in (('default',None),('off-noop',0),('on',1),('on-noop',1),('off',0),('off-repeat',0)):
        r=run(base+([] if value is None else ['XV_SCENE_PARTITION='+str(value)])+targets,cwd=stage)
        compiled=[x for x in r.stdout.splitlines() if ' -c ' in x]
        if label in ('off-noop','on-noop','off-repeat'):assert not compiled,(label,compiled)
        elif label!='default':assert len(compiled)==2 and all(any(' -c '+p+' ' in c for p in ('recomp/code_010.c','recomp/kernel/xk_owner_phase.c'))for c in compiled),(label,compiled)
        for name,digest in kept.items():assert sha(stage/name)==digest,(label,name)
        hashes={n:sha(stage/n) for n in owner_objects}
        if not value:
            if not off:off=hashes
            else:assert hashes==off
            assert hashes[owner_objects[0]]==sha(a.retained_build/owner_objects[0])
        for n in owner_objects:shutil.copy2(stage/n,out/(label+'-'+Path(n).name))
        builds.append(dict(label=label,selector=value,compiled=compiled,objects=hashes))
    for bad in ('','2','-1','0 1','invalid'):
        r=run(base+['XV_SCENE_PARTITION='+bad,targets[0]],cwd=stage,ok=False)
        assert 'XV_SCENE_PARTITION must be 0 or 1' in r.stderr
    for setting in ('XV_OWNER_PHASE=0','GAME_PROFILE=unsupported'):
        prefix=setting.split('=')[0]+'='
        r=run([x for x in base if not x.startswith(prefix)]+[setting,'XV_SCENE_PARTITION=1',targets[0]],cwd=stage,ok=False)
        assert 'XV_SCENE_PARTITION requires' in r.stderr
    # A missing body must fail before any compilation or output change.
    (stage/'recomp/code_010.c').write_text(units['code_010.c'])
    r=run(base+['XV_SCENE_PARTITION=1',targets[0]],cwd=stage,ok=False)
    assert 'selectively regenerated primary 5D410 scope'in r.stderr
    (stage/'recomp/code_010.c').write_text(units['code_010.c'].replace(original,candidate,1))
    result=dict(result='PASS',body=body.stdout,builds=builds,preserved_objects=kept,
        scope='Actual primary generated CFG with deterministic child/HLE stand-ins; original REP runtime; complete host context/8MiB/table hashes at callbacks. ARM production compiles, no device or game-speed result.',
        sources={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'Makefile',ROOT/'recomp/kernel/xk_owner_phase.c',ROOT/'recomp/kernel/xk_owner_phase.h',ROOT/'games/halo_ce_3925/scene_partition.py',ROOT/'tools/tests/scene_partition.c',ROOT/'tools/tests/scene_partition_body.c',Path(__file__)]})
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS scene observer accounting, generated CFG callbacks, six retained ARM transitions; only selected scene/observer objects change')

if __name__=='__main__':main()
