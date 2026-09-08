#!/usr/bin/env python3
"""Specialize existing translated fragment sources for alpha testing disabled.

Preserve all color/alpha arithmetic, texture modes and texture CLIPPLANE tests.
Only the runtime alpha-test block and its now-unused uniform are removed.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
START = '    // ---- alpha test (NV097_SET_ALPHA_TEST_ENABLE/FUNC/REF):'
END = '    return saturate(float4(out_rgb, out_a));'

def specialize(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError('unrecognized generated alpha-test block')
    first, last = source.index(START), source.index(END)
    block = source[first:last]
    if first >= last or block.count('discard;') != 1 or 'if (!pass_) discard;' not in block:
        raise ValueError('unexpected alpha-test implementation')
    uniform = ', uniform float4 xv_atest'
    if source.count(uniform) != 1:
        raise ValueError('unexpected alpha-test uniform')
    return (source[:first] + source[last:]).replace(uniform, '')

def main():
    paths = sorted(set(re.findall(r'"app0:(shaders/[^\"]+\.gxp)"',
                                 (ROOT/'shaders/xv_ps_table.h').read_text())))
    for path in paths:
        source = ROOT/Path(path).with_suffix('.cg')
        target = ROOT/path.replace('.frag.gxp', '_na.frag.cg')
        text = specialize(source.read_text())
        if not target.exists() or target.read_text() != text: target.write_text(text)
    print(f'Generated {len(paths)} alpha-disabled variants; original sources unchanged.')

if __name__ == '__main__': main()
