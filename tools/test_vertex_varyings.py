#!/usr/bin/env python3
"""Check compiled vertex outputs against generated Cg and fragment fog contract.

GXP layout reference:
https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h
https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/src/gxp.cpp
Only metadata is read; this is not a shader execution or hardware test.
"""
from pathlib import Path
import re, struct
root = Path(__file__).resolve().parents[1]
layout = (root / 'shaders/xv_layouts.h').read_text()
paths = sorted(set(re.findall(r'"app0:(shaders/halo_vs_\d+\.gxp)"', layout)))
assert paths, 'No generated vertex layouts'
fog_count = 0
for name in paths:
    path = root / name
    source = path.with_suffix('.cg').read_text()
    body = re.search(r'struct VertOut\s*\{(.*?)\};', source, re.S)
    assert body, name
    expected = set(re.findall(r':\s*(POSITION|COLOR[01]|TEXCOORD\d+|FOG|PSIZE)\s*;', body[1]))
    assert 'FOG' not in expected, f'{name}: regenerate fog output as TEXCOORD7'
    if '// oFog' in body[1]:
        assert re.search(r'\bfog\s*:\s*TEXCOORD7\s*;', body[1]), name
        fog_count += 1
    d = path.read_bytes()
    def u32(off):
        assert 0 <= off <= len(d)-4, name
        return struct.unpack_from('<I', d, off)[0]
    assert d[:4] == b'GXP\0' and 0x30 <= u32(8) <= len(d), name
    assert not u32(0x14) & 1, name
    v = 0x2c + u32(0x2c)
    assert 0x30 <= v and v+32 <= u32(8), name
    flags, texcoords = u32(v+16), u32(v+20)
    actual = {'POSITION'}
    for semantic, bit in [('FOG', 0x200), ('COLOR0', 0x800), ('COLOR1', 0x400), ('PSIZE', 0x100)]:
        if flags & bit:
            actual.add(semantic)
    actual.update(f'TEXCOORD{i}' for i in range(10) if (texcoords >> (3*i)) & 7)
    assert actual == expected, f'{name}: compiled {sorted(actual)} != source {sorted(expected)}'
assert fog_count, 'No fog outputs checked'
print(f'PASS: {len(paths)} compiled vertex interfaces match source, including {fog_count} TEXCOORD7 fog outputs')
