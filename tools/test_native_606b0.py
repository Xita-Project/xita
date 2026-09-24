#!/usr/bin/env python3
"""Differential tests of recomp/kernel/xk_native_606b0.c against the lifted guest bodies of a stage (generated code
is not in the repository).

  tools/test_native_606b0.py <stage>/recomp [cases] [--cc CC] [--seed N] [--only 606b0|602f0] [--mutants] [--bench]

The stage's f_000606B0 / f_000602F0 must carry the hooks (tools/patch_native_606b0_hooks.py). Builds
tools/tests/native_606b0.c (the per-flare region of f_000606B0: whole-function runs guest / native / verify) and
tools/tests/native_602f0.c (f_000602F0) three ways - plain page table -O2, host per-thread table + render view (image
globals through the page table, as the Vita build) -O2, and -O0 - and runs the randomized cases in each. --mutants
also builds deliberately broken copies of the native (one change each) and requires every one of them to be caught."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    '606b0': dict(test='tools/tests/native_606b0.c',
                  guest=['000606B0', '00060E90', '0005FE30', '00060000', '00011120', '00061560', '00011B60'],
                  hook='xv_native_606b0_enter(c'),
    '602f0': dict(test='tools/tests/native_602f0.c',
                  guest=['000602F0', '000B1260', '00011120', '00061270', '00019E7B', '0001EC1F', '0001EABA', '0001EAF7',
                         '0005FE80'],
                  hook='xv_native_602f0(c)'),
}
# (suite, description, text in xk_native_606b0.c, replacement): each must make its differential test fail
MUTANTS = [
    ('606b0', 'reflection vector: fadd st,st reassociated as 2*x', 'X[3] = srf(s, 0x38); X[3] = X[3] + X[3];', 'X[3] = srf(s, 0x38); X[3] = 2.0 * X[3] + 0.0 * X[1];'),
    ('606b0', 'view-axis dot: partial sum rounded to float', 'X[2] = srf(s, 0x24); X[2] = X[2] * s->g_6d8; X[1] = X[1] + X[2];\n    X[2] = srf(s, 0x20); X[2] = X[2] * s->g_6d4; X[1] = X[1] + X[2];\n    swf(s, 0x2C, X[1]);',
     'X[2] = srf(s, 0x24); X[2] = X[2] * s->g_6d8; X[1] = (float)(X[1] + X[2]);\n    X[2] = srf(s, 0x20); X[2] = X[2] * s->g_6d4; X[1] = X[1] + X[2];\n    swf(s, 0x2C, X[1]);'),
    ('606b0', 'distance fade: near and far swapped', 'X[2] = nm_f32(m, s->ebp + 0x18u); X[2] = X[2] - nm_f32(m, s->ebp + 0x1Cu);',
     'X[2] = nm_f32(m, s->ebp + 0x1Cu); X[2] = X[2] - nm_f32(m, s->ebp + 0x18u);'),
    ('606b0', 'fpatan operands swapped', 'X[1] = atan2(X[1], X[2]);                    /* fpatan */\n    X[1] = X[1] * s->c_a80;',
     'X[1] = atan2(X[2], X[1]);                    /* fpatan */\n    X[1] = X[1] * s->c_a80;'),
    ('606b0', '60000 mode 3 back-edge not counted', 's->be++;                                     /* jmp 600F2', '(void)0;                                     /* jmp 600F2'),
    ('606b0', '60000: second fxch of the pair not modelled (dead slot)', 'SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x14); SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x10); X[2] = X[2] - X[3];\n    X[1] = X[2];                                 /* fstp st(1) */\n    X[2] = srf(s, L + 0x1C)',
     'SWAP(3, 2); X[3] = X[3] * srf(s, L + 0x14); X[3] = X[3] * srf(s, L + 0x10); X[2] = X[2] - X[3];\n    X[1] = X[2];                                 /* fstp st(1) */\n    X[2] = srf(s, L + 0x1C)'),
    ('606b0', '60E90 return address not stored', '    sw32(s, kr, ret);\n    uint32_t ecx = s->ecx, edx = ecx;', '    uint32_t ecx = s->ecx, edx = ecx;'),
    ('606b0', '11120: epsilon compare TOP field', 'CMP(X[2], s->c_af8, 2);', 'CMP(X[2], s->c_af8, 1);'),
    ('606b0', '11120: squared length reassociated', 'X[4] = X[4] + X[5];\n    X[5] = X[1]; X[5] = X[5] * X[1];\n    X[4] = X[4] + X[5];',
     'X[5] = X[5] + X[1] * X[1];\n    X[4] = X[4] + X[5];'),
    ('606b0', 'edge fade stored before the clamp', '    swf(s, k, X[1]);\n}', '    swf(s, k, X[1] + 0.0 * s->c_a78 + (X[1] == s->c_a78 ? 1e-7 : 0.0));\n}'),
    ('606b0', 'shadow not consulted for data in the window', 'd[j] = o < m->win ? m->S[o] : *NM_HP(m, b);', 'd[j] = *NM_HP(m, b); (void)o;'),
    ('606b0', 'straddling read not mapped to the shadow', 'if ((uintptr_t)(p - m->h0) < m->n0) return m->S[p - m->h0];', '(void)0;'),
    ('606b0', 'reflection index read unsigned', 's->eax = (uint32_t)(int32_t)(int16_t)nm_rd(m, s->edi + s->ecx + 0x3Cu, 2);', 's->eax = nm_rd(m, s->edi + s->ecx + 0x3Cu, 2);'),
    ('606b0', 'flare-loop inc keeps no stale carry', 'FL_INCDEC(); s->eax += 1u;\n    sw32(s, 0x70, s->eax);', 's->eax += 1u;\n    sw32(s, 0x70, s->eax);'),
    ('606b0', 'shl edi,7 flags not recorded', 's->edi = nf_shl32(fl, s->edi, 7);', 's->edi <<= 7;'),
    ('606b0', 'stage byte bit 7 not masked', 's->ecx = nm_rd(m, s->esi + 0x22u, 1) & 0xFFFFFF7Fu;', 's->ecx = nm_rd(m, s->esi + 0x22u, 1);'),
    ('606b0', 'fsp not masked back', 'if (s->touched) c->fsp = s->F;', 'c->fsp = s->F;'),
    ('606b0', 'bx==0 copy of the brightness skipped', 'if (!(s->ebx & 0xFFFFu)) { s->ecx = sr32(s, 0x14); sw32(s, 0x34, s->ecx); }', '(void)0;'),
    ('606b0', 'flare barrier not called', '    xv_flare_barrier(2u);\n    const uint32_t row', '    const uint32_t row'),
    ('606b0', 'reflection count cached (not re-read)', 's->eax = nm_rd(m, s->ebp + 0xC4u, 4);       /* 60D90: next reflection */', 's->eax = (uint32_t)(int32_t)(int16_t)s->ebx + 1u + (s->ebx < 7u ? 0u : 0u) ? nm_rd(m, s->ebp + 0xC4u, 4) : 0; if (s->ebx == 0) s->eax = 2;'),
    ('606b0', 'draw test: brightness == 0 drawn', '{ const uint32_t r = AH() & 0x41u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) { info->draws++;', '{ const uint32_t r = AH() & 0x01u; FLG(XK_LOGIC, 0, 0, r, 8); if (!r) { info->draws++;'),
    ('606b0', '5FE30 object path: index not shifted', 's->eax &= 0x7FFFu; s->eax = nf_shl32(fl, s->eax, 16);', 's->eax &= 0x7FFFu; s->eax = nf_shl32(fl, s->eax, 15);'),
]

def body(src, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: sys.exit(f'f_{fn} not found')
    return src[m.start():src.index('\n}\n', m.end()) + 3]

def find(rec, fn):
    for p in sorted(rec.glob('code_*.c')):
        s = p.read_text(errors='replace')
        if re.search(r'^void f_%s\(xctx' % fn, s, re.M): return s
    sys.exit(f'no shard defines f_{fn}')

def build_and_run(cc, flags, rec, test_c, guest_c, native_c, cases, seed, d, tag, extra=()):
    exe = d / f'test-{tag}'
    common = [cc, *flags, '-std=gnu11', '-w', '-fno-strict-aliasing', '-DXV_NATIVE_606B0=1',
              '-I' + str(rec), '-I' + str(rec / 'kernel'), '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel')]
    # the native with its production flags (Makefile: -ffp-contract=off -fno-math-errno), the guest and driver like the stage
    subprocess.run(common + ['-ffp-contract=off', '-fno-math-errno', '-c', str(native_c), '-o', str(d / f'native-{tag}.o')], check=True)
    subprocess.run(common + [str(ROOT / test_c), str(guest_c), str(d / f'native-{tag}.o'), '-lm', '-o', str(exe)], check=True)
    return subprocess.run([str(exe), str(cases), str(seed), *extra], capture_output=True, text=True)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='2000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--only', choices=sorted(SUITES)); ap.add_argument('--mutants', action='store_true'); ap.add_argument('--bench', action='store_true')
    ap.add_argument('--keep', help='directory to keep the built test programs in')
    a = ap.parse_args(); rec = Path(a.recomp)
    native = ROOT / 'recomp/kernel/xk_native_606b0.c'
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-606b0-') as d:
        d = Path(a.keep) if a.keep else Path(d); d.mkdir(parents=True, exist_ok=True)
        suites = [a.only] if a.only else list(SUITES)
        guest = {}
        for name in suites:
            su = SUITES[name]
            if not (ROOT / su['test']).exists(): print(f'[{name}] no test harness yet'); continue
            first = find(rec, su['guest'][0])
            if su['hook'] not in body(first, su['guest'][0]): sys.exit(f'f_{su["guest"][0]} has no XV_NATIVE_606B0 hook')
            preamble = first[:first.index('\nvoid f_')]
            g = d / f'guest-{name}.c'
            g.write_text('#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + preamble + '\n' + '\n'.join(body(find(rec, fn), fn) for fn in su['guest']))
            guest[name] = g
            for vname, flags in variants.items():
                r = build_and_run(a.cc, flags, rec, su['test'], g, native, a.cases, a.seed, d, f'{name}-{len(vname)}-{flags[-1][-4:]}',
                                  ('bench',) if a.bench and vname == 'plain -O2' else ())
                print(f'[{name} {vname}] ' + r.stdout.strip().replace('\n', f'\n[{name} {vname}] '))
                if r.returncode or 'mismatches' not in r.stdout: rc = 1
        if a.mutants:
            from concurrent.futures import ThreadPoolExecutor
            src = native.read_text(); caught = 0
            muts = [m for m in MUTANTS if m[0] in guest]
            def one(i):
                suite, what, old, new = muts[i]
                if src.count(old) != 1: return i, None
                (d / f'mut{i}.c').write_text(src.replace(old, new))
                r = build_and_run(a.cc, ['-O2'], rec, SUITES[suite]['test'], guest[suite], d / f'mut{i}.c', a.cases, a.seed, d, f'm{i}')
                m = re.search(r'(\d+) mismatches', r.stdout)
                return i, (r.returncode, int(m.group(1)) if m else -1)
            with ThreadPoolExecutor(min(8, os.cpu_count() or 2)) as ex:
                for i, res in ex.map(one, range(len(muts))):
                    what = f'{muts[i][0]}: {muts[i][1]}'
                    if res is None: print(f'[mutant {i}] {what}: pattern not found exactly once'); rc = 1; continue
                    ok = res[0] != 0 and res[1] != 0
                    caught += ok
                    print(f'[mutant {i}] {what}: {"caught" if ok else "NOT CAUGHT"} ({res[1]} mismatching cases)')
                    if not ok: rc = 1
            print(f'mutants caught: {caught}/{len(muts)}')
    sys.exit(rc)

if __name__ == '__main__': main()
