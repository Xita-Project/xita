#!/usr/bin/env python3
"""tools/record_profile.py - where one thread's CPU time goes, from recomp/host/sampler.c output (XV_HOST_SAMPLE).

  tools/record_profile.py <samples> <harness> [--from F] [--to T] [--phase 0|1] [--kind 2] [--top 40] [--nm NM] [--addr2line A2L]

Frames (F, T] select 60-frame windows. kind 0 = owner, 2 = scene helper, 1 = other threads. Every sampled PC is
resolved with `addr2line -f -i` (the innermost inlined function and its file:line, plus the outermost symbol), then
reported three ways: by outermost symbol, by innermost inlined function, and by source line for the runtime and
xd3d.c (the D3D recording path). The group table splits the thread into guest code (f_XXXXXXXX), the recording
path (runtime/*.c, xd3d.c), libc (mem*), and the rest. 1 sample = 1 ms of CPU (ITIMER_PROF)."""
import collections, subprocess, sys


def arg(a, name, default=None, conv=str):
    return conv(a[a.index(name) + 1]) if name in a else default


def main():
    a = sys.argv[1:]
    if a and a[0] == '--ab':
        return ab(a[1:])
    src, exe = a[0], a[1]
    lo, hi = arg(a, '--from', 0, int), arg(a, '--to', 1 << 30, int)
    kind, top = arg(a, '--kind', 2, int), arg(a, '--top', 40, int)
    focus = arg(a, '--focus', None)   # comma list of outermost symbols: line table of everything inlined into them
    want_phase = arg(a, '--phase', None, int)   # XV_REC_AB runs: only windows of this phase (0 original, 1 new)
    nm, a2l = arg(a, '--nm', 'nm'), arg(a, '--addr2line', 'addr2line')
    base = 0
    for line in subprocess.run([nm, exe], capture_output=True, text=True).stdout.splitlines():
        if line.endswith(' __executable_start'):
            v = int(line.split()[0], 16); base = v if v > 0x1000 else 0; break
    counts = collections.Counter(); lrs = collections.Counter(); cur = 0; windows = 0; total_all = collections.Counter(); in_window = False
    cycles_per_sample = 0   # "frame F samples S dropped D helper-cycles N": kind-2 samples are every N user cycles (perf)
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame':
            cur = int(f[1]); ok = lo < cur <= hi
            if want_phase is not None:
                ph = None
                if len(f) > 10 and f[8] == 'ab':
                    x, y = int(f[9]), int(f[10]); ph = 0 if y <= 3 and x > 3 else 1 if x <= 3 and y > 3 else None
                ok = ok and ph == want_phase
            in_window = ok; windows += ok
            if len(f) > 7 and f[6] == 'helper-cycles' and ok: cycles_per_sample = max(cycles_per_sample, int(f[7]))
            continue
        if not in_window: continue
        total_all[int(f[0])] += int(f[2])
        if int(f[0]) == kind:
            counts[int(f[1], 16) + base] += int(f[2])
            if len(f) > 3: lrs[(int(f[1], 16) + base, int(f[3], 16) + base)] += int(f[2])   # XV_HOST_SAMPLE_LR=1
    offs = sorted(set(counts) | {lr for _, lr in lrs})
    res = {}
    CH = 4000
    for i in range(0, len(offs), CH):
        chunk = offs[i:i + CH]
        # -i prints the inline chain innermost first, then the outer function; separate addresses with a sentinel
        args = []
        for o in chunk: args += [hex(o), '0']
        out = subprocess.run([a2l, '-f', '-i', '-e', exe] + args, capture_output=True, text=True).stdout.split('\n')
        j = 0
        for o in chunk:
            frames = [(out[j], out[j + 1])]; j += 2          # the first pair always belongs to this address
            while j + 1 < len(out) and not (out[j].startswith('??') and out[j + 1].startswith('??')):
                frames.append((out[j], out[j + 1])); j += 2  # enclosing (inlined-into) functions
            j += 2                                           # the sentinel address 0: ?? / ??:0
            res[o] = frames
    by_outer, by_inner, by_line, groups = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    by_focus = collections.Counter(); focus_set = set(focus.split(',')) if focus else set()
    for o, n in counts.items():
        fr = res.get(o, [('?', '?')])
        inner_fn, inner_loc = fr[0]; outer_fn = fr[-1][0]
        by_outer[outer_fn] += n; by_inner[inner_fn] += n
        if outer_fn in focus_set:
            l0 = fr[0][1].split(' (')[0].split('/')[-1]
            chain = ' < '.join(f[0] for f in fr[:-1][:3])
            by_focus[f'{outer_fn}: {l0} {chain}'] += n
        loc = inner_loc.split(' (')[0]
        short = loc.split('/stage/')[-1] if '/stage/' in loc else loc.split('/')[-1]
        if 'runtime/' in short or 'xd3d.c' in short or 'vita_runtime_shim' in short: by_line[f'{short} {inner_fn}'] += n
        if outer_fn.startswith('f_'): g = 'guest code (f_*)'
        elif any(x in inner_loc for x in ('/runtime/', 'xd3d.c', 'xd3d.h', 'vita_runtime_shim')): g = 'D3D recording (runtime/, xd3d.c)'
        elif outer_fn.startswith(('mem', '__mem', 'str', '__str')) or 'libc' in inner_loc: g = 'libc (mem*/str*)'
        elif outer_fn.startswith('xv_hle_') or outer_fn.startswith('xk_'): g = 'kernel/HLE (xk_*, other xv_hle_*)'
        else: g = 'other'
        groups[g] += n
    total = sum(counts.values()) or 1
    print(f'kind {kind}: {total} samples (1 ms) in {windows} windows, frames ({lo},{hi}]; all kinds {dict(total_all)}')
    per = 1.0 / (windows * 60) if windows else 0
    unit = 'ms/frame at 1 ms/sample'
    if kind == 2 and cycles_per_sample: per *= cycles_per_sample / 1e6; unit = f'Mcycles/frame at {cycles_per_sample} cycles/sample'
    print(f'== groups ({unit}, % of this thread)')
    for g, n in groups.most_common(): print(f'  {100.0*n/total:5.1f}%  {n*per:7.3f}  {g}')
    # LR (the caller of a leaf) for samples in small wrappers: who makes the syscalls / mem* calls
    WRAP = ('__libc_do_syscall', 'syscall', 'clock_gettime', '__clock_gettime', 'memcpy', 'memset', 'memcmp', 'memmove',
            '__memcpy_neon', '__memset', '__memcmp', 'futex', '__futex', 'sem_post', 'sem_wait', '__GI_')
    by_caller = collections.Counter()
    for (pc, lr), n in lrs.items():
        fr = res.get(pc, [('?', '?')]); fn = fr[-1][0]
        if not fn.startswith(WRAP) and '__libc' not in fn: continue
        cf = res.get(lr, [('?', '?')])
        by_caller[f'{fn} <- {cf[0][0]} ({cf[0][1].split("/")[-1]}) in {cf[-1][0]}'] += n
    tables = [('outermost symbol', by_outer), ('innermost inlined function', by_inner), ('recording-path source line', by_line)]
    if by_caller: tables.append(('wrapper <- caller (LR)', by_caller))
    if by_focus: tables.append(('line inside --focus functions (innermost line, inline chain)', by_focus))
    for title, c in tables:
        print(f'== by {title}')
        for k, n in c.most_common(top): print(f'  {100.0*n/total:5.1f}%  {n*per:7.3f}  {k}')


def load(src, exe, lo, hi, kind, nm, a2l):
    """(outer-symbol Counter in Mcycles/frame, windows) for one sample file (perf-sampled kind 2)."""
    base = 0
    for line in subprocess.run([nm, exe], capture_output=True, text=True).stdout.splitlines():
        if line.endswith(' __executable_start'):
            v = int(line.split()[0], 16); base = v if v > 0x1000 else 0; break
    counts = collections.Counter(); cur = 0; windows = 0; cps = 0
    for line in open(src):
        f = line.split()
        if not f: continue
        if f[0] == 'frame':
            cur = int(f[1]); windows += lo < cur <= hi
            if len(f) > 7 and f[6] == 'helper-cycles' and lo < cur <= hi: cps = max(cps, int(f[7]))
            continue
        if lo < cur <= hi and int(f[0]) == kind: counts[int(f[1], 16) + base] += int(f[2])
    offs = sorted(counts)
    out = subprocess.run([a2l, '-f', '-e', exe] + [hex(o) for o in offs], capture_output=True, text=True).stdout.split('\n')
    by = collections.Counter()
    for i, o in enumerate(offs): by[out[2 * i]] += counts[o]
    scale = (cps / 1e6 if cps else 1e-3) / (windows * 60) if windows else 0
    return collections.Counter({k: v * scale for k, v in by.items()}), windows


def ab(a):
    """--ab <samplesA> <exeA> <samplesB> <exeB> [--from F --to T --nm NM --addr2line A2L --top N]: per-symbol
    Mcycles/frame of the scene helper in A and B (same frame windows), largest differences first."""
    sa, ea, sb, eb = a[0], a[1], a[2], a[3]
    lo, hi = arg(a, '--from', 0, int), arg(a, '--to', 1 << 30, int)
    nm, a2l, top = arg(a, '--nm', 'nm'), arg(a, '--addr2line', 'addr2line'), arg(a, '--top', 30, int)
    A, wa = load(sa, ea, lo, hi, 2, nm, a2l); B, wb = load(sb, eb, lo, hi, 2, nm, a2l)
    ta, tb = sum(A.values()), sum(B.values())
    print(f'helper Mcycles/frame: A {ta:.3f} ({wa} windows)  B {tb:.3f} ({wb} windows)  B-A {tb - ta:+.3f} ({100 * (tb - ta) / ta:+.2f}%)')
    guest = lambda k: k.startswith('f_')
    ga = sum(v for k, v in A.items() if not guest(k)); gb = sum(v for k, v in B.items() if not guest(k))
    print(f'  outside guest code (f_*): A {ga:.3f}  B {gb:.3f}  B-A {gb - ga:+.3f}')
    keys = sorted(set(A) | set(B), key=lambda k: -abs(B[k] - A[k]))
    for k in keys[:top]: print(f'  {A[k]:8.3f} {B[k]:8.3f} {B[k] - A[k]:+8.3f}  {k}')


if __name__ == '__main__':
    main()
