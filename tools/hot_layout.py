#!/usr/bin/env python3
"""Place profiled-hot generated guest functions contiguously (code layout only; no code change).

The Vita linker script places `*(SORT(.text.sorted.*))` before ordinary `.text`, so giving each hot
`f_XXXXXXXX` definition `__attribute__((section(".text.sorted.NNNNNN")))` packs the hot set densely in rank
order. Cold code stays where it was. Everything else about the function (body, callers, optimisation)
is untouched; the attribute is inserted only on the definition line.

usage:
  hot_layout.py apply <rank.json> <stage-recomp-dir> [--top N]
      rank.json: {"order": ["f_XXXXXXXX", ...], ...} hottest first (built from a sampled gameplay profile).
"""
import json, re, sys
from pathlib import Path

DEF = re.compile(r'^void (f_[0-9A-F]{8})\(xctx \*restrict c\)$', re.M)
ATTR = '__attribute__((section(".text.sorted.{:06d}")))\n'   # own line: build checks match '^void f_'


def apply(rank_path, recomp, top=None):
    ranks = json.loads(Path(rank_path).read_text())['order']
    if top:
        ranks = ranks[:top]
    # Profiles name hooked bodies f_X_body; their definition line is `void f_X(...)` under `#define f_X f_X_body`.
    index = {}
    for i, name in enumerate(ranks):
        index.setdefault(name[:-5] if name.endswith('_body') else name, i)
    placed = 0
    for shard in sorted(Path(recomp).glob('code_0*.c')):
        text = shard.read_text()
        if '.text.sorted.' in text:
            raise SystemExit(f'{shard} already has layout attributes')
        def sub(m):
            nonlocal placed
            name = m.group(1)
            if name not in index:
                return m.group(0)
            placed += 1
            return ATTR.format(index[name]) + m.group(0)
        new = DEF.sub(sub, text)
        if new != text:
            shard.write_text(new)
    print(f'placed {placed} of {len(ranks)} ranked functions')
    return placed


def main():
    a = sys.argv[1:]
    if not a or a[0] not in ('apply',):
        sys.exit(__doc__)
    top = int(a[a.index('--top') + 1]) if '--top' in a else None
    apply(a[1], a[2], top)


if __name__ == '__main__':
    main()
