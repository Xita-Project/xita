#!/usr/bin/env python3
"""Check version guards and recover the unmodified BSP reference from local XBE.

Requires the recompiler's iced-x86 dependency; does not regenerate the game.
"""
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root))
import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks

img=r.Image(str(root/'haloce/default.xbe'),str(root/'game_manifest.json'))
disc=r.Discovery(img,{},img.kernel_imports(),lambda *args:None)
for address in (0x88b80,0x114c50):disc.add_root(address)
for address in (0x88b80,0x114c50):
    disc.lift_function(disc.functions[address]);disc.split_blocks(disc.functions[address])
emitter=r.Emitter(img,disc,{},img.kernel_imports(),'unused',1,hooks=HaloHooks(img))
for address,marker in ((0x88b80,'xv_bsp_plane_interval(c);'),(0x114c50,'xk_quality_decal_budget();')):
    out=emitter.emit_function(disc.functions[address])
    assert out.count(marker)==1,(hex(address),out.count(marker))
    original=img.bytes_at
    img.bytes_at=lambda p,n:b'\x00'*n
    try:unmodified=emitter.emit_function(disc.functions[address])
    finally:img.bytes_at=original
    assert marker not in unmodified
    if address==0x88b80:
        reference=unmodified[unmodified.index('    /* 00088BA5 '):unmodified.index('    /* 00088BF9 ')]
        path=root/'recomp/host/build/original_bsp_interval.c'
        if path.exists():assert path.read_text()==reference
        else:path.parent.mkdir(parents=True,exist_ok=True);path.write_text(reference)
    print(hex(address),'guarded hook emitted once; mismatched image retains original code')
