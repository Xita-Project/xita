#!/usr/bin/env python3
"""Actual Make parse validation plus production remote request/poll selectors."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    stage = out/'parse-snapshot'; assert not stage.exists(); stage.mkdir()
    names = ['Makefile', 'games/halo_ce_3925/runtime.mk']
    for n in names:
        d=stage/n;d.parent.mkdir(parents=True, exist_ok=True);shutil.copyfile(ROOT/n,d)
    (stage/'recomp').mkdir();(stage/'recomp/code_000.c').write_text('/* XV_NATIVE_POLYGON_EDGE */\n')
    (stage/'parse-only.mk').write_text('.PHONY: trial_parse_only\ntrial_parse_only:\n')
    env = {k:v for k,v in os.environ.items() if not k.startswith('XV_')}
    env.setdefault('VITASDK',str(Path.home()/'vitasdk'))
    base = {'RECOMP':'1','GAME_PROFILE':'halo_ce_3925','XV_NATIVE_POLYGON_EDGE':'1',
            'XV_LIGHT_QUERY_CENSUS':'1','XV_EXPERIMENTAL_OBJECT_JOBS':'1'}
    rows=[]
    def check(name, flag, changes=None, error=None):
        options=dict(base);options.update(changes or {})
        if flag is not None:options['XV_POLYGON_EDGE_TRIAL']=flag
        cmd=['make','-rR','-n','-f','Makefile','-f','parse-only.mk']+[k+'='+v for k,v in options.items() if v is not None]+['trial_parse_only']
        before={str(f.relative_to(stage)):sha(f) for f in stage.rglob('*') if f.is_file()}
        r=subprocess.run(cmd,cwd=stage,env=env,text=True,capture_output=True,timeout=20)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        assert before=={str(f.relative_to(stage)):sha(f) for f in stage.rglob('*') if f.is_file()}
        assert (r.returncode!=0 and error in r.stdout+r.stderr) if error else r.returncode==0,(name,r.stdout,r.stderr)
        rows.append(dict(name=name,command=cmd,exit=r.returncode,expected_error=error))
    check('default',None);check('off','0');check('on','1')
    disabled={'RECOMP':'0','XV_NATIVE_POLYGON_EDGE':'0','XV_LIGHT_QUERY_CENSUS':'0','XV_EXPERIMENTAL_OBJECT_JOBS':'0'}
    check('default-without-prerequisites',None,disabled);check('off-without-prerequisites','0',disabled)
    for name,value in [('empty',''),('two','2'),('negative','-1'),('text','true'),('multi','0 1'),('noncanonical','01')]:
        check('invalid-'+name,value,error='XV_POLYGON_EDGE_TRIAL must be 0 or 1')
    prerequisite='XV_POLYGON_EDGE_TRIAL requires RECOMP=1 GAME_PROFILE=halo_ce_3925 XV_NATIVE_POLYGON_EDGE=1 XV_LIGHT_QUERY_CENSUS=1 XV_EXPERIMENTAL_OBJECT_JOBS=1'
    check('wrong-recomp','1',{'RECOMP':'0'},prerequisite);check('wrong-profile','1',{'GAME_PROFILE':'wrong'},prerequisite)
    for option in ['XV_NATIVE_POLYGON_EDGE','XV_LIGHT_QUERY_CENSUS','XV_EXPERIMENTAL_OBJECT_JOBS']:
        for value,tag in [('0','zero'),('','empty'),(None,'unset')]:
            check(option.lower()+'-'+tag,'1',{option:value},prerequisite)
    (out/'make-results.json').write_text(json.dumps(rows,indent=2)+'\n')
    runs=[]
    for name,defs in [('absent',[]),('off-off',['-DXV_POLYGON_EDGE_TRIAL=0','-DXV_CLIP_REGION_TRIAL=0']),
                      ('off-on',['-DXV_POLYGON_EDGE_TRIAL=0','-DXV_CLIP_REGION_TRIAL=1']),
                      ('on-off',['-DXV_POLYGON_EDGE_TRIAL=1','-DXV_CLIP_REGION_TRIAL=0']),
                      ('on-on',['-DXV_POLYGON_EDGE_TRIAL=1','-DXV_CLIP_REGION_TRIAL=1'])]:
        binary=out/('remote-'+name)
        cmd=[os.environ.get('CC','cc'),'-O1','-g','-std=gnu11','-ffunction-sections','-fdata-sections',
             '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-Wl,--gc-sections',
             '-DXV_LIGHT_QUERY_CENSUS','-DXV_EXPERIMENTAL_OBJECT_JOBS',*defs,
             str(ROOT/'tools/tests/polygon_edge_trial_admission.c'),'-lm','-o',str(binary)]
        r=subprocess.run(cmd,env=env,text=True,capture_output=True);(out/('compile-'+name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
        r=subprocess.run([str(binary)],env=env,text=True,capture_output=True,timeout=20)
        (out/('remote-'+name+'.log')).write_text(r.stdout+r.stderr);r.check_returncode()
        runs.append(dict(name=name,command=cmd,output=r.stdout.strip(),binary_sha256=sha(binary)))
    assert runs[0]['output']==runs[1]['output']
    sources=['Makefile','runtime/xv_benchmark.c','runtime/xv_benchmark.h',
             'tools/tests/polygon_edge_trial_admission.c','tools/test_polygon_edge_trial_admission.py']
    receipt=dict(pass_all=True,make=rows,remote=runs,source_sha256={n:sha(ROOT/n) for n in sources},
                 scope='Make parse only and actual request/poll with observable control doubles; no gameplay, timing benchmark or benchmark arm execution.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(dict(pass_all=True,make_cases=len(rows),remote_builds=len(runs))))


if __name__=='__main__':
    main()
