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


def cadence(selected, period):
    """Group actual interval frame identifiers, preserving missing/zero samples.

    A high-cost phase is correlation with a cycle, not proof of its cause.
    These groups never remove intervals from the overall summary.
    """
    if period < 2:
        raise ValueError('cadence period must be at least 2 frames')
    phases = {}
    for end, values in selected:
        for offset, value in enumerate(values):
            phase = (end - len(values) + 1 + offset) % period
            phases.setdefault(phase, []).append(value)
    rows = []
    for phase, values in sorted(phases.items()):
        valid = [v for v in values if v]
        row = dict(phase=phase, samples=len(valid), unavailable=values.count(0))
        if valid:
            stats = summarize([(0, values)])
            row.update({k: stats[k] for k in ('mean_ms', 'p95_ms', 'max_ms',
                                             'over_100ms', 'over_200ms')})
        rows.append(row)
    return dict(period_frames=period,
                limits='Correlation only; all intervals remain in the overall summary. '
                       'Frame identifiers are inferred from each complete report end.',
                phases=rows)


SLOW_LINE = re.compile(r'\[frame-slow-us\] frame (\d+):([\d ]*)$')
SLOW_SEGMENTS = ('before_present', 'recorder_drain', 'capture_and_histogram',
                 'settings', 'end_frame', 'flip', 'flush', 'publish_acquire',
                 'begin_frame', 'ui_begin', 'texture_purge')


def slow_frames(text):
    """Adjacent elapsed segments; pre-present includes scheduling/reporting.

    These are wall times on the present caller, not exclusive CPU or GPU work.
    Fail closed on partial records or sums that do not cover the interval.
    """
    rows = []
    for line in text.splitlines():
        m = SLOW_LINE.search(line)
        if not m:
            if '[frame-slow-us]' in line:
                raise ValueError('malformed slow-frame record')
            continue
        values = [int(x) for x in m[2].split()]
        if len(values) != 12 or sum(values[1:]) != values[0]:
            raise ValueError('slow-frame segments do not partition the interval')
        rows.append(dict(frame=int(m[1]), total_us=values[0],
                         segments_us=dict(zip(SLOW_SEGMENTS, values[1:]))))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--from-frame', type=int, default=0)
    p.add_argument('--to-frame', type=int, default=2**32-1)
    p.add_argument('--cadence-period', type=int,
                   help='also group intervals by frame identifier modulo this period')
    p.add_argument('--slow-only', action='store_true',
                   help='show opt-in slow-frame partitions instead of aggregate windows')
    a = p.parse_args()
    text = a.log.read_text(errors='replace')
    if a.slow_only:
        rows = [r for r in slow_frames(text) if a.from_frame <= r['frame'] <= a.to_frame]
        print(json.dumps(rows, indent=2))
        return
    records, discarded = windows(text)
    selected = [(f, v) for f, v in records if a.from_frame <= f <= a.to_frame]
    result = summarize(selected)
    result['incomplete_windows_in_log'] = discarded
    result['window_end_frames'] = [f for f, _ in selected]
    if a.cadence_period is not None:
        if a.cadence_period < 2:
            p.error('--cadence-period must be at least 2')
        result['cadence'] = cadence(selected, a.cadence_period)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
