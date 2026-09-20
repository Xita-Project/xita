#!/usr/bin/env python3
"""Check marker-record snapshot arithmetic; does not authorize shared input reads."""
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
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    image=r.Image(a.xbe,a.manifest)
    d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
    for address in (0xA1EC0,0xB5F60,0xB5B40):
        d.add_root(address);d.lift_function(d.functions[address]);d.split_blocks(d.functions[address])
    emitter=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=NoGameHooks())
    body=emitter.emit_function(d.functions[0xA1EC0])
    region=body[:body.index('L_000A1EC0:')]+body[body.index('L_000A1F5F:'):body.index('    /* 000A1F9A')]+ '    return;\n}\n'
    raw=region.replace('f_000A1EC0','original_marker')
    current=region.replace('f_000A1EC0','current_marker').replace('f_000B5F60(c);','(void)xv_math_quaternion_matrix(c);').replace('f_000B5B40(c);','(void)xv_math_matrix_multiply(c);')
    source=a.out/'reference.c'
    source.write_text('#include "xv_x86rt.h"\nint xv_math_quaternion_matrix(xctx *),xv_math_matrix_multiply(xctx *);\n'+
        '\n'.join(emitter.emit_function(d.functions[n]) for n in (0xB5F60,0xB5B40))+raw+current)
    binary=a.out/'check'
    command=['cc','-O2','-g','-fno-strict-aliasing','-ffp-contract=off',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
        '-ffunction-sections','-fdata-sections','-I'+str(ROOT/'recomp'),
        str(ROOT/'tools/tests/marker_snapshot.c'),str(source),str(ROOT/'recomp/kernel/xk_math.c'),
        str(ROOT/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
    # The shared matrix extraction also serves ordinary, aliasing and fallback
    # leaf calls. Exercise that full existing fixture with these owned lifts.
    command[command.index(str(ROOT/'tools/tests/marker_snapshot.c'))]=str(ROOT/'tools/tests/native_math.c')
    command[-1]=str(a.out/'leaf-check')
    subprocess.run(command,check=True)
    subprocess.run([command[-1]],check=True)
    subprocess.run([command[-1],'disabled'],check=True)

if __name__=='__main__':main()
