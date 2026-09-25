#!/usr/bin/env python3
"""Differential test of XV_NATIVE_EFFECTS (recomp/kernel/xk_native_effects.c) against the lifted guest bodies of a
stage (generated code is not in the repository).

  tools/test_native_effects.py <stage>/recomp [cases] [--seed N] [--funcs 7E530,...] [--variants all|plain,tt,O0]
                               [--mutants N] [--bench N] [--replay FILE --reps N] [--threads N --iters K]
                               [--cc CC] [--extra FLAGS] [--keep PATH] [--flags-from make-n.txt]

The stage's shards are only read. The test program is: the lifted bodies of the hooked functions (behind the same
wrapper the stage gets, tools/patch_native_effects_hooks.py) and of the lifted callees they need, the unit, and
tools/tests/native_effects.c, which supplies a synthetic guest arena, deterministic stand-ins for the guest callees
outside the set (173F20, 325C0: they change registers, flags, the x87 stack and memory, and sometimes return with an
unexpected x87 depth) and for the D3D HLE entries (logged with registers, arguments and the data they read), and
randomized scenes per hooked function. Each case runs the lifted body (mode 0), the native (mode 2) and verify mode (1)
from identical state; the whole arena, every xctx field, the xv_preempt calls and the HLE call log must match (NaN
words against NaN words are counted, not failed). Built three ways: plain page table -O2, per-thread table + render
view -O2 (the stage's configuration), -O0."""
import argparse, os, re, shlex, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CALLEES = ['0007DBE0', '001290E0', '00080250', '000191E0', '00019170', '0005DD50']   # lifted callees the references need
STUBBED = ['00173F20', '000325C0']      # guest callees outside the set: stand-ins in tools/tests/native_effects.c
def hooked_of(unit_text):
    m = re.search(r'#define NX_HOOKS\(X\) \\\n(.*?)\n#define', unit_text, re.S)
    return re.findall(r'X\(([0-9A-F]{8}),', m.group(1))

# Deliberately broken natives (applied to the unit): each must produce a mismatch. Equivalent mutants left out: the
# carry an `inc edx` keeps in 7E530 (the shl before it shifts i * 3 < 2^17 by 4: its carry is always 0) and the flag
# record of 7E420's `or eax,-1` (7DBE0's first instruction rewrites the record before anything reads it).
MUTANTS = [
    ('round: half away from zero instead of to even', 'double r = (a + 4503599627370496.0) - 4503599627370496.0;', 'double r = floor(a + 0.5);'),
    ('round: the sign of a negative value dropped', 'return v < 0 ? -r : r;', 'return r;'),
    ('7E530: the scale slot not multiplied last', 'A = A * hv_rf(m, &Sv, ecx + 0x24u); hv_wf(m, &Tv, eax + 0x28u, A);', 'B = A * hv_rf(m, &Sv, ecx + 0x24u); hv_wf(m, &Tv, eax + 0x28u, B);'),
    ('7E530: loop condition signed as unsigned', 'if (sf == of) break; }                                                 /* jl 7E540 */', 'if (a >= b) break; }'),
    ('7E530: back-edge budget not counted', "HX_BACKEDGE(pre, (c->r[0] = eax, c->r[1] = ecx, c->r[2] = edx, c->r[4] = esp, c->r[7] = base, hf_store(&f, c),", "if (0) HX_BACKEDGE(pre, (c->r[0] = eax, c->r[1] = ecx, c->r[2] = edx, c->r[4] = esp, c->r[7] = base, hf_store(&f, c),"),
    ('11B60: packed colour without the green byte', 'uint32_t edx = i3 | ebx | ecx | eax;', 'uint32_t edx = i3 | ecx | eax;'),
    ('11B60: the 255 slot not left in st(4)', 'HX_SLOT(fsp0, 4) = s4; HX_SLOT(fsp0, 5) = k;\n    c->r[0] = edx; c->r[1] = ecx;', 'HX_SLOT(fsp0, 4) = s4;\n    c->r[0] = edx; c->r[1] = ecx;'),
    ('11610: alpha masked to a byte', 'edx |= hf_shl32(&f, v1, 24u);', 'edx |= hf_shl32(&f, v1 & 0xFFu, 24u);'),
    ('11BD0: blue shifted by 8', 'edx |= hf_shl32(&f, v1 & 0xFFu, 16u);', 'edx |= hf_shl32(&f, v1 & 0xFFu, 8u);'),
    ('7DBE0: the -1.0 test inverted', 'if (f->res != 0) {                                                                  /* jne: 7DC5D */', 'if (f->res == 0) {'),
    ('7E420: the second record copy skipped', 'R.edi = hv_r32(m, &Av, R.eax + 4u); R.eax = hv_r32(m, &Av, R.eax + 8u); hv_w32(m, &Fv, R.ecx + 4u, R.edi); hv_w32(m, &Fv, R.ecx + 8u, R.eax);\n            } else {', 'R.edi = hv_r32(m, &Av, R.eax + 4u); R.eax = hv_r32(m, &Av, R.eax + 8u); hv_w32(m, &Fv, R.ecx + 4u, R.edi);\n            } else {'),
    ('56F20: the rotation row difference reversed', 'x3 = x3 - x4;                                                                       /* fsubp st(1),st */', 'x3 = x4 - x3;'),
    ('56F20: sin of the unrounded angle', 'x2 = sin(hv_rf(m, &F, S + 0x2Cu));', 'x2 = sin(x1 * 1.0);'),
    ('56F20: the x87 base not moved on a depth miss', 'if (xv_x87reg_miss) xv_x87reg_miss(c, (ip)); base = (c->fsp + 1u) & 7u; }', 'if (xv_x87reg_miss) xv_x87reg_miss(c, (ip)); }'),
    ('80360: the bitmap test compares the wrong halfword', 'uint16_t a = hm_r16(m, esi + 0xAu), b = (uint16_t)ebp;', 'uint16_t a = hm_r16(m, esi + 0x8u), b = (uint16_t)ebp;'),
    ('80360: sbb without the carry of neg', 'uint32_t cf = hf_cf(&f), r = ebx - ebx - cf; hf_set(&f, XK_SBB, ebx, ebx, r, 32); f.cf = cf; ebx = r;', 'uint32_t cf = 0, r = ebx - ebx - cf; hf_set(&f, XK_SBB, ebx, ebx, r, 32); f.cf = cf; ebx = r;'),
    ('unit: journal skips word stores (verify undo incomplete)', 'uint8_t *p = hm_p(m, a); if (m->J) nx_jlog_host(m->J, a, p, 4); memcpy(p, &v, 4); }', 'uint8_t *p = hm_p(m, a); memcpy(p, &v, 4); }'),
    ('unit: undo in forward order', 'for (unsigned i = J.n; i-- > 0; ) nx_jwrite(&J.e[i], J.e[i].old);', 'for (unsigned i = 0; i < J.n; ++i) nx_jwrite(&J.e[i], J.e[i].old);'),
    ('unit: a view across a page end taken as one', 'if ((lo & 0xFFFu) + len <= 4096u) v.h = hm_p(m, lo);', 'v.h = hm_p(m, lo);'),
]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='2000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--funcs', default=''); ap.add_argument('--variants', default='all')
    ap.add_argument('--mutants', type=int, default=0); ap.add_argument('--bench', type=int, default=0)
    ap.add_argument('--replay'); ap.add_argument('--reps', type=int, default=0)
    ap.add_argument('--threads', type=int, default=0); ap.add_argument('--iters', type=int, default=20)
    ap.add_argument('--extra', default=''); ap.add_argument('--keep')
    ap.add_argument('--flags-from', help='make -n output of the stage: the defines of its kernel units (default: a fixed set)')
    ap.add_argument('--unit', default=str(ROOT / 'recomp/kernel/xk_native_effects.c'))
    a = ap.parse_args(); rec = Path(a.recomp)
    unit_text = Path(a.unit).read_text()
    HOOKED = hooked_of(unit_text)
    funcs = HOOKED + [f for f in CALLEES if f not in HOOKED]
    texts = {}
    for p in sorted(rec.glob('code_*.c')):
        t = p.read_text(errors='replace')
        for fn in funcs:
            if fn not in texts and re.search(r'^void f_%s\(xctx' % fn, t, re.M): texts[fn] = (t, p.name)
    missing = [fn for fn in funcs if fn not in texts]
    if missing: sys.exit(f'not found in {rec}: {missing}')
    first = texts[funcs[0]][0]
    preamble = first[:first.index('\nvoid f_') + 1].replace('#include "xv_x87reg.h"   /* --x87-regs */\n', '')
    parts = ['#include "xv_x87reg.h"\n', preamble]
    for fn in funcs:
        t = texts[fn][0]
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, t, re.M)
        body = t[m.start():t.index('\n}\n', m.end()) + 3]
        if fn in HOOKED:
            parts.append('void f_%s_body(xctx *restrict c);\nvoid f_%s(xctx *restrict c) { extern int xv_native_effects_%s(xctx *); '
                         'if (!xv_native_effects_%s(c)) f_%s_body(c); }\n#define f_%s f_%s_body\n' % ((fn,) * 7))
            parts.append(body + '#undef f_%s\n' % fn)
        else:
            parts.append(body)
    guest = ''.join(parts)
    if a.flags_from:
        line = next(l for l in Path(a.flags_from).read_text().splitlines() if ' -c recomp/kernel/xk_native_1721b0.c' in l or ' -c recomp/kernel/xk_native_4b9d0.c' in l)
        defs = [t for t in shlex.split(line) if t.startswith('-D')]
    else:
        defs = ['-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DXV_SCENE_THREAD=1', '-DXV_NATIVE_CONSTANT_PACK=1', '-DXV_NATIVE_63C00=1']
    defs = [d for d in defs if not d.startswith(('-DXV_THREAD_PAGE_TABLE', '-DXV_RENDER_VIEW'))]
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'], 'plain -O0': ['-O0']}
    if a.variants != 'all': variants = {k: v for k, v in variants.items() if k.split()[0].split('+')[0] in a.variants.split(',') or k.split()[0] in a.variants.split(',')}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-effects-') as d:
        d = Path(d); (d / 'kernel').mkdir()
        (d / 'guest.c').write_text(guest)
        hooks_def = '-DNX_TEST_HOOKS(X)=' + ' '.join('X(%s)' % h for h in HOOKED)
        def build(exe, flags, unit):
            (d / 'kernel/xk_native_effects.c').write_text(unit)
            cmd = [a.cc, *flags, *a.extra.split(), '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_EFFECTS=1',
                   '-DXV_NATIVE_EFFECTS_TEST=1', hooks_def, *defs, '-I' + str(d / 'kernel'), '-I' + str(rec), '-I' + str(rec / 'kernel'),
                   str(ROOT / 'tools/tests/native_effects.c'), str(d / 'guest.c'), str(d / 'kernel/xk_native_effects.c'), '-lm', '-lpthread', '-o', str(exe)]
            subprocess.run(cmd, check=True)
        common = ['--funcs', a.funcs] if a.funcs else []
        def run(exe, args, timeout=None):
            r = subprocess.run([str(exe)] + args, capture_output=True, text=True, timeout=timeout)
            return r
        if a.replay or a.bench or a.threads:
            exe = d / 'x'; build(exe, ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1', '-pthread'], unit_text)
            if a.keep: subprocess.run(['cp', str(exe), a.keep], check=True)
            if 'arm' in a.cc: print('built', a.keep or exe); sys.exit(0)
            args = [a.cases, a.seed] + common
            if a.replay: args += ['--replay', a.replay, str(a.reps)]
            elif a.bench: args += ['--bench', str(a.bench)]
            else: args += ['--threads', str(a.threads), str(a.iters)]
            r = run(exe, args); print(r.stdout.strip() + r.stderr.strip()); sys.exit(r.returncode)
        for name, flags in variants.items():
            exe = d / 'test'; build(exe, flags, unit_text)
            if a.keep: subprocess.run(['cp', str(exe), a.keep + '-' + name.replace(' ', '').replace('+', '-')], check=True)
            if 'arm' in a.cc: continue
            r = run(exe, [a.cases, a.seed] + common)
            print(f'[{name}] ' + (r.stdout.strip() + r.stderr.strip()).replace('\n', f'\n[{name}] '), flush=True)
            rc |= r.returncode
        if a.mutants:
            caught = 0
            only = os.environ.get('NX_MUTANT_ONLY')
            for name, old, new in MUTANTS:
                if only and only not in name: continue
                cnt = unit_text.count(old)
                if cnt != 1: print(f'[mutant] {name}: pattern found {cnt} times'); rc |= 1; continue
                exe = d / 'mutant'
                try: build(exe, ['-O2'], unit_text.replace(old, new))
                except subprocess.CalledProcessError: print(f'[mutant] {name}: does not compile'); rc |= 1; continue
                try:
                    r = run(exe, [str(a.mutants), a.seed] + common, timeout=1800)
                    m = re.search(r'(\d+) mismatches', r.stdout)
                    n = int(m.group(1)) if m else -1
                    how = f'{n} of {a.mutants} cases differ' if m else f'no summary (exit {r.returncode}: the mutant crashed)'
                except subprocess.TimeoutExpired:
                    n, how = -1, 'no summary (the mutant never finished)'
                ok = n != 0; caught += ok
                print(f'[mutant] {name}: {how}' + ('' if ok else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught} of {len(MUTANTS)} mutants caught')
    sys.exit(rc)

if __name__ == '__main__': main()
