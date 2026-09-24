#!/usr/bin/env python3
"""Compare two host/Pi harness runs made with XV_STATE_HASH (+ XV_TICK_TRACE) at equal game ticks.

  tools/state_hash_compare.py <runA prefix> <runB prefix> [--noise name,name,...]

<prefix>.state is the XV_STATE_HASH file, <prefix>.tick the XV_TICK_TRACE file (optional). Runs whose level
starts one tick apart are only comparable at equal ticks (the state at the last present of each tick); runs
with the same phase also frame by frame. Reports: equal ticks, ticks where the object physics hash differs,
per-array differences from the 60-frame lines (arrays in --noise are listed separately: by default the ones
that follow host real time, measured between two baseline runs, docs/x87-register-stack.md), and the tick trace.
"""
import os, re, sys

NOISE = {'xbox sound', 'particle systems', 'effect', 'particle', 'effect location', 'particle system particles',
         'lights', 'actor', 'prop', 'decals', 'decal vertex cache', 'sounds', 'looping sounds', 'object looping sounds',
         'xbox sound cache', 'xbox texture cache', 'xbox texture', 'cached object render states'}


def load_state(path):
    frames, arrays = {}, {}
    for line in open(path):
        if line.startswith('arrays '):
            fr, rest = line.rstrip('\n').split(' ', 2)[1:]
            arrays[int(fr)] = {m.group(1): m.group(2) for m in re.finditer(r'(?:^| )([^:]+?):([0-9a-f]{8})(?= |$)', rest)}
        else:
            f = line.split()
            if len(f) == 4:
                frames[int(f[0])] = (f[1], f[2], f[3])
    return frames, arrays


def per_tick(frames, arrays):
    d = {}
    for k in sorted(frames):
        d[frames[k][0]] = (frames[k][2], frames[k][1], arrays.get(k), k)
    return d


def ticks(path):
    return [l.split() for l in open(path) if l.strip()] if os.path.exists(path) else []


def main():
    a, b = sys.argv[1], sys.argv[2]
    noise = set(sys.argv[sys.argv.index('--noise') + 1].split(',')) if '--noise' in sys.argv else NOISE
    ta, tb = per_tick(*load_state(a + '.state')), per_tick(*load_state(b + '.state'))
    common = sorted(set(ta) & set(tb), key=lambda x: int(x, 16))
    od = [t for t in common if ta[t][0] != tb[t][0]]
    arr = {}
    for t in common:
        pa, pb = ta[t][2], tb[t][2]
        if pa is None or pb is None:
            continue
        for n in set(pa) & set(pb):
            if pa[n] != pb[n]:
                arr.setdefault(n, []).append(ta[t][3])
    real = {n: f'{len(v)} reports from frame {v[0]}' for n, v in arr.items() if n not in noise}
    print(f'{a} vs {b}: {len(common)} equal ticks; object physics differs at {len(od)} (first {od[:3]})')
    print(f'  arrays differing beyond the noise set: {real or "none"}')
    print(f'  noise arrays: { {n: len(v) for n, v in arr.items() if n in noise} }')
    A, B = ticks(a + '.tick'), ticks(b + '.tick')
    if A and B:
        n = min(len(A), len(B))
        same = sum(1 for i in range(n) if A[i] == B[i])
        byt = lambda t: {x[1]: x for x in t}
        TA, TB = byt(A), byt(B)
        ct = set(TA) & set(TB)
        dt = sorted(int(k, 16) for k in ct if TA[k][2:] != TB[k][2:])
        print(f'  tick trace: {same}/{n} frames identical; equal ticks {len(ct)}, differing {len(dt)} (first {dt[:3]})')
    return 1 if od or real else 0


if __name__ == '__main__':
    sys.exit(main())
