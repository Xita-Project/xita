#!/usr/bin/env python3
"""Aggregate recomp/host/sampler.c output by guest function.

  tools/host_profile.py <samples.txt> <harness binary> [--from-frame N] [--top 30] [--base 0x10000 | --nm <cross-nm>] [--addr2line <cross-addr2line>]

Lines "frame F samples S dropped D" start a window; "kind offset count" lines follow. Offsets are PIE-relative and
resolved with addr2line -f (function names f_XXXXXXXX for recompiled guest code, kernel/runtime names otherwise).
kind 0 = the harness main thread (owner: tick, and the scene too unless XV_SCENE_THREAD=1), 2 = the scene helper thread, 1 = any other
thread (object workers, capture/upload/texture workers; older samplers also counted the helper here). Percentages are of that kind's samples in the selected windows."""
import subprocess, sys, collections
def main():
    a = sys.argv[1:]; src, exe = a[0], a[1]; frm = 0; top = 30
    # The sampler records pc - __executable_start. For a PIE that is the file offset addr2line wants; for a static
    # (non-PIE) binary __executable_start is the link address (0x10000 on ARM), so the offsets must be shifted back,
    # or every symbol resolves 64 KiB away (the Pi profile of 2026-09-23 named f_000B8980 at 66 % that way).
    base = 0
    if '--base' in a: base = int(a[a.index('--base') + 1], 0)
    else:
        nm = a[a.index('--nm') + 1] if '--nm' in a else 'nm'
        try:
            for line in subprocess.run([nm, exe], capture_output=True, text=True).stdout.splitlines():
                if line.endswith(' __executable_start'): base = int(line.split()[0], 16); break
        except OSError: pass
        if base: print(f'static binary: adding load base {base:#x} to every sampled offset (pass --base 0 for a PIE)')
    if '--from-frame' in a: frm = int(a[a.index('--from-frame') + 1])
    if '--top' in a: top = int(a[a.index('--top') + 1])
    counts = [collections.Counter(), collections.Counter(), collections.Counter()]; cur = 0; windows = 0
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame': cur = int(f[1]); windows += cur >= frm; continue
        if cur < frm: continue
        counts[int(f[0])][int(f[1], 16) + base] += int(f[2])
    offs = sorted(set(counts[0]) | set(counts[1]) | set(counts[2]))
    a2l = a[a.index('--addr2line') + 1] if '--addr2line' in a else 'addr2line'
    out = subprocess.run([a2l, '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
    name = {o: out[2 * i] for i, o in enumerate(offs)}
    for kind, label in ((0, 'owner thread'), (2, 'scene helper thread'), (1, 'other threads')):
        byfn = collections.Counter()
        for o, n in counts[kind].items(): byfn[name.get(o, '?')] += n
        if not byfn and kind == 2: continue
        total = sum(byfn.values()) or 1
        print(f'== {label}: {total} samples in {windows} windows (1 ms each)')
        for fn, n in byfn.most_common(top): print(f'  {100.0 * n / total:5.1f}%  {n:7d}  {fn}')
if __name__ == '__main__': main()
