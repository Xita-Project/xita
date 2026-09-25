#!/usr/bin/env python3
"""Summarize XV_FRAME_TIMES logs; select scene-qualified complete report windows.

Intervals measure CPU Present-to-Present, not GPU completion or physical scanout.
--from-frame/--to-frame select by the report's end-frame identifier.
"""
import argparse
import json
import math
import re
from pathlib import Path

LINE = re.compile(r'\[frame-us\] end (\d+) offset (\d+) count (\d+):([\d ]*)$')


def windows(text):
    pending = {}
    end = None
    complete = []
    discarded = 0
    for line in text.splitlines():
        m = LINE.search(line)
        if not m:
            continue
        frame, offset, count = map(int, m.group(1, 2, 3))
        values = [int(x) for x in m[4].split()]
        if end != frame or offset == 0:
            discarded += bool(pending)
            pending = {}
            end = frame
        if offset not in (0, 20, 40) or count != 20 or len(values) != count:
            raise ValueError(f'malformed frame timing chunk ending at {frame}')
        if offset in pending:
            raise ValueError(f'duplicate timing chunk ending at {frame}')
        pending[offset] = values
        if len(pending) == 3:
            complete.append((frame, sum((pending[i] for i in (0, 20, 40)), [])))
            pending = {}
    return complete, discarded + bool(pending)


def summarize(selected):
    raw = [v for _, values in selected for v in values]
    values = sorted(v for v in raw if v)
    if not values:
        raise ValueError('no valid intervals in selected complete windows')
    def percentile(p):
        return values[math.ceil(len(values) * p) - 1] / 1000
    mean = sum(values) / len(values)
    return dict(windows=len(selected), samples=len(values), unavailable=raw.count(0),
                mean_ms=mean/1000, fps=1e6/mean, p50_ms=percentile(.5),
                p95_ms=percentile(.95), p99_ms=percentile(.99), max_ms=values[-1]/1000,
                over_50ms=sum(v > 50000 for v in values),
                over_100ms=sum(v > 100000 for v in values),
                over_200ms=sum(v > 200000 for v in values))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--from-frame', type=int, default=0)
    p.add_argument('--to-frame', type=int, default=2**32-1)
    a = p.parse_args()
    records, discarded = windows(a.log.read_text(errors='replace'))
    selected = [(f, v) for f, v in records if a.from_frame <= f <= a.to_frame]
    result = summarize(selected)
    result['incomplete_windows_in_log'] = discarded
    result['window_end_frames'] = [f for f, _ in selected]
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
