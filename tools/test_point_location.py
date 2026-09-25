#!/usr/bin/env python3
"""Build a differential prototype against a private retained CE translation.
No guest code is stored in the repository. --cc cross compiler builds only.
"""
import argparse, hashlib, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('stage',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='cc');a=p.parse_args()
root=Path(__file__).resolve().parents[1]
s=(a.stage/'recomp/code_028.c').read_text();start=s.index('void f_0017A8B0(');end=s.index('\nvoid f_',start+5);body=s[start:end]
assert hashlib.sha256(body.encode()).hexdigest()=='35811f19270e8c5e14d406924f45fda37f12c21a72374ecd8b30df5b6ebce0a0', 'reference changed'
a.out.mkdir(parents=True,exist_ok=True)
(a.out/'reference.c').write_text('#include "xv_x86rt.h"\n'+body)
cmd=[a.cc,'-O2','-ffp-contract=off','-fno-fast-math','-I'+str(a.stage/'recomp'),'-I'+str(root/'recomp/kernel'),str(a.out/'reference.c'),str(root/'tools/tests/point_location.c'),'-lm','-o',str(a.out/'test')]
if 'arm-' in a.cc:cmd[1:1]=['-static','-mcpu=cortex-a9','-mfpu=neon-vfpv3','-mfloat-abi=hard','-mthumb']
subprocess.run(cmd,check=True)
if 'arm-' not in a.cc:subprocess.run([str(a.out/'test')],check=True)
