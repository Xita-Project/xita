#!/usr/bin/env python3
"""tools/rec_ab.py - split one XV_REC_AB run (runtime/xv_record_opt.h) into its two phases and compare them.

  tools/rec_ab.py <log> [--from F] [--to T] [--samples S --exe E [--nm NM --addr2line A2L] [--top N]]

XV_REC_AB=<frames> alternates every recording knob between the original path (phase 0) and the new path (phase 1)
every <frames> recorded frames. Each report window is classified by the frames it spent in each phase; a window
with at most 3 frames of the other phase belongs to a phase, straddling windows are dropped. Neighbouring windows
of opposite phases are paired, so slow drift (thermal, other work on the machine) cancels; the table gives the mean
per-window difference and its standard error.

Window sources:
  - "[rec-ab] draw frames a b" (runtime, Vita and host): starts the window of the [draw-prep], [draw-prep-sub],
    [flare-work] and "frame time" (game/wait/pump/draw-hle ms) lines that follow it in the same report batch
  - "[rec-ab] hle frames a b" (xd3d.c Present report): starts the window of the [hle-time] lines that follow
  - "[host-perf] ... ab a b" (host harness, XV_HOST_PERF=1): scene-helper cycles/instructions/CPU per frame
  Host logs without [rec-ab] lines (older builds) fall back to "[host] report at frame N" windows; there the
  [hle-time] block of a window is printed before the report that closes it, so it is attributed forward.
With --samples (XV_HOST_SAMPLE + XV_HOST_PERF_SAMPLE) it also prints per-symbol scene-helper Mcycles per frame in
each phase (largest differences first)."""
import collections, math, re, subprocess, sys


def arg(a, name, default=None, conv=str):
    return conv(a[a.index(name) + 1]) if name in a else default


def phase_of(a, b):
    return 0 if b <= 3 and a > 3 else 1 if a <= 3 and b > 3 else None


def windows_from_log(path):
    """{kind: [(seq, frame, phase, {metric: value})]} for kinds 'draw', 'hle', 'perf'."""
    lines = open(path, errors='replace').read().splitlines()
    have_markers = any('[rec-ab] draw frames' in l for l in lines)
    out = collections.defaultdict(list)
    cur = {'draw': None, 'hle': None}       # open window per kind: [seq, frame, phase, metrics]
    frame = 0; seq = 0
    pending_hle = {}                        # fallback: hle metrics waiting for the report that closes their window
    def close(kind):
        w = cur[kind]
        if w is not None and w[2] is not None and w[3]: out[kind].append(tuple(w))
        cur[kind] = None
    for line in lines:
        m = re.search(r'\[host\] report at frame (\d+)', line)
        if m:
            frame = int(m.group(1)); seq += 1
            if not have_markers:
                close('draw'); close('hle')
                cur['draw'] = [seq, frame, None, {}]
                cur['hle'] = [seq, frame, None, dict(pending_hle)]; pending_hle = {}
            continue
        m = re.search(r'\[rec-ab\] (draw|hle) frames (\d+) (\d+)', line)
        if m:
            kind = m.group(1); seq += 1; close(kind)
            cur[kind] = [seq, frame, phase_of(int(m.group(2)), int(m.group(3))), {}]
            continue
        m = re.search(r'\[host-perf\] frame (\d+) scene-helper: ([\d.]+) Mcycles ([\d.]+) Minstr ([\d.]+) ms CPU per frame \(\d+ frames\) ab (\d+) (\d+)', line)
        if m:
            ph = phase_of(int(m.group(5)), int(m.group(6)))
            if ph is not None:
                out['perf'].append((seq, int(m.group(1)), ph, {'helper Mcycles': float(m.group(2)), 'helper Minstr': float(m.group(3)),
                                                              'helper CPU ms': float(m.group(4))}))
            if not have_markers:
                for k in ('draw', 'hle'):
                    if cur[k] is not None: cur[k][2] = ph
            continue
        target = None; met = {}
        m = re.search(r'\[draw-prep\] \d+ frames \d+ draws: (.*) ms/frame', line)
        if m:
            target = 'draw'
            for k, v in re.findall(r'(\w+) ([\d.]+)', m.group(1)): met['draw-prep ' + k] = float(v)
        m = re.search(r'\[draw-prep-sub\] \d+ frames: (.*) ms/frame', line)
        if m:
            target = 'draw'
            for k, v in re.findall(r'([\w-]+) ([\d.]+) \(\d+\)', m.group(1)): met['draw-prep-sub ' + k] = float(v)
        m = re.search(r'\[flare-work\] .* recording ([\d.]+) ms/frame', line)
        if m: target = 'draw'; met['flare-work recording'] = float(m.group(1))
        m = re.search(r'frame time: game ([\d.]+) ms \+ wait ([\d.]+) ms .*\| pump ([\d.]+) ms .* draw-hle ([\d.]+) ms', line)
        if m:
            target = 'draw'
            for k, v in zip(('frame game', 'frame wait', 'frame pump', 'frame draw-hle'), m.groups()): met[k + ' ms'] = float(v)
        if '[hle-time]' in line and 'timing on' not in line:
            body = re.sub(r'^.*?\[hle-time\] (\d+ frames \([^)]*\):)?', '', line)
            target = 'hle'
            for k, v, n in re.findall(r' ([\w:]+) ([\d.]+) \((\d+)\)', ' ' + body):
                met['hle ' + k] = met.get('hle ' + k, 0) + float(v)
                met['calls ' + k] = met.get('calls ' + k, 0) + float(n)
        if target and met:
            if target == 'hle' and not have_markers:
                for k, v in met.items(): pending_hle[k] = pending_hle.get(k, 0) + v
            elif cur[target] is not None:
                for k, v in met.items(): cur[target][3][k] = cur[target][3].get(k, 0) + v
    close('draw'); close('hle')
    return out


def paired(ws, key):
    """mean and standard error of (phase1 - phase0) over neighbouring opposite-phase windows."""
    diffs = []; last = {}
    for seq, frame, phase, met in ws:
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
    allw = windows_from_log(log)
    print(f'{log}: frames ({lo},{hi}]')
    print(f'  {"metric":48s} {"phase0":>9s} {"phase1":>9s} {"paired 1-0":>11s} {"+-se":>7s} pairs')
    for kind in ('perf', 'draw', 'hle'):
        ws = [w for w in allw.get(kind, []) if lo < w[1] <= hi]
        keys = []
        for w in ws:
            for k in w[3]:
                if k not in keys: keys.append(k)
        for k in keys:
            v0 = [w[3][k] for w in ws if w[2] == 0 and k in w[3]]; v1 = [w[3][k] for w in ws if w[2] == 1 and k in w[3]]
            if not v0 or not v1: continue
            pr = paired(ws, k)
            if not pr: continue
            mean, se, n = pr
            print(f'  {k:48s} {sum(v0)/len(v0):9.3f} {sum(v1)/len(v1):9.3f} {mean:+11.3f} {se:7.3f} {n}')
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
                if len(f) > 10 and f[8] == 'ab': phase = phase_of(int(f[9]), int(f[10]))
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
