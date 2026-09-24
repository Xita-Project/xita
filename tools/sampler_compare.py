#!/usr/bin/env python3
"""tools/sampler_compare.py <base.samples> <base harness> <regs.samples> <regs harness> <from> <to> [--nm NM] [--addr2line A2L] [--top N]
Per-function XV_HOST_SAMPLE totals (1 ms ITIMER_PROF samples = CPU time) in frames (from, to], two builds side
by side (e.g. the memory lowering vs --x87-regs). For a static ARM harness pass the cross --nm/--addr2line."""
import sys, subprocess, collections
a = sys.argv[1:]
bs, bx, rs, rx, lo, hi = a[0], a[1], a[2], a[3], int(a[4]), int(a[5])
nm = a[a.index('--nm') + 1] if '--nm' in a else 'nm'
a2l = a[a.index('--addr2line') + 1] if '--addr2line' in a else 'addr2line'
top = int(a[a.index('--top') + 1]) if '--top' in a else 25
FOCUS = ['f_0004B9D0', 'f_00092330', 'f_00063C00', 'f_0004C980', 'f_000B8980', 'f_00061270', 'f_00019E7B',
         'f_0015CE90', 'f_0014B230', 'f_00090770', 'f_0008DDF0', 'f_00054010', 'f_0005B4A0', 'f_00053E90']


def base_of(exe):
    for line in subprocess.run([nm, exe], capture_output=True, text=True).stdout.splitlines():
        if line.endswith(' __executable_start'):
            v = int(line.split()[0], 16)
            return v if v > 0x1000 and 'arm' in nm else 0   # static ARM: link address; x86 PIE: 0
    return 0


def load(src, exe):
    base = base_of(exe)
    counts = [collections.Counter(), collections.Counter()]; cur = 0; windows = 0
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame':
            cur = int(f[1]); windows += lo < cur <= hi; continue
        if not (lo < cur <= hi): continue
        counts[int(f[0])][int(f[1], 16) + base] += int(f[2])
    offs = sorted(set(counts[0]) | set(counts[1]))
    out = subprocess.run([a2l, '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
    name = {o: out[2 * i] for i, o in enumerate(offs)}
    res = []
    for k in (0, 1):
        byfn = collections.Counter()
        for o, n in counts[k].items(): byfn[name.get(o, '?')] += n
        res.append(byfn)
    return res, windows


B, wb = load(bs, bx); R, wr = load(rs, rx)
print(f'frames ({lo}, {hi}]: windows {wb} / {wr}')
for k, label in ((0, 'OWNER (tick + serial scene)'), (1, 'OTHER threads (object workers, helpers)')):
    tb, tr = sum(B[k].values()), sum(R[k].values())
    print(f'\n{label}: total samples base {tb}  regs {tr}  ({100.0 * (tr - tb) / max(tb, 1):+.1f}%)')
    guest_b = sum(v for n, v in B[k].items() if n.startswith('f_')); guest_r = sum(v for n, v in R[k].items() if n.startswith('f_'))
    print(f'  guest code (f_*): base {guest_b}  regs {guest_r}  ({100.0 * (guest_r - guest_b) / max(guest_b, 1):+.1f}%)')
    x87b = sum(v for n, v in B[k].items() if n.startswith('x87_')); x87r = sum(v for n, v in R[k].items() if n.startswith('x87_'))
    print(f'  out-of-line x87_* helpers: base {x87b}  regs {x87r}')
    names = [n for n in FOCUS if B[k][n] or R[k][n]]
    for n, _ in (B[k] + R[k]).most_common(top):
        if n not in names: names.append(n)
    print(f'  {"function":24s} {"base":>7s} {"regs":>7s} {"change":>8s}')
    for n in names[:top + len(FOCUS)]:
        b, r = B[k][n], R[k][n]
        if b + r < 20: continue
        print(f'  {n:24s} {b:7d} {r:7d} {100.0 * (r - b) / max(b, 1):+7.1f}%')
