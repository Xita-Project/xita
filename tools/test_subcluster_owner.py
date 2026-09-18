#!/usr/bin/env python3
"""Real pthread-backend admission and sanitizer checks for native subclusters."""
import argparse,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--clip-source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private output required')
    a.out.mkdir(parents=True,exist_ok=False)
    s=(ROOT/'tools/tests/clip_region_startup.c').read_text().replace('ARENA_BYTES=2u<<20','ARENA_BYTES=8u<<20')
    s=s.replace('#include "../../recomp/kernel/xk_object_jobs.c"','#include "'+str(ROOT/'recomp/kernel/xk_object_jobs.c')+'"')
    s=s.replace('#include "../../recomp/kernel/xk_clip_trial.h"','#include "'+str(ROOT/'recomp/kernel/xk_clip_trial.h')+'"\n#include "kernel/xk_subcluster.h"')
    assert s.count('    XV_CLIP_TRIAL_PRESENT(c);')==1
    s=s.replace('    XV_CLIP_TRIAL_PRESENT(c);','    assert(!xv_subcluster_bounds(c)); assert(!xv_subcluster_publish(c));\n    XV_CLIP_TRIAL_PRESENT(c);')
    s+='\nuint32_t xk_mem_arena_size(void) { return ARENA_BYTES; }\n'
    (a.out/'subcluster_backend_fixture.c').write_text(s)
    common=[os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-pthread','-fno-strict-aliasing','-ffp-contract=off','-frounding-math','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_LIGHT_QUERY_CENSUS','-DXV_NATIVE_CLIP_REGION','-DXV_CLIP_REGION_TRIAL=1',
        '-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel'),'-I'+str(a.out),'-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    shared=[str(a.clip_source.resolve()),str(ROOT/'recomp/kernel/xk_clip_region_control.c'),str(ROOT/'recomp/kernel/xk_subcluster.c'),str(ROOT/'recomp/kernel/xk_subcluster_math.c'),str(ROOT/'recomp/xv_x86rt.c')]
    linker=['-Wl,--gc-sections,--wrap=xv_native_clip_region_init,--wrap=xv_native_clip_region_override,--wrap=xv_clip_region_compatible','-lm']
    rows=[];commands=[]
    for name,source,args in [('backend',a.out/'subcluster_backend_fixture.c',['admission']),('owner',ROOT/'tools/tests/subcluster_owner.c',[])]:
        cmd=[*common,str(source),*shared,*linker,'-o',str(a.out/name)];commands.append(cmd)
        with (a.out/(name+'-compile.log')).open('w') as f:subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT)
        r=subprocess.run([str(a.out/name),*args],capture_output=True,text=True,timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        (a.out/(name+'-run.log')).write_text(r.stdout+r.stderr);rows.append(dict(name=name,exit_code=r.returncode,output=r.stdout.strip()))
        (a.out/'result.json').write_text(json.dumps(dict(results=rows,commands=commands),indent=2)+'\n');r.check_returncode();print(r.stdout.strip(),flush=True)
if __name__=='__main__':main()
