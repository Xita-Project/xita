#!/usr/bin/env python3
"""Install the XV_NATIVE_606B0 hooks into a stage's hand-maintained shards (idempotent).
Usage: patch_native_606b0_hooks.py <stage>/recomp

f_000606B0 (render lens flares) gets label hooks around its pure per-flare region
(recomp/kernel/xk_native_606b0.c):
    after the local-cache preamble        xv_native_606b0_enter(c, 0x606B0)   bookkeeping (a stale verify record)
    after L_000606FC: / L_00060700: /     switch (xv_native_606b0_enter(c, 0x<label>)) {
          L_00060AC0: / L_00060DA6:           case 1: goto L_00060B13; case 2: goto L_00060DC0; default: break; }
    after L_00060B13: / L_00060DC0:       xv_native_606b0_probe(c, 0x<label>)   (verify / timing of the guest region)
f_000602F0 (collect the BSP lens-flare markers) gets an entry hook after its preamble:
    { extern int xv_native_602f0(xctx *); if (xv_native_602f0(c)) return; }
All inside #if defined(XV_NATIVE_606B0) && XV_NATIVE_606B0. The natives return 0 when their env mode is 0 (default),
when they decline, and while verify mode re-runs the guest code, so the guest code below the hooks stays the
reference implementation."""
import re, sys, glob

PRE = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
IF, ENDIF = '#if defined(XV_NATIVE_606B0) && XV_NATIVE_606B0\n', '#endif\n'
ENTER = ('    { extern int xv_native_606b0_enter(xctx *, unsigned); switch (xv_native_606b0_enter(c, 0x%Xu)) '
         '{ case 1: goto L_00060B13; case 2: goto L_00060DC0; default: break; } }   /* recomp/kernel/xk_native_606b0.c */\n')
PROBE = '    { extern void xv_native_606b0_probe(xctx *, unsigned); xv_native_606b0_probe(c, 0x%Xu); }\n'
TOP = '    { extern int xv_native_606b0_enter(xctx *, unsigned); (void)xv_native_606b0_enter(c, 0x606B0u); }\n'
H602 = '    { extern int xv_native_602f0(xctx *); if (xv_native_602f0(c)) return; }   /* recomp/kernel/xk_native_606b0.c */\n'

def body_span(s, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
    if not m: return None
    return m.end(), s.index('\n}\n', m.end())

def insert_after(s, start, end, anchor, text, what):
    i = s.find(anchor, start)
    if i < 0 or i > end: sys.exit(f'{what}: anchor {anchor.strip()!r} not found inside the function')
    if s.count(anchor, start, end) != 1: sys.exit(f'{what}: anchor {anchor.strip()!r} not unique inside the function')
    i += len(anchor)
    return s[:i] + IF + text + ENDIF + s[i:], len(IF + text + ENDIF)

def main():
    root = sys.argv[1]; found = {'000606B0': 0, '000602F0': 0}
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); changed = False
        span = body_span(s, '000606B0')
        if span:
            found['000606B0'] += 1
            a, b = span
            if 'xv_native_606b0_enter(c' in s[a:b]: print(f'{f}: f_000606B0 hooks already present')
            else:
                s, d = insert_after(s, a, b, PRE, TOP, 'f_000606B0'); b += d
                for lab in (0x606FC, 0x60700, 0x60AC0, 0x60DA6):
                    s, d = insert_after(s, a, b, 'L_%08X:\n' % lab, ENTER % lab, 'f_000606B0'); b += d
                for lab in (0x60B13, 0x60DC0):
                    s, d = insert_after(s, a, b, 'L_%08X:\n' % lab, PROBE % lab, 'f_000606B0'); b += d
                changed = True; print(f'{f}: hooks installed in f_000606B0')
        span = body_span(s, '000602F0')
        if span:
            found['000602F0'] += 1
            a, b = span
            if 'xv_native_602f0(c)' in s[a:b]: print(f'{f}: f_000602F0 hook already present')
            else:
                s, d = insert_after(s, a, b, PRE, H602, 'f_000602F0')
                changed = True; print(f'{f}: hook installed in f_000602F0')
        if changed: open(f, 'w').write(s)
    for fn, n in found.items():
        if n != 1: sys.exit(f'expected exactly one definition of f_{fn}, found {n}')

if __name__ == '__main__': main()
