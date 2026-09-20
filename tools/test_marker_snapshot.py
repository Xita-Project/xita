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
from games.halo_ce_3925 import marker_region
from games.halo_ce_3925.hooks import HaloHooks

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--workers',action='store_true');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    image=r.Image(a.xbe,a.manifest)
    d=HaloDiscovery(image,{},image.kernel_imports(),lambda *args:None)
    for address in (0xA1EC0,0xB5F60,0xB5B40):
        d.add_root(address);d.lift_function(d.functions[address]);d.split_blocks(d.functions[address])
    emitter=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=NoGameHooks())
    body=emitter.emit_function(d.functions[0xA1EC0])
    assert marker_region.matches(image)
    hooked=marker_region.hook(body)
    assert hooked.count('if (xv_math_marker_record(c))')==1
    production=r.Emitter(image,d,{},image.kernel_imports(),'unused',1,hooks=HaloHooks(image))
    assert 'if (xv_math_marker_record(c))' in production.emit_function(d.functions[0xA1EC0])
    assert marker_region.hook(body.replace('0xA1F6Du','0xA1F6Eu'))==body.replace('0xA1F6Du','0xA1F6Eu')
    read=image.bytes_at
    for address,size,_ in marker_region.SPANS:
        def changed(p,n):
            data=bytearray(read(p,n))
            if (p,n)==(address,size):data[-1]^=1
            return bytes(data)
        image.bytes_at=changed
        try:assert not marker_region.matches(image)
        finally:image.bytes_at=read
    # Disabled source must preprocess identically, including continuation labels.
    plain=a.out/'plain.c';off=a.out/'off.c'
    plain.write_text(body);off.write_text(hooked)
    assert subprocess.check_output(['cc','-E','-P',str(plain)])==subprocess.check_output(['cc','-E','-P',str(off)])
    region=body[:body.index('L_000A1EC0:')]+body[body.index('L_000A1F5F:'):body.index('    /* 000A1F9A')]+ '    return;\n}\n'
    raw=region.replace('f_000A1EC0','original_marker')
    current=region.replace('f_000A1EC0','current_marker').replace('f_000B5F60(c);','(void)xv_math_quaternion_matrix(c);').replace('f_000B5B40(c);','(void)xv_math_matrix_multiply(c);')
    admitted=hooked[:hooked.index('L_000A1EC0:')]+hooked[hooked.index('L_000A1F5F:'):hooked.index('    /* 000A1F9A')]+ '    return;\n}\n'
    admitted=admitted.replace('f_000A1EC0','hooked_marker').replace('f_000B5F60(c);','(void)xv_math_quaternion_matrix(c);').replace('f_000B5B40(c);','(void)xv_math_matrix_multiply(c);')
    source=a.out/'reference.c'
    source.write_text('#include "xv_x86rt.h"\nint xv_math_quaternion_matrix(xctx *),xv_math_matrix_multiply(xctx *);\n'+
        '\n'.join(emitter.emit_function(d.functions[n]) for n in (0xB5F60,0xB5B40))+raw+current+admitted)
    binary=a.out/'check'
    command=['cc','-O2','-g','-fno-strict-aliasing','-ffp-contract=off',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',
        '-ffunction-sections','-fdata-sections','-I'+str(ROOT/'recomp'),
        str(ROOT/'tools/tests/marker_snapshot.c'),str(source),str(ROOT/'recomp/kernel/xk_math.c'),
        str(ROOT/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(binary)]
    if a.workers:
        command[command.index(str(ROOT/'tools/tests/marker_snapshot.c'))]=str(ROOT/'tools/tests/marker_workers.c')
        command[1:1]=['-pthread','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_MARKER_RECORD=1',
            '-Wl,--wrap=xv_object_math_unlock',str(ROOT/'recomp/kernel/xk_marker.c')]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True,timeout=90)
    if a.workers:return
    # The shared matrix extraction also serves ordinary, aliasing and fallback
    # leaf calls. Exercise that full existing fixture with these owned lifts.
    command[command.index(str(ROOT/'tools/tests/marker_snapshot.c'))]=str(ROOT/'tools/tests/native_math.c')
    command[-1]=str(a.out/'leaf-check')
    subprocess.run(command,check=True)
    subprocess.run([command[-1]],check=True)
    subprocess.run([command[-1],'disabled'],check=True)

if __name__=='__main__':main()
