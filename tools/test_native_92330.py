#!/usr/bin/env python3
"""Differential test of the native light cluster query (recomp/kernel/xk_native_92330.c) against the lifted guest
bodies f_00056670 + f_00052240 + f_00051E90 + f_00011840 + f_000B77C0 + f_000A9330 of a stage (generated code is not
in the repository).

  tools/test_native_92330.py <stage>/recomp [cases] [--cc CC] [--seed N] [--verify] [--mutants N]

The stage's shards are only read: f_00056670 gets the XV_NATIVE_92330 hooks in the extracted copy when it does not
carry them (tools/patch_native_92330_hooks.py logic). The native is compiled against the stage's own xv_x86rt.h.
Builds tools/tests/native_92330.c three ways - plain page table -O2, host per-thread table + render view -O2 (image
globals through the page table, as on the Vita) and -O0 - and runs the randomized cases in each. --verify also runs
the in-game verify mode (mode 1) per case. --mutants N builds deliberately broken natives and requires each to be
caught within N cases (a crash or a hang counts; two mutants need ~3000 cases: a summation association that changes
one rounding in 3000, and the exit carry flag that only the stack-straddling-0 scenes reach)."""
import argparse, os, re, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import patch_native_92330_hooks as hooks

FUNCS = ['00056670', '00052240', '00051E90', '00011840', '000B77C0', '000A9330']
def body(src, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, src, re.M)
    if not m: return None
    return src[m.start():src.index('\n}\n', m.end()) + 3]
def hook(text):
    """The hooked f_00056670 (the tool's edits applied to the extracted body)."""
    if 'xv_native_92330(c, &xn92_)' in text: return text
    head, rest = text.split('{\n', 1)
    b = rest
    if b.count(hooks.DECL_ANCHOR) != 1 or b.count(hooks.LABEL) != 1: sys.exit('f_00056670: hook anchors not found')
    b = b.replace(hooks.DECL_ANCHOR, hooks.DECL_ANCHOR + hooks.DECL, 1).replace(hooks.LABEL, hooks.HOOK + hooks.LABEL, 1).replace(hooks.RET, hooks.POST)
    return head + '{\n' + b

# Deliberately broken natives: each must produce a mismatch.
MUTANTS = [
    ('d2 association (dz2 + (dx2 + dy2))', '    double d2 = dz * dz, t = dx * dx;\n    d2 = d2 + t;\n    t = dy * dy;\n    d2 = d2 + t;',
     '    double d2 = dz * dz, t = dx * dx + dy * dy;\n    d2 = d2 + t;'),
    ('circle test strict/non-strict', 'if (cc == 0x0100) {', 'if (cc & 0x4100) {'),
    ('edge back-edge not counted', 'if (i < n) { be++; continue; }', 'if (i < n) { continue; }'),
    ('dead projected z not stored', 'n9_swf(m, k, G + 0x1Cu, k1);', '(void)k1;'),
    ('datum hint off by one', 'eax = ebx + 1u;\n        n9_w16(m, edx + 0x2Cu', 'eax = ebx;\n        n9_w16(m, edx + 0x2Cu'),
    ('fsw TOP replaced, not OR-ed', '(*fsw & ~0x4700u)', '(*fsw & ~0x7F00u)'),
    ('list write for count >= 0', 'if ((int16_t)edx > 0) { n9_w16(m, edi, (uint16_t)ebx); edi += 2u; }', 'if ((int16_t)edx >= 0) { n9_w16(m, edi, (uint16_t)ebx); edi += 2u; }'),
    ('dirty reload skipped', '                if (s->dirty) {\n                    pos = n9_r32(m, S - 4u);', '                if (0) {\n                    pos = n9_r32(m, S - 4u);'),
    ('slot 6 wrong value', 'N9_SL(4, rr); N9_SL(5, d2); N9_SL(6, rr2);', 'N9_SL(4, rr); N9_SL(5, d2); N9_SL(6, d2);'),
    ('plane distance: fabs before the store', 'const float distf = n9_swf(m, &k, F + 0xCu, k1);\n    k1 = fabs(k1);', 'k1 = fabs(k1);\n    const float distf = n9_swf(m, &k, F + 0xCu, k1);'),
    ('salt wrap to 0x8000 missing', 'if (n9_r16(m, edx + 0x32u) == 0) n9_w16(m, edx + 0x32u, 0x8000u);', ''),
    ('exit carry flag', 'c->f_cf = ebx < b0;', 'c->f_cf = 0;'),
    ('11840 tie goes to y', 'axis = (cc & 0x100) ? 0u : 1u;', 'axis = (cc & 0x4100) ? 0u : 1u;'),
    ('stamp kept when it holds the previous epoch', 'if (n9_r32(m, a) != edx) {\n            n9_w32(m, a, edx);', 'if (n9_r32(m, a) != edx && n9_r32(m, a) != edx - 1u) {\n            n9_w32(m, a, edx);'),
    ('cluster record not re-translated after a child', '                    hc = n9_span(m, esi + 0x5Cu, 8);', '                    (void)0;'),
    ('scan back-edges off by one when full', 'be += ebx - b0 - 1u; goto pop;', 'be += ebx - b0; goto pop;'),
]

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recomp'); ap.add_argument('cases', nargs='?', default='3000')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc')); ap.add_argument('--seed', default='1')
    ap.add_argument('--verify', action='store_true'); ap.add_argument('--mutants', type=int, default=0)
    ap.add_argument('--variants', default='all')
    a = ap.parse_args(); rec = Path(a.recomp)
    shards = sorted(rec.glob('code_*.c')); texts = {}
    for p in shards:
        t = p.read_text(errors='replace')
        for fn in FUNCS:
            if fn not in texts and re.search(r'^void f_%s\(xctx' % fn, t, re.M): texts[fn] = (p, t)
    missing = [fn for fn in FUNCS if fn not in texts]
    if missing: sys.exit(f'not found in {rec}: {missing}')
    preamble = texts['00056670'][1][:texts['00056670'][1].index('\nvoid f_')]
    parts = []
    for fn in FUNCS:
        b = body(texts[fn][1], fn)
        parts.append(hook(b) if fn == '00056670' else b)
    extra = '#include "xv_x87reg.h"\n' if (rec / 'xv_x87reg.h').exists() and any('xfsp0' in p for p in parts) else ''
    # stages with XV_QSERIAL hooks (tools/patch_qserial.py) define X_QS8/X_QS32 between functions: the owner-thread meaning
    qs = '#ifndef X_QS32\n#define X_QS8(a) X_IMG8(a)\n#define X_QS32(a) (*(xu32_u *)X_G(a))\n#endif\n'
    guest = '#include "xv_x86rt.h"\n#include "xv_phase.h"\n' + extra + preamble + '\n' + qs + '\n'.join(parts)
    guest += '\n#include <stdio.h>\nvoid xv_x87reg_miss(xctx *c, uint32_t site) __attribute__((weak));\nvoid xv_x87reg_miss(xctx *c, uint32_t site) { (void)c; fprintf(stderr, "x87reg miss %08X\\n", site); }\n' if extra else ''
    variants = {'plain -O2': ['-O2'], 'thread-table+render-view -O2': ['-O2', '-DXV_THREAD_PAGE_TABLE=1', '-DXV_RENDER_VIEW=1'],
                'plain -O0': ['-O0']}
    if a.variants != 'all': variants = {k: v for k, v in variants.items() if k.split()[0] in a.variants.split(',')}
    rc = 0
    with tempfile.TemporaryDirectory(prefix='xita-native-92330-') as d:
        d = Path(d); (d / 'guest.c').write_text(guest); (d / 'kernel').mkdir()
        native_src = (ROOT / 'recomp/kernel/xk_native_92330.c').read_text()
        def build(exe, flags, native_text):
            (d / 'kernel/xk_native_92330.c').write_text(native_text)
            cmd = [a.cc, *flags, '-std=gnu11', '-w', '-fno-strict-aliasing', '-ffp-contract=off', '-DXV_NATIVE_92330=1', '-I' + str(rec),
                   '-I' + str(rec / 'kernel'), str(ROOT / 'tools/tests/native_92330.c'), str(d / 'guest.c'),
                   str(d / 'kernel/xk_native_92330.c'), '-lm', '-o', str(exe)]
            subprocess.run(cmd, check=True)
        for name, flags in variants.items():
            exe = d / 'test'; build(exe, flags, native_src)
            args = [str(exe), a.cases, a.seed] + (['--verify'] if a.verify else [])
            r = subprocess.run(args, capture_output=True, text=True)
            print(f'[{name}] ' + (r.stdout.strip() + r.stderr.strip()).replace('\n', f'\n[{name}] '), flush=True)
            rc |= r.returncode
        if a.mutants:
            caught = 0
            for name, old, new in MUTANTS:
                if native_src.count(old) != 1: print(f'[mutant] {name}: pattern not found once'); rc |= 1; continue
                exe = d / 'mutant'; build(exe, ['-O2'], native_src.replace(old, new))
                try:
                    r = subprocess.run([str(exe), str(a.mutants), a.seed], capture_output=True, text=True, timeout=1800)
                    m = re.search(r'(\d+) mismatches', r.stdout)
                    n = int(m.group(1)) if m else -1
                    how = f'{n} of {a.mutants} cases differ' if m else f'no summary (exit {r.returncode}: the mutant crashed)'
                except subprocess.TimeoutExpired:
                    n, how = -1, 'no summary (the mutant never finished)'
                ok = n != 0                                  # a crash or a hang is a difference too
                caught += ok
                print(f'[mutant] {name}: {how}' + ('' if ok else '  <-- NOT CAUGHT'), flush=True)
            print(f'[mutant] {caught} of {len(MUTANTS)} mutants caught')
    sys.exit(rc)
if __name__ == '__main__': main()
