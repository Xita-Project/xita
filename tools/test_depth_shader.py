#!/usr/bin/env python3
"""Verify that the compiled depth program has no fragment input dependencies.

Layout: https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h
(SceGxmProgram and SceGxmProgramVertexVaryings).
"""
from pathlib import Path
import hashlib, re, struct
root=Path(__file__).resolve().parents[1]
name='ps_28CF808C_07_na.frag.gxp'
data=(root/'shaders'/name).read_bytes()
assert hashlib.sha256(data).hexdigest()=='9b9fef26ecb86680340b26d0f9084d27b0344270d075c01724f8004ec63d60ed'
u32=lambda offset:struct.unpack_from('<I',data,offset)[0]
assert data[:4]==b'GXP\0' and u32(8)==len(data)
assert u32(0x14)&1 and not u32(0x14)&((1<<3)|(1<<4)|(1<<6)|(1<<7)|(1<<14))
assert u32(0x18)==u32(0x1c)==u32(0x20)==u32(0x24)==u32(0x64)==0
varyings=0x2c+u32(0x2c)
assert varyings+32<=len(data) and struct.unpack_from('<H',data,varyings+12)[0]==0
# Control: the old color passthrough does depend on a varying.
color=(root/'shaders/xv_color.frag.gxp').read_bytes()
offset=0x2c+struct.unpack_from('<I',color,0x2c)[0]
assert struct.unpack_from('<H',color,offset+12)[0]==1
header=(root/'shaders/xv_ps_gxp.h').read_text()
entry=re.search(r'\{"app0:shaders/'+re.escape(name)+r'", (xv_ps_bytes_\d+),',header)
assert entry
array=re.search(r'static const uint8_t '+entry[1]+r'\[\] = \{(.*?)\};',header,re.S)
assert array and bytes(int(x,16)for x in re.findall(r'0x([0-9a-fA-F]{2})',array[1]))==data
print('PASS: protected depth GXP has zero varyings, samplers, uniforms, discard, depth replacement or buffer stores; embedded bytes match')
