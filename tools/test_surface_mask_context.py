#!/usr/bin/env python3
"""Build/run the pinned mask local-context fixture; generated game code is private."""
import argparse,json,shlex,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--cc',default='cc')
p.add_argument('--cflags',default='-fsanitize=address,undefined -no-pie')
p.add_argument('--build-only',action='store_true')
a=p.parse_args()
subprocess.run([sys.executable,str(ROOT/'tools/prepare_surface_mask_registers.py'),'--reference',str(a.reference),'--out',str(a.out),'--local-context'],check=True)
cmd=[a.cc,'-O2','-g','-fno-strict-aliasing',*shlex.split(a.cflags),'-I'+str(ROOT/'recomp'),str(a.out/'reference.c'),str(ROOT/'tools/tests/surface_mask_context.c'),'-o',str(a.out/'test')]
(a.out/'command.json').write_text(json.dumps(cmd,indent=2))
subprocess.run(cmd,check=True)
if not a.build_only:subprocess.run([str(a.out/'test')],check=True,timeout=120)
