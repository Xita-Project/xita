#!/usr/bin/env python3
"""Differential test of private 85020 polygon writers; no game code is stored here."""
import argparse, hashlib, json, shlex, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path); p.add_argument('candidate', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--headers', type=Path, required=True)
    p.add_argument('--cc', default='cc'); p.add_argument('--flags', default='')
    p.add_argument('--build-only', action='store_true'); p.add_argument('--cases', type=int, default=1024)
    a=p.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
    sources=[]; hashes=[]
    for name,path in [('reference',a.reference),('candidate',a.candidate)]:
        body=path.read_text()
        if body.count('void f_00085020(')!=1: raise ValueError('expected one 85020 body')
        hashes.append(hashlib.sha256(body.encode()).hexdigest())
        dest=a.output/(name+'.c')
        dest.write_text('#include "xv_x86rt.h"\n'+body.replace('void f_00085020(', 'void '+name+'('))
        sources.append(str(dest))
    cmd=[a.cc,'-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off',
         '-DXV_THREAD_PAGE_TABLE=1','-I'+str(a.headers),*shlex.split(a.flags),
         *sources,str(ROOT/'tools/tests/collision_polygon.c'),'-lm','-o',str(a.output/'test')]
    subprocess.run(cmd,check=True)
    (a.output/'build.json').write_text(json.dumps(dict(command=cmd,sha256=hashes),indent=2))
    if not a.build_only:
        r=subprocess.run([str(a.output/'test'),str(a.cases)],capture_output=True,text=True,timeout=120)
        (a.output/'result.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');r.check_returncode()
if __name__=='__main__': main()
