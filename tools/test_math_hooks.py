#!/usr/bin/env python3
"""Validate exact-image math hooks and recover independent original lifts."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
img=r.Image(str(root/'haloce/default.xbe'),str(root/'local/halo_ce_3925/game_manifest.json'))
disc=HaloDiscovery(img,{},img.kernel_imports(),lambda *args:None)
for address in (0xB5B40,0xB5F60,0xB5EA0):
    disc.add_root(address)
    disc.lift_function(disc.functions[address]);disc.split_blocks(disc.functions[address])
emitter=r.Emitter(img,disc,{},img.kernel_imports(),'unused',1,hooks=HaloHooks(img))
for address,helper,size in ((0xB5B40,'xv_math_matrix_multiply',339),(0xB5F60,'xv_math_quaternion_matrix',291),(0xB5EA0,'xv_math_point_transform',105)):
    emitted=emitter.emit_function(disc.functions[address])
    assert emitted.count(f'if ({helper}(c)) return;')==1
    original=img.bytes_at
    def changed(p,n):
        data=bytearray(original(p,n))
        if p==address and n==size:data[-1]^=1
        return bytes(data)
    img.bytes_at=changed
    try:reference=emitter.emit_function(disc.functions[address])
    finally:img.bytes_at=original
    assert helper not in reference
    target=root/f'recomp/host/build/original_{address:08X}.c'
    target.parent.mkdir(parents=True,exist_ok=True);target.write_text(reference+'\n')
    print(hex(address),'hook verified; changed tail byte retains original lift')
