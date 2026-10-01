#!/usr/bin/env python3
"""Summarize native-mode collision counters from ordinary gameplay logs.

Elapsed query time includes interruptions, excludes budget handling and report
counter overhead, and is not exclusive CPU time or predicted FPS savings.
"""
import argparse
import json
import re
from pathlib import Path

ROW = re.compile(r'\[native-4b9d0\] (features )?(\d+) frames: calls (\d+) '
                 r'verified (\d+) mismatched (\d+) .*?; (.*?); us/call native ([0-9.]+)$')
COUNTER = re.compile(r'([a-z0-9-]+) (\d+)')


def summarize(text, windows=3):
    if windows < 1:
        raise ValueError('windows must be positive')
    rows = {'query': [], 'features': []}
    ready = False
    for line in text.splitlines(keepends=True):
        if not line.endswith('\n'):
            continue  # a collector may still be writing this report
        if '[native-4b9d0] f_00088110' in line and ': verify' in line:
            raise ValueError('Verification-mode log is not native performance evidence')
        if 'frame stats:' in line:
            ready = bool(re.search(r'loaded 1 active 1.*director on 1', line))
        match = ROW.search(line.rstrip())
        if not ready or not match:
            continue
        feature, frames, calls, verified, mismatched, counters, mean = match.groups()
        frames, calls = int(frames), int(calls)
        if int(verified) or int(mismatched):
            raise ValueError('Verification-mode/mismatch rows cannot establish native-mode timing')
        if not frames or not calls:
            continue
        rows['features' if feature else 'query'].append({
            'frames': frames, 'calls': calls, 'mean_us': float(mean),
            'counters': dict((k, int(v)) for k, v in COUNTER.findall(counters))})
    result = {}
    for kind, all_rows in rows.items():
        selected = all_rows[-windows:]
        if not selected:
            continue
        frames = sum(r['frames'] for r in selected)
        calls = sum(r['calls'] for r in selected)
        elapsed = sum(r['calls'] * r['mean_us'] for r in selected)
        counters = {k: sum(r['counters'].get(k, 0) for r in selected)
                    for k in set().union(*(r['counters'] for r in selected))}
        result[kind] = {
            'windows': len(selected), 'frames': frames, 'calls': calls,
            'calls_per_frame': calls / frames,
            'aggregate_native_elapsed_ms_per_frame': elapsed / frames / 1000,
            'weighted_us_per_call': elapsed / calls,
            'counts_per_frame': {k: v / frames for k, v in sorted(counters.items())},
            'elements_per_search': counters.get('scanned', 0) / counters['scans']
                if counters.get('scans') else None}
    if not result:
        raise ValueError('No complete timed native collision windows after gameplay readiness')
    result['note'] = ('Native mode only. Rounded per-call elapsed is aggregated; it includes '
                      'scheduling and excludes budget/counter work. Query and feature windows '
                      'may differ; do not infer total frame savings by adding them.')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--windows', type=int, default=3)
    args = parser.parse_args()
    print(json.dumps(summarize(args.log.read_text(errors='replace'), args.windows), indent=2))
