#!/usr/bin/env python3
"""Compare an experimental bit-scan batch with the retained, pinned guest lift.
The supplied generated code stays outside the repository. No production hook.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
s=a.reference.read_text().split('void f_00053E90(xctx *restrict c)\n',1)[1].split('\nvoid f_',1)[0]
if hashlib.sha256(s.encode()).hexdigest()!='325e0b3eb548cb30a5f46f0d5922c1a635a9ad2c859a65cabfb1204b469b3b84':
    raise SystemExit('unsupported retained body')
original='void original(xctx *restrict c)\n'+s
# Eligibility is a performance hint only. The batch still reloads and validates
# live inputs, so mutations at original yields cannot make it use stale masks.
body=s.replace('{\n','{\n    int mask_sparse=0;\n',1)
body=body.replace('L_00053EB9:\n', 'L_00053EB9:\n    mask_sparse=__builtin_popcount(X_M32(c->r[0]))<=8;\n')
body=body.replace('L_00053ED0:\n','L_00053ED0:\n    if(mask_sparse) batches += !!xv_surface_mask_zero_run(c,xram_,xpt_,(const uint8_t *)&X_IMG32(0x39BE58u));\n')
candidate='void candidate(xctx *restrict c)\n'+body
f=a.out/'reference.c';f.write_text('#include "kernel/xk_surface_mask_scan.h"\nextern unsigned batches;\n'+original+candidate)
cmd=[os.environ.get('CC','cc'),'-O2','-g','-fno-strict-aliasing','-fsanitize=address,undefined','-no-pie','-I'+str(ROOT/'recomp'),str(f),str(ROOT/'tools/tests/surface_mask_scan.c'),'-o',str(a.out/'test')]
(a.out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
subprocess.run(cmd,check=True);subprocess.run([str(a.out/'test')],check=True)
