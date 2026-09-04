#!/usr/bin/env python3
"""Prove capture-group source equivalence with the unchanged parser/generator.

Compare every member and its canonical bytes, using a fixed name (the only
normalization), for all 128 possible vertex-output masks and unrestricted input.
Abort on any source difference or FNV collision; list the offending group.
"""
from collections import defaultdict
from dataclasses import asdict
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from dx8_pixelshader_parse import decode_psdef
import pixelshader_recomp_gen as gen
from psdef_hash import canonicalize, canonical_hash


def verify(directory):
    groups = defaultdict(list)
    for path in sorted(directory.glob('*.bin')):
        groups[canonicalize(path.read_bytes())].append(path)
    hashes = {}
    for data, paths in groups.items():
        h = canonical_hash(data)
        if h in hashes and hashes[h] != data:
            raise ValueError(f'FNV collision {h:08X}')
        hashes[h] = data
        defs = [asdict(decode_psdef(p.read_bytes(), 0)) for p in paths]
        canonical = asdict(decode_psdef(data, 0))
        for mask in [None, *range(128)]:
            gen.VARYINGS_AVAILABLE = (None if mask is None else
                {v[0] for i, v in enumerate(gen.VARYINGS) if mask & (1 << i)})
            expected = gen.generate(canonical, 'equivalence', False)[0]
            for path, d in zip(paths, defs):
                if gen.generate(d, 'equivalence', False)[0] != expected:
                    raise ValueError(f'source differs: group {h:08X}, member {path.stem}, mask {mask}; '
                                     f'members: {[p.stem for p in paths]}')
    gen.VARYINGS_AVAILABLE = None
    print(f'{sum(map(len, groups.values()))} captures, {len(groups)} groups: '
          '0 differing groups (129 varying configurations, including canonical copies)')


if __name__ == '__main__':
    verify(ROOT / 'shaders/psdefs')
