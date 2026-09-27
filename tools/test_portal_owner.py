#!/usr/bin/env python3
"""Actual pthread backend, clip controller and portal adapter admission checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--clip-source',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    if a.out.is_relative_to(ROOT):p.error('private outputs required')
    source=(ROOT/'tools/tests/clip_region_startup.c').read_text()
    source=source.replace('#include "../../recomp/kernel/xk_object_jobs.c"',
        '#include "'+str(ROOT/'recomp/kernel/xk_object_jobs.c')+'"')
    source=source.replace('#include "../../recomp/kernel/xk_clip_trial.h"',
        '#include "'+str(ROOT/'recomp/kernel/xk_clip_trial.h')+'"\n#include "kernel/xk_portal_polygon.h"')
    assert source.count('    XV_CLIP_TRIAL_PRESENT(c);')==1
    source=source.replace('    XV_CLIP_TRIAL_PRESENT(c);','    assert(!xv_portal_polygon(c));\n    XV_CLIP_TRIAL_PRESENT(c);')
    source+='\nuint32_t xk_mem_arena_size(void) { return ARENA_BYTES; }\n'
    (a.out/'backend.c').write_text(source)
    common=[os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-pthread','-fno-strict-aliasing',
        '-ffp-contract=off','-frounding-math','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_LIGHT_QUERY_CENSUS','-DXV_NATIVE_CLIP_REGION',
        '-DXV_CLIP_REGION_TRIAL=1','-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel'),
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    sources=[str(a.clip_source.resolve()),str(ROOT/'recomp/kernel/xk_clip_region_control.c'),
        str(ROOT/'recomp/kernel/xk_portal_polygon.c'),str(ROOT/'recomp/kernel/xk_portal_polygon_math.c'),
        str(ROOT/'recomp/xv_x86rt.c')]
    linker=['-Wl,--gc-sections,--wrap=xv_native_clip_region_init,--wrap=xv_native_clip_region_override,--wrap=xv_clip_region_compatible','-lm']
    results=[];commands=[]
    for name,fixture,cases in [('backend',a.out/'backend.c',['admission']),
            ('owner',ROOT/'tools/tests/portal_owner.c',[None]),
            ('scene',ROOT/'tools/tests/portal_scene.c',[None,'off'])]:
        cmd=[*common,*(['-DXV_THREAD_PAGE_TABLE=1'] if name=='scene' else []),str(fixture),*sources,*linker,'-o',str(a.out/name)]
        with (a.out/(name+'-compile.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
        commands.append(cmd)
        for case in cases:
            cmd=[str(a.out/name)]+([case] if case else [])
            r=subprocess.run(cmd,capture_output=True,text=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            (a.out/(name+('-'+case if case else '')+'-run.log')).write_text(r.stdout+r.stderr)
            results.append(dict(name=name,case=case,exit_code=r.returncode,output=r.stdout.strip()))
            (a.out/'result.json').write_text(json.dumps(dict(results=results,commands=commands),indent=2)+'\n')
            r.check_returncode();print(r.stdout.strip(),flush=True)

    # Compile production guard bodies, retaining only their reachable code.
    for name in ('view','stack'):
        cmd=[*common,'-DXV_THREAD_PAGE_TABLE=1','-DXV_RENDER_VIEW=1','-DXV_SCENE_THREAD=1',
             '-I'+str(ROOT/'runtime'),str(ROOT/f'tools/tests/portal_{name}_guard.c'),
             '-Wl,--gc-sections','-lm','-o',str(a.out/name)]
        with (a.out/(name+'-compile.log')).open('w') as f:
            subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
        commands.append(cmd)
        r=subprocess.run([str(a.out/name)],capture_output=True,text=True,timeout=60,
                         env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        (a.out/(name+'-run.log')).write_text(r.stdout+r.stderr)
        results.append(dict(name=name,exit_code=r.returncode,output=r.stdout.strip()))
        (a.out/'result.json').write_text(json.dumps(dict(results=results,commands=commands),indent=2)+'\n')
        r.check_returncode();print(r.stdout.strip(),flush=True)

if __name__=='__main__':main()
