#!/usr/bin/env python3
"""Summarize sampled typed-query elapsed time, not CPU self time or FPS gains."""
import argparse
import json
import re
from pathlib import Path

STAGES = ('capture', 'compute', 'reacquire', 'validate', 'publish')
ROW = re.compile(
    r'\[typed-query-cost\] lane (\d+) samples (\d+) '
    r'outcomes decline/bypass/applied (\d+)/(\d+)/(\d+) '
    r'us capture/compute/reacquire/validate/publish '
    r'(\d+)/(\d+)/(\d+)/(\d+)/(\d+);')


def summarize(text, windows):
    complete, pending = [], None
    for match in ROW.finditer(text):
        row = list(map(int, match.groups()))
        lane, samples, declined, bypassed, applied, *times = row
        if samples != declined + bypassed + applied:
            raise ValueError('Sample/outcome count mismatch')
        if not samples and any(times):
            raise ValueError('Elapsed time without samples')
        if lane == 0:
            pending = row
        elif lane == 1 and pending is not None:
            if pending[1] + samples:
                complete.append((pending, row))
            pending = None
        else:
            pending = None
    selected = complete[-windows:]
    if not selected:
        raise ValueError('No complete nonempty two-lane sample windows')
    rows = [row for pair in selected for row in pair]
    samples = sum(row[1] for row in rows)
    totals = [sum(row[5+i] for row in rows) for i in range(5)]
    elapsed = sum(totals)
    return {
        'windows': len(selected), 'samples': samples,
        'outcomes': dict(zip(('declined', 'bypassed', 'applied'),
                             (sum(row[i] for row in rows) for i in (2, 3, 4)))),
        'stages': {name: {'sampled_us': total,
                          'us_per_sample': round(total / samples, 2),
                          'share_percent': round(100 * total / elapsed, 2) if elapsed else 0}
                   for name, total in zip(STAGES, totals)},
        'note': 'One in 64 admitted calls; elapsed includes scheduling. Lane sums overlap. '
                'Excludes pre-admission checks and the original list-publication tail. '
                'Not CPU self time, frame time, or predicted FPS savings.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--windows', type=int, default=3)
    args = parser.parse_args()
    if args.windows < 1:
        parser.error('--windows must be positive')
    print(json.dumps(summarize(args.log.read_text(errors='replace'), args.windows), indent=2))


if __name__ == '__main__':
    main()
