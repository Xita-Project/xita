#!/usr/bin/env python3
"""Specialize translated fragment sources: a failed alpha test writes zero instead of discarding.

PowerVR treats any program containing discard as punch-through: every covered fragment that passes depth is shaded
before visibility resolves, and the tile pipeline serializes on the feedback. The runtime selects these `_az`
variants only for draws where a zero source leaves the target unchanged (additive-style blends whose destination
factor is ONE, INV_SRC_ALPHA or INV_SRC_COLOR, or no color writes), with no depth write and a stencil pass op of
KEEP, so the result is identical to the discard. Color/alpha arithmetic and the test itself are unchanged.

  tools/specialize_ps_alphazero.py [shader dir]   (default: shaders/ next to this tool's repo root)
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
START = '    // ---- alpha test (NV097_SET_ALPHA_TEST_ENABLE/FUNC/REF):'
END = '    return saturate(float4(out_rgb, out_a));'
KILL = 'if (!pass_) discard;'
ZERO = 'if (!pass_) { out_rgb = float3(0.0, 0.0, 0.0); out_a = 0.0; }'

def specialize(source):
    if source.count(START) != 1 or source.count(END) != 1:
        raise ValueError('unrecognized generated alpha-test block')
    first, last = source.index(START), source.index(END)
    block = source[first:last]
    if first >= last or block.count('discard;') != 1 or KILL not in block:
        raise ValueError('unexpected alpha-test implementation')
    if 'discard;' in source[:first] or 'discard;' in source[last:]:   # CLIPPLANE texkill keeps punch-through: no gain
        raise ValueError('discard outside the alpha-test block')
    if not re.search(r'\bfloat3 out_rgb\b', source) or not re.search(r'\bfloat\s+out_a\b', source):
        raise ValueError('outputs are not plain locals')
    return source[:first] + block.replace(KILL, ZERO) + source[last:]

def main():
    shaders = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT/'shaders'
    table = (shaders/'xv_ps_table.h').read_text()
    names = sorted(set(re.findall(r'"app0:shaders/([^\"]+)\.frag\.gxp"', table)))
    made = skipped = 0
    for name in names:
        source = shaders/f'{name}.frag.cg'
        target = shaders/f'{name}_az.frag.cg'
        try: text = specialize(source.read_text())
        except (OSError, ValueError) as e:
            skipped += 1; print(f'skip {name}: {e}'); continue
        if not target.exists() or target.read_text() != text: target.write_text(text)
        made += 1
    print(f'Generated {made} alpha-zero variants ({skipped} skipped); original sources unchanged.')

if __name__ == '__main__': main()
