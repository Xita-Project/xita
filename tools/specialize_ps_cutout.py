#!/usr/bin/env python3
"""Keep material alpha cutouts, specializing only the captured GREATER function."""
from pathlib import Path
import re
from specialize_ps_alpha import START, END
ROOT=Path(__file__).resolve().parents[1]
def specialize(source):
    assert source.count(START)==source.count(END)==1
    start,end=source.index(START),source.index(END)
    assert source[start:end].count('discard;')==1
    assert 'if (!pass_) discard;' in source[start:end]
    # Negated comparison preserves the original rejection of NaN alpha.
    return source[:start]+'''    // Selected only for captured alpha testing enabled with function GREATER.
    if (!(saturate(out_a) > xv_atest.x)) discard;
'''+source[end:]
def paths():
    return sorted(set(re.findall(r'"app0:(shaders/ps_154066FD_[^\"]+\.gxp)"',(ROOT/'shaders/xv_ps_table.h').read_text())))
if __name__=='__main__':
    for p in paths():
        target=ROOT/p.replace('.frag.gxp','_gt.frag.cg');text=specialize((ROOT/Path(p).with_suffix('.cg')).read_text())
        if not target.exists() or target.read_text()!=text:target.write_text(text)
    print('Generated',len(paths()),'GREATER cutout variants; other shader arithmetic unchanged.')
