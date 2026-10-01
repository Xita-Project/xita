#!/usr/bin/env python3
"""Check source transformation and the embedded, SDK-compiled candidates."""
import re
import struct
from specialize_ps_neutral_detail import ROOT, paths, specialize
header=(ROOT/'shaders/xv_ps_gxp.h').read_text()
count=0
for path in paths():
    for suffix in ('','_na','_gt'):
        src=ROOT/path.replace('.frag.gxp',suffix+'.frag.cg')
        target=ROOT/path.replace('.frag.gxp','_nd'+suffix+'.frag.cg')
        original=src.read_text(); candidate=target.read_text()
        assert candidate==specialize(original), target
        assert len(re.findall(r'\btex(?:2D|CUBE)\(',original))==4
        assert len(re.findall(r'\btex(?:2D|CUBE)\(',candidate))==3
        gxp=target.with_suffix('.gxp');data=gxp.read_bytes()
        assert data[:4]==b'GXP\0' and (struct.unpack_from('<I',data,8)[0]+3)&~3==len(data)
        # 0D already omits unused tex3; preserve each baseline's other samplers.
        baseline=src.with_suffix('.gxp').read_bytes()
        assert b'tex1\0' in baseline and b'tex1\0' not in data
        for name in (b'tex0\0',b'tex2\0',b'tex3\0'):
            assert (name in data)==(name in baseline)
        entry=re.search(r'\{"app0:shaders/'+re.escape(gxp.name)+r'", (xv_ps_bytes_\d+),',header)
        assert entry
        array=re.search(r'static const uint8_t '+entry[1]+r'\[\] = \{(.*?)\};',header,re.S)
        assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',array[1]))==data
        count+=1
assert count==18
print('PASS: 18 variants preserve source arithmetic/alpha, remove one of four fetches, omit tex1 reflection, and match embedded GXP bytes')
