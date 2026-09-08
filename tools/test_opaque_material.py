#!/usr/bin/env python3
"""Guard the exact shader dependency used by the runtime opacity proof."""
from pathlib import Path
import re
from specialize_ps_alpha import specialize

root = Path(__file__).resolve().parents[1]
paths = set()
for line in (root / 'shaders/xv_ps_table.h').read_text().splitlines():
    if re.search(r'0x154066FDu\s*}', line):
        match = re.search(r'"app0:([^\"]+)"', line)
        if match:
            paths.add(match[1])
assert len(paths) == 6, paths
for path in sorted(paths):
    source = (root / Path(path).with_suffix('.cg')).read_text()
    variant = (root / path.replace('.frag.gxp', '_na.frag.cg')).read_text()
    assert specialize(source) == variant, path
    for text in (source, variant):
        code = re.sub(r'//[^\n]*', '', text)
        assert re.search(r'float\s+out_a\s*=\s*saturate\(t0\.a\);', code), path
        assert re.findall(r'\bt0(?:\.([rgba]+))?\s*=', code) == ['', 'rgb'], path
        assert 'float4 t0 = tex2D(tex0, IN.texcoord0.xy);' in code, path
        assert 'uniform sampler2D tex0' in code, path
    assert source.count('discard;') == 1 and 'discard' not in re.sub(r'//[^\n]*', '', variant), path
print('PASS: six material programs and six alpha-disabled variants preserve tex0 alpha and all other shader arithmetic')
