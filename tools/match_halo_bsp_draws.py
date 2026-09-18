#!/usr/bin/env python3
"""Match a draw trace to an explicitly identified owned map/BSP.

Matches establish source-range provenance only. Requested spans are not copied
bytes, compared bytes, CPU/GPU time or a proof that the source is immutable.
Trace instrumentation makes these frames unsuitable for gameplay timing.
Unmatched streams remain unclassified, not 'dynamic'.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

from analyze_index_ranges import analyze
from audit_halo_bsp_geometry import audit


def match_frame(bsp, frame):
    by_resource = {}
    for material in bsp['materials']:
        for stream in material['streams']:
            resource = stream['resource']
            if resource in by_resource:
                raise ValueError('Ambiguous BSP resource inventory')
            by_resource[resource] = stream
    counts = Counter()
    spans = {}
    unmatched = {}
    matched_draws = set()
    for draw in frame['observations']:
        for stream in draw['streams']:
            size = stream['vertices'] * stream['stride']
            start = draw['base'] * stream['stride']
            expected = by_resource.get(stream['vb'])
            admitted = (expected is not None and
                stream['data'] == expected['physical_data'] and
                stream['stride'] == expected['stride'] and
                0 <= start <= start + size <= expected['bytes'])
            counts['all_streams'] += 1
            counts['all_requested_bytes'] += size
            if not admitted:
                key = (stream['vb'], stream['data'], stream['stride'])
                item = unmatched.setdefault(key, dict(resource=key[0], data=key[1], stride=key[2],
                    requests=0, requested_bytes=0, known_header=expected is not None))
                item['requests'] += 1
                item['requested_bytes'] += size
                continue
            kind = expected['kind']
            counts[kind + '_streams'] += 1
            counts[kind + '_requested_bytes'] += size
            matched_draws.add(draw['cmd'])
            spans.setdefault(stream['vb'], []).append((start, start + size))
    unique_bytes = 0
    for ranges in spans.values():
        end = 0
        for lo, hi in sorted(ranges):
            unique_bytes += max(0, hi - max(lo, end))
            end = max(end, hi)
    return dict(frame=frame['frame'], traced_draws=frame['draws'],
        bsp_matched_draws=len(matched_draws), bsp_matched_resources=len(spans),
        bsp_unique_requested_span_bytes=unique_bytes, counts=dict(counts),
        unmatched=sorted(unmatched.values(), key=lambda row: -row['requested_bytes']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', type=Path)
    parser.add_argument('log', type=Path)
    parser.add_argument('--bsp', type=int, help='Known active BSP index; required for multi-BSP maps')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    inventory = audit(args.map)
    if args.bsp is None and len(inventory['bsps']) != 1:
        parser.error('Specify the known active --bsp; address reuse prevents guessing')
    index = 0 if args.bsp is None else args.bsp
    candidates = [b for b in inventory['bsps'] if b['index'] == index]
    if len(candidates) != 1:
        parser.error('BSP index not present in this map')
    frames = [match_frame(candidates[0], frame)
              for frame in analyze(args.log.read_text(errors='replace'))['frames']]
    result = dict(scope=__doc__.strip(), map=inventory['map'], map_sha256=inventory['sha256'],
                  bsp=index, frames=frames)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    for frame in frames:
        print(json.dumps({k: v for k, v in frame.items() if k != 'unmatched'}))


if __name__ == '__main__':
    main()
