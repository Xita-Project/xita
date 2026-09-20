#!/usr/bin/env python3
"""Validate captured quaternion arithmetic against the owned original guest lift."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.discovery import HaloDiscovery

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    image=r.Image(a.xbe,a.manifest)
    d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
    address=0xB5F60
    d.add_root(address);d.lift_function(d.functions[address]);d.split_blocks(d.functions[address])
    emitter=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=NoGameHooks())
    reference=a.out/'original.c'
    reference.write_text('#include "xv_x86rt.h"\n'+emitter.emit_function(d.functions[address]))
    binary=a.out/'check'
    subprocess.run(['cc','-O2','-g','-fno-strict-aliasing','-ffp-contract=off',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
        '-ffunction-sections','-fdata-sections','-I'+str(ROOT/'recomp'),
        str(ROOT/'tools/tests/quaternion_snapshot.c'),str(reference),
        str(ROOT/'recomp/kernel/xk_math.c'),str(ROOT/'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

if __name__=='__main__':main()
