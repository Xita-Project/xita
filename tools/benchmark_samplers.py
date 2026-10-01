#!/usr/bin/env python3
"""Compare one/four-slot production sampler caches in a synthetic host replay.

GPU API calls are descriptor-writing stubs. Results do not predict Vita FPS.
"""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATTERN = re.compile(r'slots=(\d+) materials=(\d+) binds=(\d+) preparations=(\d+) '
                     r'host_ns_per_bind=([\d.]+) checksum=(\d+)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=int, default=7)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    host = ROOT / 'recomp/host'
    binaries = {1: host / 'build/sampler_replay_single_test',
                4: host / 'build/sampler_replay_test'}
    subprocess.run(['make', '-C', str(host), *['build/' + p.name for p in binaries.values()]], check=True)
    samples = []
    for run in range(args.runs):
        for slots in ((1, 4) if run % 2 == 0 else (4, 1)):
            result = subprocess.run([str(binaries[slots])], check=True, text=True, capture_output=True)
            matches = PATTERN.findall(result.stdout)
            if len(matches) != 5:
                raise RuntimeError('missing replay measurements: ' + result.stdout)
            for capacity, materials, binds, preparations, elapsed, checksum in matches:
                samples.append(dict(run=run, slots=int(capacity), materials=int(materials),
                                    binds=int(binds), preparations=int(preparations),
                                    ns_per_bind=float(elapsed), checksum=int(checksum)))
    summary = []
    for materials in (1, 2, 4, 8, 16):
        selected = [s for s in samples if s['materials'] == materials]
        if len({s['checksum'] for s in selected}) != 1:
            raise RuntimeError('cached output changed')
        medians = [statistics.median(s['ns_per_bind'] for s in selected if s['slots'] == n) for n in (1, 4)]
        reduction = 100 * (1 - medians[1] / medians[0])
        row = dict(materials=materials, one_slot_ns=medians[0], four_slot_ns=medians[1],
                   host_time_reduction_percent=reduction)
        summary.append(row)
        print(f'{materials:2} materials: {medians[0]:6.2f} -> {medians[1]:6.2f} ns/bind; '
              f'{reduction:+.1f}% host time reduction')
    print('Synthetic CPU replay only. Overflow workloads can regress; no Vita FPS claim.')
    if args.json:
        args.json.write_text(json.dumps(dict(runs=args.runs, summary=summary, samples=samples), indent=2) + '\n')


if __name__ == '__main__':
    main()
