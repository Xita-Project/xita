#!/usr/bin/env python3
"""Build a differential prototype against a private retained CE translation.
No guest code is stored in the repository. --cc cross compiler builds only.
"""
import argparse, hashlib, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('stage',type=Path);p.add_argument('--out',type=Path,required=True);p.add_argument('--cc',default='cc');p.add_argument('--snapshot',action='store_true');p.add_argument('--mutant',choices=['side','dead-slot']);p.add_argument('--verify',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1]
s=(a.stage/'recomp/code_028.c').read_text();start=s.index('void f_0017A8B0(');end=s.index('\nvoid f_',start+5);body=s[start:end]
assert hashlib.sha256(body.encode()).hexdigest()=='35811f19270e8c5e14d406924f45fda37f12c21a72374ecd8b30df5b6ebce0a0', 'reference changed'
a.out.mkdir(parents=True,exist_ok=True)
(a.out/'reference.c').write_text('#include "xv_x86rt.h"\n'+body)
if not a.mutant and (a.out/'xk_point_location.h').exists():
    raise SystemExit('Use a clean output directory: local candidate override exists')
if a.mutant:
    h=(root/'recomp/kernel/xk_point_location.h').read_text()
    old,new = ('? 4u : 8u','? 8u : 4u') if a.mutant=='side' else ('c->st[(c->fsp - 1u) & 7u] = x;', '(void)x;')
    assert old in h
    (a.out/'xk_point_location.h').write_text(h.replace(old,new))
cmd=[a.cc,'-O2','-ffp-contract=off','-fno-fast-math','-I'+str(a.stage/'recomp'),'-I'+str(a.out),'-I'+str(root/'recomp/kernel'),str(a.out/'reference.c'),str(root/'tools/tests/point_location.c'),'-lm','-o',str(a.out/'test')]
if 'arm-' in a.cc:cmd[1:1]=['-static','-mcpu=cortex-a9','-mfpu=neon-vfpv3','-mfloat-abi=hard','-mthumb']
if a.snapshot:cmd[1:1]=['-DXV_THREAD_PAGE_TABLE=1']
if a.verify:
    cmd[1:1]=['-DPL_TEST_HOOK=1']
    (a.out/'xk_point_location_hook.h').write_text((root/'recomp/kernel/xk_point_location_hook.h').read_text())
subprocess.run(cmd,check=True)
if 'arm-' not in a.cc:
    r=subprocess.run([str(a.out/'test')],capture_output=True,text=True)
    if a.mutant:
        assert r.returncode==1 and 'mismatch case' in r.stderr, 'mutant was not detected'
        print('Caught mutant:',a.mutant)
    else:
        print(r.stdout,end='');print(r.stderr,end='')
        r.check_returncode()
