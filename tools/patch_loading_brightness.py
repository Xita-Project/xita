#!/usr/bin/env python3
"""Apply the profile guard to a retained CE generated shard, without regeneration."""
from pathlib import Path
import argparse
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from games.halo_ce_3925.loading_brightness import LINES, MARKER


def patch(source):
    start = source.index('void f_000D4C40(xctx *restrict c)')
    end = source.find('\nvoid f_', start+1)
    if end < 0: end=len(source)
    body=source[start:end]
    target='    /* 000D4D09  call 0001D370h */'
    count=body.count(target)
    if count != 2:
        raise ValueError(f'expected two retained loading call sites, found {count}')
    replacement='\n'.join(LINES)+'\n'+target
    if MARKER in body:
        if body.count(replacement)!=count or body.count(MARKER)!=count:
            raise ValueError('partial or altered loading guard')
        return source
    return source[:start]+body.replace(target,replacement)+source[end:]

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('shard',type=Path);args=ap.parse_args()
    old=args.shard.read_text();new=patch(old)
    if new!=old:args.shard.write_text(new)
    print('loading brightness guard: '+('applied' if new!=old else 'already present'))
