#!/usr/bin/env python3
"""tools/rec_ab.py - split one XV_REC_AB run (runtime/xv_record_opt.h) into its two phases and compare them.

  tools/rec_ab.py <log> [--from F] [--to T] [--samples S --exe E [--nm NM --addr2line A2L] [--top N]]

XV_REC_AB=<frames> alternates every recording knob between the original path (phase 0) and the new path (phase 1)
every <frames> frames. Each 60-frame window is classified by its [host-perf] "ab a b" frame counts (a window with
at most 3 frames of the other phase belongs to a phase; straddling windows are dropped). Neighbouring windows of
opposite phases are paired, so slow drift (thermal, other work on the machine) cancels; the table gives the mean
per-window difference and its standard error for:
  - scene-helper Mcycles, Minstr and CPU ms per frame ([host-perf], XV_HOST_PERF=1)
  - every [draw-prep] stage and sub-stage, every [hle-time] entry and [flare-work] recording (ms per frame)
With --samples (XV_HOST_SAMPLE + XV_HOST_PERF_SAMPLE) it also prints per-symbol scene-helper Mcycles per frame in
each phase (largest differences first)."""
import collections, math, re, subprocess, sys


def arg(a, name, default=None, conv=str):
    return conv(a[a.index(name) + 1]) if name in a else default


def windows_from_log(path, lo, hi):
    """[(frame, phase, {metric: value})] for 60-frame windows ending in (lo, hi]."""
    out = []; cur = None; metrics = {}; phase = None
    def flush():
        if cur is not None and phase is not None and lo < cur <= hi: out.append((cur, phase, dict(metrics)))
    for line in open(path, errors='replace'):
        m = re.search(r'\[host\] report at frame (\d+)', line)
        if m:
            flush(); cur = int(m.group(1)); metrics.clear(); phase = None; continue
        m = re.search(r'\[host-perf\] frame \d+ scene-helper: ([\d.]+) Mcycles ([\d.]+) Minstr ([\d.]+) ms CPU per frame \(\d+ frames\) ab (\d+) (\d+)', line)
        if m:
            a, b = int(m.group(4)), int(m.group(5))
            phase = 0 if b <= 3 and a > 3 else 1 if a <= 3 and b > 3 else None
            metrics['helper Mcycles'] = float(m.group(1)); metrics['helper Minstr'] = float(m.group(2)); metrics['helper CPU ms'] = float(m.group(3))
            continue
        m = re.search(r'\[draw-prep\] \d+ frames \d+ draws: (.*) ms/frame', line)
        if m:
            for k, v in re.findall(r'(\w+) ([\d.]+)', m.group(1)): metrics['draw-prep ' + k] = float(v)
            continue
        m = re.search(r'\[draw-prep-sub\] \d+ frames: (.*) ms/frame', line)
        if m:
            for k, v in re.findall(r'([\w-]+) ([\d.]+) \(\d+\)', m.group(1)): metrics['draw-prep-sub ' + k] = float(v)
            continue
        m = re.search(r'\[hle-time\] (?:\d+ frames \([^)]*\):)?(.*)', line)
        if m and '[hle-time]' in line and 'timing on' not in line:
            for k, v in re.findall(r' ([\w:]+) ([\d.]+) \(\d+\)', m.group(1)): metrics['hle ' + k] = metrics.get('hle ' + k, 0) + float(v)
            continue
        m = re.search(r'\[flare-work\] .* recording ([\d.]+) ms/frame', line)
        if m: metrics['flare-work recording'] = float(m.group(1))
    flush()
    return out


def paired(ws, key):
    """mean and standard error of (phase1 - phase0) over neighbouring opposite-phase windows."""
    diffs = []; last = {}
    for frame, phase, met in ws:
        if key not in met: continue
        other = last.get(1 - phase)
        if other is not None and frame - other[0] <= 180:
            d = met[key] - other[1] if phase == 1 else other[1] - met[key]
            diffs.append(d); last.pop(1 - phase)
        else: last[phase] = (frame, met[key])
    if not diffs: return None
    n = len(diffs); mean = sum(diffs) / n
    se = math.sqrt(sum((d - mean) ** 2 for d in diffs) / (n - 1) / n) if n > 1 else float('nan')
    return mean, se, n


def main():
    a = sys.argv[1:]
    log = a[0]; lo, hi = arg(a, '--from', 0, int), arg(a, '--to', 1 << 30, int)
    ws = windows_from_log(log, lo, hi)
    n0 = sum(1 for w in ws if w[1] == 0); n1 = len(ws) - n0
    print(f'{log}: windows ({lo},{hi}] phase 0 (original) {n0}, phase 1 (new) {n1}')
    keys = []
    for _, _, met in ws:
        for k in met:
            if k not in keys: keys.append(k)
    print(f'  {"metric":44s} {"phase0":>9s} {"phase1":>9s} {"paired 1-0":>11s} {"+-se":>7s} pairs')
    for k in keys:
        v0 = [m[k] for _, p, m in ws if p == 0 and k in m]; v1 = [m[k] for _, p, m in ws if p == 1 and k in m]
        if not v0 or not v1: continue
        pr = paired(ws, k)
        if not pr: continue
        mean, se, n = pr
        print(f'  {k:44s} {sum(v0)/len(v0):9.3f} {sum(v1)/len(v1):9.3f} {mean:+11.3f} {se:7.3f} {n}')
    samples = arg(a, '--samples')
    if samples:
        exe = arg(a, '--exe'); nm = arg(a, '--nm', 'nm'); a2l = arg(a, '--addr2line', 'addr2line'); top = arg(a, '--top', 30, int)
        base = 0
        for line in subprocess.run([nm, exe], capture_output=True, text=True).stdout.splitlines():
            if line.endswith(' __executable_start'):
                v = int(line.split()[0], 16); base = v if v > 0x1000 else 0; break
        counts = [collections.Counter(), collections.Counter()]; wins = [0, 0]; cps = 0; phase = None; cur = 0
        for line in open(samples):
            f = line.split()
            if not f: continue
            if f[0] == 'frame':
                cur = int(f[1]); phase = None
                if len(f) > 10 and f[8] == 'ab':
                    x, y = int(f[9]), int(f[10])
                    phase = 0 if y <= 3 and x > 3 else 1 if x <= 3 and y > 3 else None
                if len(f) > 7 and f[6] == 'helper-cycles': cps = max(cps, int(f[7]))
                if phase is not None and lo < cur <= hi: wins[phase] += 1
                continue
            if phase is None or not (lo < cur <= hi) or int(f[0]) != 2: continue
            counts[phase][int(f[1], 16) + base] += int(f[2])
        offs = sorted(set(counts[0]) | set(counts[1]))
        out = subprocess.run([a2l, '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
        name = {o: out[2 * i] for i, o in enumerate(offs)}
        by = [collections.Counter(), collections.Counter()]
        for p in (0, 1):
            scale = (cps / 1e6 if cps else 1e-3) / (wins[p] * 60) if wins[p] else 0
            for o, n in counts[p].items(): by[p][name[o]] += n * scale
        t0, t1 = sum(by[0].values()), sum(by[1].values())
        g0 = sum(v for k, v in by[0].items() if not k.startswith('f_')); g1 = sum(v for k, v in by[1].items() if not k.startswith('f_'))
        print(f'sampled scene helper, Mcycles/frame: phase0 {t0:.3f} ({wins[0]} windows) phase1 {t1:.3f} ({wins[1]} windows); outside guest code {g0:.3f} -> {g1:.3f} ({g1 - g0:+.3f})')
        for k in sorted(set(by[0]) | set(by[1]), key=lambda k: -abs(by[1][k] - by[0][k]))[:top]:
            print(f'  {by[0][k]:8.3f} {by[1][k]:8.3f} {by[1][k] - by[0][k]:+8.3f}  {k}')


if __name__ == '__main__':
    main()
