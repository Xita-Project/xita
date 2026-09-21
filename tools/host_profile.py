#!/usr/bin/env python3
"""Aggregate recomp/host/sampler.c output by guest function.

  tools/host_profile.py <samples.txt> <harness binary> [--from-frame N] [--top 30]

Lines "frame F samples S dropped D" start a window; "kind offset count" lines follow. Offsets are PIE-relative and
resolved with addr2line -f (function names f_XXXXXXXX for recompiled guest code, kernel/runtime names otherwise).
kind 0 = the harness main thread (owner: tick, and the scene too unless XV_SCENE_THREAD=1), 1 = any other thread
(scene helper, object workers). Percentages are of that kind's samples in the selected windows."""
import subprocess, sys, collections
def main():
    a = sys.argv[1:]; src, exe = a[0], a[1]; frm = 0; top = 30
    if '--from-frame' in a: frm = int(a[a.index('--from-frame') + 1])
    if '--top' in a: top = int(a[a.index('--top') + 1])
    counts = [collections.Counter(), collections.Counter()]; cur = 0; windows = 0
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame': cur = int(f[1]); windows += cur >= frm; continue
        if cur < frm: continue
        counts[int(f[0])][int(f[1], 16)] += int(f[2])
    offs = sorted(set(counts[0]) | set(counts[1]))
    out = subprocess.run(['addr2line', '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
    name = {o: out[2 * i] for i, o in enumerate(offs)}
    for kind, label in ((0, 'owner thread'), (1, 'other threads')):
        byfn = collections.Counter()
        for o, n in counts[kind].items(): byfn[name.get(o, '?')] += n
        total = sum(byfn.values()) or 1
        print(f'== {label}: {total} samples in {windows} windows (1 ms each)')
        for fn, n in byfn.most_common(top): print(f'  {100.0 * n / total:5.1f}%  {n:7d}  {fn}')
if __name__ == '__main__': main()
