#!/usr/bin/env python3
from pathlib import Path
import re,struct,json
from specialize_ps_cutout import ROOT,paths,specialize
from specialize_ps_alpha import START,END
sizes=[]
header=(ROOT/'shaders/xv_ps_gxp.h').read_text()
for path in paths():
    base=(ROOT/Path(path).with_suffix('.cg')).read_text()
    dest=ROOT/path.replace('.frag.gxp','_gt.frag.cg');text=dest.read_text()
    assert text==specialize(base)
    assert text[:base.index(START)]==base[:base.index(START)]
    assert text[text.index(END):]==base[base.index(END):]
    assert text.count('discard;')==base.count('discard;')==1
    assert 'if (!(saturate(out_a) > xv_atest.x)) discard;' in text
    a=(ROOT/path).read_bytes();b=dest.with_suffix('.gxp').read_bytes()
    u32=lambda b,o:struct.unpack_from('<I',b,o)[0]
    # The compiler pads the declared program size to a four-byte file boundary.
    assert b[:4]==b'GXP\0' and ((u32(b,8)+3)&~3)==len(b)
    assert not any(b[u32(b,8):])
    assert u32(a,0x14)==u32(b,0x14) # Same program capabilities, including discard.
    gxp=path.replace('.frag.gxp','_gt.frag.gxp')
    entry=re.search(r'\{"app0:'+re.escape(gxp)+r'", (xv_ps_bytes_\d+),',header);assert entry
    arr=re.search(r'static const uint8_t '+entry[1]+r'\[\] = \{(.*?)\};',header,re.S);assert arr
    assert bytes(int(x,16)for x in re.findall(r'0x([0-9a-f]{2})',arr[1]))==b
    sizes.append(dict(path=gxp,original_bytes=len(a),specialized_bytes=len(b)))
print('PASS: six exact GREATER source specializations; color/alpha arithmetic and GXP capability flags preserved; embedded bytes match')
print(json.dumps(sizes))
