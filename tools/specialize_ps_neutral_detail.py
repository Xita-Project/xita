#!/usr/bin/env python3
"""Remove a detail fetch only for a runtime-proven solid RGBA 0xff808080 upload."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]

def specialize(source):
    pattern = r'    (half4|float4) t1 = tex2D\(tex1, IN\.texcoord1\.xy\);'
    assert len(re.findall(pattern, source)) == 1
    assert source.count('uniform sampler2D tex1') == 1
    result = source.replace(', uniform sampler2D tex1', '')
    result = re.sub(pattern, lambda m: '    '+m[1]+' t1 = '+m[1]+'(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0, 1.0);', result)
    assert 'tex1' not in re.sub(r'//[^\n]*', '', result)
    return result

def paths():
    return sorted(set(re.findall(r'"app0:(shaders/ps_154066FD_[^\"]+\.gxp)"', (ROOT/'shaders/xv_ps_table.h').read_text())))

if __name__ == '__main__':
    for path in paths():
        for suffix in ('', '_na', '_gt'):
            source = ROOT/path.replace('.frag.gxp', suffix+'.frag.cg')
            target = ROOT/path.replace('.frag.gxp', '_nd'+suffix+'.frag.cg')
            target.write_text(specialize(source.read_text()))
    print('Generated 18 neutral-detail variants; remaining arithmetic and alpha tests unchanged.')
