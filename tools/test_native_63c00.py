#!/usr/bin/env python3
"""Differential test of the native lens-flare visibility test (recomp/kernel/xk_native_63c00.c) against the lifted guest
bodies of a stage (generated code is not in the repository).

  tools/test_native_63c00.py <stage>/recomp [cases] [--seed N] [--cc CC] [--variants a,b] [--mutants] [--no-verify]

Extracts f_00063C00 (the entry hook is added to the extracted text when the stage has none), f_000637A0, f_000B5EA0,
f_00019E7B, f_0001EC1F and f_0001EABA, links them with the stage's xk_math.c (xv_math_point_transform) and
xk_crt_float.c (xv_native_crt_float), tools/tests/native_63c00.c and the native, and runs the randomized cases in
several builds: the CRT floor native (XV_NATIVE_CRT_FLOAT, env on / env off) or lifted, the per-thread page table with
the render view, the object-job math guard, -O0. --mutants builds deliberately broken copies of the native and expects
each to be caught."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FUNCS = ['f_00063C00', 'f_000637A0', 'f_000B5EA0', 'f_00019E7B', 'f_0001EC1F', 'f_0001EABA']
ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
HOOK = ('#if defined(XV_NATIVE_63C00) && XV_NATIVE_63C00\n'
        '    { extern int xv_native_63c00(xctx *); if (xv_native_63c00(c)) return; }\n#endif\n')
VARIANTS = {
    'crt-native -O2': (['-O2', '-DXV_NATIVE_CRT_FLOAT=1'], {}),
    'crt-native env-off -O2': (['-O2', '-DXV_NATIVE_CRT_FLOAT=1'], {'XV_NATIVE_CRT_FLOAT': '0'}),
    'crt-lifted -O2': (['-O2'], {}),
    'thread-table+render-view -O2': (['-O2', '-DXV_NATIVE_CRT_FLOAT=1', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'], {}),
    'object-jobs+census -O2': (['-O2', '-DXV_NATIVE_CRT_FLOAT=1', '-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DXV_LIGHT_QUERY_CENSUS'], {}),
    'crt-lifted -O0': (['-O0'], {}),
}
# (description, old, new): each must match exactly once in the native; the test must report mismatches.
# Equivalent, so not listed: 'depth clamp keeps an equal depth' (one >= Z: z == [1F0A78] stores the same value either
# way and that compare's condition codes are overwritten), 'clamp: hi test uses >=' (v == hi keeps the same value).
MUTANTS = [
    ('depth row summation order', 'const double D2 = ((pr[10] * P2 + pr[6] * P1) + pr[2] * P0) + pr[14];', 'const double D2 = (pr[10] * P2 + (pr[6] * P1 + pr[2] * P0)) + pr[14];'),
    ('no float rounding of the stored depth', 'double Z = invw * n63_d(p->fD2);', 'double Z = invw * p->D2;'),
    ('extent raise uses >=', 'const int rx_raise = one > rxs, ry_raise = one > rys;', 'const int rx_raise = one >= rxs, ry_raise = one > rys;'),
    ('size test uses >=', 'if (!(p->a2f > eps)) {', 'if (!(p->a2f >= eps)) {'),
    ('depth test uses >=', 'if (!(D2 > eps)) {', 'if (!(D2 >= eps)) {'),
    ('clamp: lo test uses <=', 'if (v < lo) { cmp_b = lo; v = lo; N63_COV(CV_LO); }', 'if (v <= lo) { cmp_b = lo; v = lo; N63_COV(CV_LO); }'),
    ('fsw TOP of the depth clamp compare', 'const uint16_t top3 = (uint16_t)(top | (((p->T + 5u) & 7u) << 11));', 'const uint16_t top3 = top;'),
    ('lifted CRT: condition codes of an earlier compare', 'if (!p->crt_native) { cmp_a = r; cmp_b = v; }', 'if (!p->crt_native && k < 3) { cmp_a = r; cmp_b = v; }'),
    ('floor under the caller control word', 'c->fcw = cw; r = x87_round(c, v); c->fcw = p->fcw;', 'r = x87_round(c, v);'),
    ('lifted CRT frame: one dead word missing', '        N63_W(0x54u, 0x19F1Cu); N63_W(0x58u, E - 0x38u);', '        N63_W(0x54u, 0x19F1Cu);'),
    ('dead stack: B5EA0 return address (empty rectangle)', '    N63_W(0x60u, 0x637EBu);\n    N63_W(0x4u, p->fZ);', '    N63_W(0x4u, p->fZ);'),
    ('dead x87 slot of the point transform', 'c->st[(T - 3u) & 7u] = p->ONE; c->st[(T - 4u) & 7u] = p->y_last;', 'c->st[(T - 3u) & 7u] = p->ONE;'),
    ('second vertex x', 'N63_W(0x10u, fX1); N63_W(0x44u, fX1); N63_W(0x4Cu, 0x63E21u);', 'N63_W(0x10u, fX1); N63_W(0x44u, fX0); N63_W(0x4Cu, 0x63E21u);'),
    ('HLE view: a re-pushed equal word skipped wrongly', 'N63_W(0x44u, fX0); N63_W(0x4Cu, 0x63E4Fu);', 'N63_W(0x4Cu, 0x63E4Fu);'),
    ('imul overflow cells', 'c->f_cf = c->f_of = prod != (int64_t)(int32_t)area;', ''),
    ('exit edx on the depth-fail path', 'r[1] = N63_MATRIX; r[2] = p->scale;', 'r[1] = N63_MATRIX; r[2] = p->A1;'),
    ('st(0) at the first SetVertexData4f', 'c->st[T] = (double)(int32_t)x0s;', 'c->st[T] = (double)(int32_t)y0s;'),
    ('no decline for a point in the stack window', 'if (p->A1 + 12u > E - N63_WINDOW_LO && p->A1 < E + 0x10u) return N63_DECLINE_LAYOUT;', ''),
    ('no decline for a misaligned esp', '    if (E & 3u) return N63_DECLINE_LAYOUT;\n', ''),
    ('lifted CRT floor: back-edge budget not charged', '        c->preempt -= (int32_t)p->backedges;', ''),
]

def body(src, fn):
    m = re.search(r'^void %s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: sys.exit(f'{fn} not found')
    return src[m.start():src.index('\n}\n', m.end()) + 3]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--variants', default=''); ap.add_argument('--mutants', action='store_true'); ap.add_argument('--no-verify', action='store_true')
    ap.add_argument('--native', default=str(ROOT / 'recomp/kernel/xk_native_63c00.c'))
    ap.add_argument('--mutant-filter', default='', help='only the mutants whose description contains this')
    ap.add_argument('--bench', action='store_true', help='time the guest body and the native on drawn camera scenes instead')
    ap.add_argument('--keep', default='', help='copy the (first variant) test binary here and do not run it (cross builds: --cc, --cflags)')
    ap.add_argument('--cflags', default='', help='extra compiler flags (e.g. -static -marm -march=armv7-a -mfpu=neon -mfloat-abi=hard)')
    a = ap.parse_args(); rec = Path(a.recomp).resolve()
    shards = {p: p.read_text(errors='replace') for p in sorted(rec.glob('code_*.c'))}
    bodies, preamble = [], None
    for fn in FUNCS:
        src = next((s for s in shards.values() if re.search(r'^void %s\(xctx' % fn, s, re.M)), None)
        if src is None: sys.exit(f'no shard defines {fn}')
        b = body(src, fn)
        if fn == 'f_00063C00':
            preamble = src[:src.index('\nvoid f_')]
            if 'xv_native_63c00(c)' not in b:
                i = b.index(ANCHOR) + len(ANCHOR); b = b[:i] + HOOK + b[i:]
        bodies.append(b)
    guest = ('#include "xv_x86rt.h"\n#include "xv_phase.h"\n'
             '#ifndef XV_EXPERIMENTAL_OBJECT_JOBS\n#define XV_HLE_PROXY(fn) 0   /* the protos define it only with object jobs */\n#endif\n' + preamble) + '\n' + '\n'.join(bodies)
    kernel = rec / 'kernel'
    variants = {k: v for k, v in VARIANTS.items() if not a.variants or any(x in k for x in a.variants.split(','))}
    native_src = Path(a.native).read_text()
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-63c00-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest)
        def build(flags, native_path, exe):
            cmd = [a.cc, *flags, *a.cflags.split(), '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_63C00=1', '-DXV_NATIVE_63C00_COVERAGE=1',
                   '-I' + str(rec), '-I' + str(kernel), str(ROOT / 'tools/tests/native_63c00.c'), str(d / 'guest.c'),
                   str(native_path), str(kernel / 'xk_math.c'), str(kernel / 'xk_crt_float.c'), '-lm', '-o', str(exe)]
            subprocess.run(cmd, check=True)
        def run(exe, env_extra, cases, seed):
            env = dict(os.environ, **env_extra)
            r = subprocess.run([str(exe), str(cases), str(seed), '0' if a.no_verify else '1'] + (['bench'] if a.bench else []), capture_output=True, text=True, env=env)
            return r.returncode, r.stdout.strip()
        (d / 'native.c').write_text(native_src)
        for name, (flags, env_extra) in variants.items():
            exe = d / 'test'; build(flags, d / 'native.c', exe)
            if a.keep:
                import shutil; shutil.copy(exe, a.keep); print(f'[{name}] built {a.keep}'); break
            code, out = run(exe, env_extra, a.cases, a.seed)
            print(f'[{name}] ' + out.replace('\n', f'\n[{name}] '), flush=True)
            rc |= code
        if a.mutants:
            caught = 0
            mutants = [m for m in MUTANTS if a.mutant_filter in m[0]]
            for desc, old, new in mutants:
                if native_src.count(old) != 1: print(f'[mutant] {desc}: pattern matched {native_src.count(old)} times'); rc |= 1; continue
                (d / 'mutant.c').write_text(native_src.replace(old, new))
                exe = d / 'mutant'
                build(['-O2', '-DXV_NATIVE_CRT_FLOAT=1'] if 'lifted' not in desc else ['-O2'], d / 'mutant.c', exe)
                code, out = run(exe, {}, a.cases, a.seed)
                m = re.search(r'(\d+) mismatches', out); n = int(m.group(1)) if m else -1
                caught += n > 0
                print(f'[mutant] {desc}: {n} of {a.cases} cases mismatch' + ('' if n > 0 else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught}/{len(mutants)} caught')
            if caught != len(mutants): rc |= 1
    sys.exit(rc)

if __name__ == '__main__': main()
