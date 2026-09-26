#!/usr/bin/env python3
"""Compare private collision-transform memory/register x87 lowerings offline."""
import argparse, hashlib, json, re, shlex, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
IDS=('000B6210','000B5E40')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline',type=Path);p.add_argument('registers',type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',default='cc')
    p.add_argument('--function',choices=['all','inverse','vector'],default='all')
    p.add_argument('--extra',default='');p.add_argument('--build-only',action='store_true')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    src='#include "xv_x87reg.h"\n';hashes={}
    for prefix,path in [('reference',a.baseline),('candidate',a.registers)]:
        text=path.read_text()
        for name in IDS:
            found=re.findall(r'^void f_'+name+r'\(xctx \*restrict c\)\n\{\n.*?^}\n',text,re.M|re.S)
            if len(found)!=1:raise ValueError('missing/ambiguous '+name)
            b=found[0]
            if ('xfsp0' in b)!=(prefix=='candidate'):raise ValueError('wrong lowering: '+name)
            if re.search(r'^\s+f_[0-9A-F]+\(c\);',b,re.M):raise ValueError('unexpected callee')
            hashes[prefix+'_'+name]=hashlib.sha256(b.encode()).hexdigest()
            src+=b.replace('void f_'+name+'(', 'void '+prefix+'_'+name+'(',1)
    code=a.output/'collision-transform-bodies.c';code.write_text(src)
    cmd=[a.cc,'-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off','-I'+str(ROOT/'recomp'),
         '-I'+str(ROOT/'recomp/kernel'),str(code),str(ROOT/'tools/tests/collision_transform_registers.c'),
         '-lm','-o',str(a.output/'transform-test')]+shlex.split(a.extra)
    subprocess.run(cmd,check=True)
    (a.output/'build.json').write_text(json.dumps(dict(command=cmd,body_sha256=hashes),indent=2)+'\n')
    if not a.build_only:subprocess.run([str(a.output/'transform-test'),a.function],check=True)
if __name__=='__main__':main()
