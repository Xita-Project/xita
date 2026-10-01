#!/usr/bin/env python3
"""All generated variants must preserve every instruction outside alpha test."""
import re
from pathlib import Path
from specialize_ps_alpha import ROOT, START, END, specialize

paths = sorted(set(re.findall(r'"app0:(shaders/[^\"]+\.gxp)"',
                             (ROOT/'shaders/xv_ps_table.h').read_text())))
clips = 0
for path in paths:
    base = (ROOT/Path(path).with_suffix('.cg')).read_text()
    variant = (ROOT/path.replace('.frag.gxp','_na.frag.cg')).read_text()
    assert specialize(base) == variant, path
    before, tail = base[:base.index(START)], base[base.index(END):]
    assert variant == before.replace(', uniform float4 xv_atest','') + tail
    # CLIPPLANE discards implement texture modes, independently of alpha test.
    clip = [line for line in base.splitlines() if 'CLIPPLANE t' in line]
    assert all(line in variant for line in clip); clips += len(clip)
    executable = re.sub(r'//[^\n]*','',variant)
    assert 'xv_atest' not in executable
    assert executable.count('discard;') == len(clip)
for bad in ('', 'uniform float4 xv_atest', START + END):
    try: specialize(bad)
    except ValueError: pass
    else: raise AssertionError('unrecognized shader was accepted')
synthetic = base.replace(START, '    if (t0.x < 0) discard; // CLIPPLANE t0\n' + START)
assert 'if (t0.x < 0) discard; // CLIPPLANE t0' in specialize(synthetic)
print(f'PASS: {len(paths)} exact alpha-disabled specializations; {clips} texture clip tests preserved.')
