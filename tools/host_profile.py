#!/usr/bin/env python3
"""Aggregate recomp/host/sampler.c output by guest function.

  tools/host_profile.py <samples.txt> <harness binary> [--from-frame N] [--top 30] [--base 0x10000 | --nm <cross-nm>] [--addr2line <cross-addr2line>] [--inline-owners]

Lines "frame F samples S dropped D" start a window; "kind offset count" lines follow. Offsets are PIE-relative and
resolved with addr2line -f (function names f_XXXXXXXX for recompiled guest code, kernel/runtime names otherwise).
kind 0 = the harness main thread (owner: tick, and the scene too unless XV_SCENE_THREAD=1), 2 = the scene helper thread, 1 = any other
thread (object workers, capture/upload/texture workers; older samplers also counted the helper here). Percentages are of that kind's samples in the selected windows."""
import subprocess, sys, collections, re
def main():
    a = sys.argv[1:]
    if '--help' in a or '-h' in a:
        print(__doc__); return
    if len(a) < 2: sys.exit(__doc__)
    src, exe = a[0], a[1]; frm = 0; top = 30
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
    name = {}
    if offs:
        owners = '--inline-owners' in a
        # Resolve addresses on stdin to avoid the command-line length limit.
        out = subprocess.run([a2l, '-a', '-f', *(['-i'] if owners else []), '-e', exe],
                             input='\n'.join(hex(o) for o in offs) + '\n',
                             capture_output=True, text=True, check=True).stdout.splitlines()
        stacks = {}; current = None
        for line in out:
            if re.fullmatch(r'0x[0-9a-fA-F]+', line):
                current = int(line, 16); stacks[current] = []
            elif current is not None:
                stacks[current].append(line)
        if set(stacks) != set(offs) or any(not s or len(s) % 2 for s in stacks.values()):
            sys.exit('Unexpected addr2line output; no profile produced')
        name = {o: s[-2] if owners else s[0] for o, s in stacks.items()}
        if owners:
            print('Outermost DWARF inline scope at each sampled PC; not an inclusive call-stack profile.')
    for kind, label in ((0, 'owner thread'), (2, 'scene helper thread'), (1, 'other threads')):
        byfn = collections.Counter()
        for o, n in counts[kind].items(): byfn[name.get(o, '?')] += n
        if not byfn and kind == 2: continue
        total = sum(byfn.values()) or 1
        print(f'== {label}: {total} samples in {windows} windows (1 ms each)')
        for fn, n in byfn.most_common(top): print(f'  {100.0 * n / total:5.1f}%  {n:7d}  {fn}')
if __name__ == '__main__': main()
