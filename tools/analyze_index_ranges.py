#!/usr/bin/env python3
"""Summarize full index-range observations from explicitly captured draw frames.

Byte totals are requests to the uploader, before cache reuse. They are not
measured copies, saved time, or a prediction of FPS. Immediate draws without a
logged source stream are excluded from byte totals.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

RANGE = re.compile(r'\[index-range\] frame (\d+) cmd (\d+) prim (\d+) count (\d+) '
                   r'min (\d+) max (\d+) retained (\d+) base (\d+) immediate (\d+)')
STREAM = re.compile(r'\[hist\] stream (\d+) vb ([0-9a-fA-F]+).*? '
                    r'data ([0-9a-fA-F]+) vertices (\d+) stride (\d+)')
KEYS = ('frame', 'cmd', 'prim', 'count', 'min', 'max', 'retained', 'base', 'immediate')


def analyze(text):
    frames = defaultdict(list)
    current = None
    for line in text.splitlines():
        match = RANGE.search(line)
        if match:
            current = dict(zip(KEYS, map(int, match.groups())))
            assert current['count'] and 0 <= current['min'] <= current['max'] < 65536
            assert current['retained'] == current['max'] + 1
            current['streams'] = []
            frames[current['frame']].append(current)
        match = STREAM.search(line)
        if match and current:
            stream = dict(stream=int(match[1]), vb=int(match[2], 16), data=int(match[3], 16),
                          vertices=int(match[4]), stride=int(match[5]))
            assert stream['vertices'] == current['retained']
            assert stream['stride'] > 0
            assert all(s['stream'] != stream['stream'] for s in current['streams'])
            current['streams'].append(stream)
    result = []
    for frame, draws in frames.items():
        total = sum(d['retained'] * s['stride'] for d in draws for s in d['streams'])
        prefix = sum(d['min'] * s['stride'] for d in draws for s in d['streams'])
        by_source = defaultdict(list)
        for d in draws:
            for s in d['streams']:
                by_source[(s['data'], d['base'], s['stride'])].append(d)
        # Identical addresses can be rewritten between draws. Count requests
        # sharing addresses to flag that dependency, without assuming cache hits.
        repeated = sum(len(group) for group in by_source.values() if len(group) > 1)
        result.append(dict(frame=frame, draws=len(draws), zero_minimum_draws=sum(d['min'] == 0 for d in draws),
                           streamed_draws=sum(bool(d['streams']) for d in draws),
                           requested_vertex_bytes=total, unreferenced_prefix_bytes=prefix,
                           prefix_percent=100 * prefix / total if total else 0,
                           requests_with_repeated_source_address=repeated,
                           nonzero_base_draws=sum(d['base'] != 0 for d in draws),
                           primitives=dict(Counter(d['prim'] for d in draws)),
                           observations=draws))
    assert result, 'No complete [index-range] records found'
    return {'scope': __doc__.strip(), 'frames': result}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = analyze(args.log.read_text(errors='replace'))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    for frame in result['frames']:
        print(json.dumps({k: v for k, v in frame.items() if k != 'observations'}))
