#!/usr/bin/env python3
"""Aggregate recomp/host/sampler.c output by guest function.

  tools/host_profile.py <samples.txt> <harness binary> [--from-frame N] [--top 30] [--base 0x10000] [--nm <cross-nm>]

Lines "frame F samples S dropped D" start a window; "kind offset count" lines follow. Offsets are PIE-relative and
resolved with addr2line -f (function names f_XXXXXXXX for recompiled guest code, kernel/runtime names otherwise).
kind 0 = the harness main thread (owner: tick, and the scene too unless XV_SCENE_THREAD=1), 1 = any other thread
(scene helper, object workers). Percentages are of that kind's samples in the selected windows."""
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
    counts = [collections.Counter(), collections.Counter()]; cur = 0; windows = 0
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame': cur = int(f[1]); windows += cur >= frm; continue
        if cur < frm: continue
        counts[int(f[0])][int(f[1], 16) + base] += int(f[2])
    offs = sorted(set(counts[0]) | set(counts[1]))
    a2l = a[a.index('--nm') + 1][:-2] + 'addr2line' if '--nm' in a and a[a.index('--nm') + 1].endswith('nm') else 'addr2line'   # the cross tool for an ARM binary
    out = subprocess.run([a2l, '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
    name = {o: out[2 * i] for i, o in enumerate(offs)}
    # addr2line only uses the symbol table when the binary has no DWARF at all; a mixed build (runtime -g, guest
    # code -g0, as tools/h2_host_build.py makes) leaves the guest functions as '??'. Resolve those from nm.
    if any(v == '??' for v in name.values()):
        import bisect
        nm = a[a.index('--nm') + 1] if '--nm' in a else 'nm'
        syms = []
        for line in subprocess.run([nm, '-n', '-S', '--defined-only', exe], capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if len(f) == 4 and f[2] in 'tTwW': syms.append((int(f[0], 16), int(f[1], 16), f[3]))
        keys = [k for k, _, _ in syms]
        for o, v in name.items():
            if v == '??' and syms:
                i = bisect.bisect_right(keys, o) - 1
                # a PC past every sized symbol is outside the executable (shared libc, vdso) in a dynamic build
                if i >= 0: name[o] = syms[i][2] if o < syms[i][0] + max(syms[i][1], 1) else '[outside the binary: libc/vdso]'
    for kind, label in ((0, 'owner thread'), (1, 'other threads')):
        byfn = collections.Counter()
        for o, n in counts[kind].items(): byfn[name.get(o, '?')] += n
        total = sum(byfn.values()) or 1
        print(f'== {label}: {total} samples in {windows} windows (1 ms each)')
        for fn, n in byfn.most_common(top): print(f'  {100.0 * n / total:5.1f}%  {n:7d}  {fn}')
if __name__ == '__main__': main()
