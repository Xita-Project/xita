#!/usr/bin/env python3
"""Qualify copied visibility packets on the actual pthread object workers."""
import argparse,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--clip-source',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--sanitizer',choices=['address,undefined','thread','none'],default='address,undefined');a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private output required')
    a.out.mkdir(parents=True,exist_ok=False)
    s=(ROOT/'tools/tests/clip_region_startup.c').read_text()
    for name in ('xk_object_jobs.c','xk_clip_trial.h'):
        s=s.replace('../../recomp/kernel/'+name,str(ROOT/'recomp/kernel'/name))
    s=s.replace('t_worker++;c->r[4]+=4;','__atomic_fetch_add(&t_worker,1,__ATOMIC_RELAXED);c->r[4]+=4;')
    (a.out/'visibility_backend_fixture.c').write_text(s)
    flags=['-O2','-g','-std=gnu11','-pthread','-fno-strict-aliasing','-ffp-contract=off','-frounding-math','-ffunction-sections','-fdata-sections',
        '-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_LIGHT_QUERY_CENSUS','-DXV_NATIVE_CLIP_REGION','-DXV_CLIP_REGION_TRIAL=1','-DXV_NATIVE_VISIBILITY_JOBS=1',
        '-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel'),'-I'+str(a.out),'-fno-omit-frame-pointer','-no-pie']
    if a.sanitizer!='none':flags+=['-fsanitize='+a.sanitizer]
    cmd=[os.environ.get('CC','cc'),*flags,str(ROOT/'tools/tests/visibility_jobs.c'),str(a.clip_source.resolve()),
        str(ROOT/'recomp/kernel/xk_clip_region_control.c'),str(ROOT/'recomp/kernel/xk_subcluster_math.c'),str(ROOT/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections,--wrap=fegetenv,--wrap=xs_bounds,--wrap=xv_native_clip_region_init,--wrap=xv_native_clip_region_override,--wrap=xv_clip_region_compatible',
        '-lm','-o',str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    with (a.out/'compile.log').open('w') as f:subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT)
    r=subprocess.run([str(a.out/'test')],capture_output=True,text=True,timeout=120,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',TSAN_OPTIONS='halt_on_error=1'))
    (a.out/'run.log').write_text(r.stdout+r.stderr);(a.out/'result.json').write_text(json.dumps(dict(exit_code=r.returncode,output=r.stdout.strip(),sanitizer=a.sanitizer),indent=2)+'\n')
    r.check_returncode();print(r.stdout.strip(),flush=True)
if __name__=='__main__':main()
