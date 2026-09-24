#!/usr/bin/env python3
"""Differential test of the native aim/look overlay blend (recomp/kernel/xk_native_aim_blend.c) against the lifted guest
bodies of f_000A39B0 and its callees of a stage (generated code is not in the repository).

  tools/test_native_aim_blend.py <stage>/recomp [cases] [--cc CC] [--seed N] [--mutants]

The stage's f_000A39B0 must carry the entry hook (tools/patch_native_aim_blend_hooks.py). Builds
tools/tests/native_aim_blend.c three ways - plain page table -O2, host per-thread table + render view (image globals
through the page table, as the Vita build) -O2, and -O0 - and runs the randomized cases in each. --mutants also builds
deliberately broken copies of the native (one change each) and requires every one of them to be caught."""
import argparse, os, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GUEST = ['000A39B0', '0001D150', '00180ADA', '000220FF', '00180AE4', '0002219C', '00180C8A', '000A5080', '000A2C30',
         '000B0E70', '000B4320', '000B0EF0']
# (description, text in xk_native_aim_blend.c, replacement): each must make the differential test fail
MUTANTS = [
    ('lerp: (1 - t) rounded to float', 'X[2] = st->one; X[2] = X[2] - X[1];', 'X[2] = (float)(st->one - X[1]);'),
    ('lerp: negation also on a zero dot', 'if (!ab_parity_even(r)) { SWAP12(); X[2] = -X[2]; SWAP12(); }',
     'if (!ab_parity_even(r) || X[3] == st->zero) { SWAP12(); X[2] = -X[2]; SWAP12(); }'),
    ('normalize: reciprocal computed as a float', 'X[1] = st->one / X[1];', 'X[1] = (float)(st->one / X[1]);'),
    ('quat apply: component 0 reassociated', 'X[2] = sf(S, b + 12); X[2] = X[2] * sf(S, a); X[1] = X[1] + X[2];\n    X[2] = sf(S, a + 4); X[2] = X[2] * sf(S, b + 8); X[1] = X[1] - X[2];',
     'X[2] = sf(S, b + 12); X[2] = X[2] * sf(S, a); X[1] = X[1] + (X[2] - sf(S, a + 4) * sf(S, b + 8));'),
    ('translation: z rows blended with the x/y order', 'X[1] = X[1] * gp;\n              X[2] = sf(S, AB_Q + 0x68) * gy;',
     'X[1] = X[1] * fp;\n              X[2] = sf(S, AB_Q + 0x68) * gy;'),
    ('ftol: fistp rounding under round-to-nearest only', 'double v = ab_round(st->fcw, st->X[2]);', 'double v = nearbyint(st->X[2]);'),
    ('fmod: zero operand through fprem (keeps the sign of -0.0)', 'st->X[1] = 0.0;\n    } else return 0;', 'st->X[1] = fmod(st->X[1], st->X[2]);\n    } else return 0;'),
    ('dead stack: f_00180ADA fst qword [ebp-2D0h] not written', 'wd(S, AB_B - 0x2D0, st->X[1]);', '(void)0;'),
    ('dead stack: f_000220FF [ebp-94h] (the dispatch record) not written', 'w32(S, AB_B - 0x94, st->edx);', '(void)0;'),
    ('x87: fxch in the CRT not modelled', 'SWAP12();\n    st->ecx = (st->ecx & ~0xFFu) | s8(S, AB_B - 0x9F);', 'st->ecx = (st->ecx & ~0xFFu) | s8(S, AB_B - 0x9F);'),
    ('x87: dead slot of the z translation row', 'X[2] = sf(S, AB_Q + 0x68) * gy;\n              X[3] = sf(S, AB_Q + 0x80) * fy; X[2] = X[2] + X[3];',
     'X[2] = sf(S, AB_Q + 0x68) * gy + sf(S, AB_Q + 0x80) * fy;'),
    ('fsw: TOP field of the lerp compare', 'CMP(X[3], 3);', 'CMP(X[3], 2);'),
    ('flags: carry cell of shr', 'fl->cfo = 1; fl->cf = v & 1u; fl->ofo = 1; fl->of = v >> 31; return r;', 'fl->cfo = 1; fl->cf = (v >> 1) & 1u; fl->ofo = 1; fl->of = v >> 31; return r;'),
    ('registers: eax after the translation', 'st->eax = s32(S, AB_Q + 0x48);\n              const uint32_t node', 'const uint32_t node'),
    ('budget: loop back-edge not counted', 'if ((int16_t)st->ebx < (int16_t)cnt16) { st->be++; continue; }', 'if ((int16_t)st->ebx < (int16_t)cnt16) continue;'),
    ('clamp: pitch upper clamp >= vs >', 'if (!((int16_t)st->ebx < (int16_t)st->eax)) { st->ebx = st->eax - 1u;',
     'if ((int16_t)st->ebx > (int16_t)st->eax) { st->ebx = st->eax - 1u;'),
    ('alias: keyframe translations read after the node write order', 'st->edx = ab_m32(m, st->ecx); w32(S, AB_Q + 0x54, st->edx);',
     'st->edx = ab_m32(m, st->ecx); w32(S, AB_Q + 0x54, st->edx); (void)ab_m32(m, s32(S, AB_Q + 0x48));\n              if (0)'),
    ('decline: unaligned keyframe pointers accepted', 'if ((nodes | s32(S, AB_Q + 0x3C) | s32(S, AB_Q + 0x40) | s32(S, AB_Q + 0x34) | s32(S, AB_Q + 0x14)) & 3u)',
     'if (nodes & 3u)'),
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

def build_and_run(cc, flags, rec, guest_c, native_c, cases, seed, d, tag):
    exe = d / f'test-{tag}'
    common = [cc, *flags, '-std=gnu11', '-w', '-fno-strict-aliasing', '-DXV_NATIVE_AIM_BLEND=1', '-DXV_EXPERIMENTAL_OBJECT_JOBS=1',
              '-I' + str(rec), '-I' + str(rec / 'kernel'), '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel')]
    # the native with its production flags (Makefile: -ffp-contract=off -fno-math-errno), the guest and driver like the stage
    subprocess.run(common + ['-ffp-contract=off', '-fno-math-errno', '-c', str(native_c), '-o', str(d / f'native-{tag}.o')], check=True)
    subprocess.run(common + [str(ROOT / 'tools/tests/native_aim_blend.c'), str(guest_c), str(d / f'native-{tag}.o'), '-lm', '-o', str(exe)], check=True)
    return subprocess.run([str(exe), str(cases), str(seed)], capture_output=True, text=True)

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--mutants', action='store_true')
    a = ap.parse_args(); rec = Path(a.recomp)
    s39 = find(rec, '000A39B0')
    if 'xv_native_aim_blend(c)' not in body(s39, '000A39B0'): sys.exit('f_000A39B0 has no XV_NATIVE_AIM_BLEND hook')
    preamble = s39[:s39.index('\nvoid f_')]
    guest = '#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + preamble + '\n' + '\n'.join(body(find(rec, fn), fn) for fn in GUEST)
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    rc = 0
    native = ROOT / 'recomp/kernel/xk_native_aim_blend.c'
    with tempfile.TemporaryDirectory(prefix='xita-native-aim-blend-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest)
        for name, flags in variants.items():
            r = build_and_run(a.cc, flags, rec, d / 'guest.c', native, a.cases, a.seed, d, 'v')
            print(f'[{name}] ' + r.stdout.strip().replace('\n', f'\n[{name}] '))
            if r.returncode or 'mismatches' not in r.stdout: rc = 1
        if a.mutants:
            from concurrent.futures import ThreadPoolExecutor
            src = native.read_text(); caught = 0
            def one(i):
                what, old, new = MUTANTS[i]
                if src.count(old) != 1: return i, None
                (d / f'mut{i}.c').write_text(src.replace(old, new))
                r = build_and_run(a.cc, ['-O2'], rec, d / 'guest.c', d / f'mut{i}.c', a.cases, a.seed, d, f'm{i}')
                m = re.search(r'(\d+) mismatches', r.stdout)
                return i, (r.returncode, int(m.group(1)) if m else -1)
            with ThreadPoolExecutor(min(8, os.cpu_count() or 2)) as ex:
                for i, res in ex.map(one, range(len(MUTANTS))):
                    what = MUTANTS[i][0]
                    if res is None: print(f'[mutant {i}] {what}: pattern not found exactly once'); rc = 1; continue
                    ok = res[0] != 0 and res[1] != 0
                    caught += ok
                    print(f'[mutant {i}] {what}: {"caught" if ok else "NOT CAUGHT"} ({res[1]} mismatching cases)')
                    if not ok: rc = 1
            print(f'mutants caught: {caught}/{len(MUTANTS)}')
    sys.exit(rc)
if __name__ == '__main__': main()
