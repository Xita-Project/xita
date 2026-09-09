#!/usr/bin/env python3
"""Audit actual vertex references in explicit mesh captures, without changing draws.

Only non-immediate stream 0 is captured. Byte totals describe requested spans
and a cache replay, not physical memory traffic, timing, or predicted FPS.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import struct

from analyze_index_ranges import analyze

HEADER = struct.Struct('<8I')
PREFIX = HEADER.size + 192 * 16 + 18 * 16


def read_mesh(path):
    data = path.read_bytes()
    if len(data) < PREFIX:
        raise ValueError(f'{path}: truncated header/constants')
    magic, vs, ps, vertices, stride, count, primitive, alpha = HEADER.unpack_from(data)
    if magic != 0x4853454D or not 0 < vertices <= 65536 or not stride or not count:
        raise ValueError(f'{path}: invalid mesh header')
    end = PREFIX + vertices * stride
    if len(data) != end + count * 2:
        raise ValueError(f'{path}: inconsistent payload length')
    indices = [v[0] for v in struct.iter_unpack('<H', data[end:])]
    if max(indices) >= vertices:
        raise ValueError(f'{path}: out-of-range index')
    unique = sorted(set(indices))
    runs = []
    for index in unique:
        if runs and runs[-1][1] == index:
            runs[-1][1] += 1
        else:
            runs.append([index, index + 1])
    return dict(vs=vs, ps=ps, vertices=vertices, stride=stride, count=count,
                unique=len(unique), runs=runs, data=data[PREFIX:end])


def reference_runs(draw, group):
    covered = set()
    for a, b in draw['runs']:
        covered.update(range(a // group, (b - 1) // group + 1))
    runs = []
    for block in sorted(covered):
        a, b = block * group, min((block + 1) * group, draw['vertices'])
        if runs and runs[-1][1] == a:
            runs[-1][1] = b
        else:
            runs.append([a, b])
    return runs


def replay(draws, sparse, group=1):
    cache = defaultdict(list)
    result = dict(copies=0, copied_bytes=0, hits=0, comparison_calls=0,
                  compared_span_bytes=0, equal_span_bytes=0, comparison_runs=0)
    for draw in draws:
        payload, stride = draw['data'], draw['stride']
        # Use the runtime's minimum size and conservative group-count threshold.
        runs = reference_runs(draw, group) if sparse else []
        groups = sum((b - a + group - 1) // group for a, b in runs)
        use_sparse = sparse and draw['vertices'] >= 512 and groups * group * 2 < draw['vertices']
        ranges = [(a * stride, b * stride) for a, b in runs] if use_sparse else [(0, len(payload))]
        hit = False
        for previous in cache[draw['source']]:
            if len(previous) < len(payload):
                continue
            result['comparison_calls'] += 1
            equal = True
            for start, end in ranges:
                result['comparison_runs'] += 1
                result['compared_span_bytes'] += end - start
                if previous[start:end] != payload[start:end]:
                    equal = False
                    break
                result['equal_span_bytes'] += end - start
            if equal:
                hit = True
                # Independently verify every indexed record, including repeats.
                assert all(previous[a*stride:b*stride] == payload[a*stride:b*stride]
                           for a, b in draw['runs'])
                break
        if hit:
            result['hits'] += 1
        else:
            cache[draw['source']].insert(0, payload)
            result['copies'] += 1
            result['copied_bytes'] += len(payload)
    return result


def audit(log, directory):
    frames = []
    for frame in analyze(log)['frames']:
        draws = []
        missing = 0
        for observed in frame['observations']:
            streams = [s for s in observed['streams'] if s['stream'] == 0]
            if observed['immediate'] or not streams:
                continue
            path = directory / f"mesh_{frame['frame']}_{observed['cmd']}.bin"
            if not path.exists():
                missing += 1
                continue
            draw = read_mesh(path)
            stream = streams[0]
            assert draw['vertices'] == observed['retained'] == stream['vertices']
            assert draw['stride'] == stream['stride'] and draw['count'] == observed['count']
            draw['source'] = stream['data'] + observed['base'] * stream['stride']
            draws.append(draw)
        if not draws:
            continue
        frames.append(dict(frame=frame['frame'], captured_draws=len(draws), missing_meshes=missing,
            requested_bytes=sum(len(d['data']) for d in draws),
            referenced_bytes=sum(d['unique'] * d['stride'] for d in draws),
            sparse_draws=sum(d['unique'] * 2 < d['vertices'] for d in draws),
            full_cache=replay(draws, False), indexed_cache=replay(draws, True),
            grouped_caches={str(g): replay(draws, True, g) for g in (4, 8, 16, 32)}))
    if not frames:
        raise ValueError('No matching non-immediate stream-0 captures')
    return dict(scope=__doc__.strip(), frames=frames)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('meshes', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.log.read_text(errors='replace'), args.meshes)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
