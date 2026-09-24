#!/usr/bin/env python3
"""Install the XV_NATIVE_AIM_BLEND entry hook into a stage's hand-maintained shards (idempotent).
Usage: patch_native_aim_blend_hooks.py <stage>/recomp

f_000A39B0 (the 2-D aim/look overlay blend under f_0003ECC0 / f_00036500, i.e. f_00090770's hot path) gets, after its
local-cache preamble:
    #if defined(XV_NATIVE_AIM_BLEND) && XV_NATIVE_AIM_BLEND
        { extern int xv_native_aim_blend(xctx *); if (xv_native_aim_blend(c)) return; }
    #endif
The native (recomp/kernel/xk_native_aim_blend.c) returns 0 when XV_NATIVE_AIM_BLEND=0 (default), when it declines a
call (compressed animation, early exit, ...) and while its verify mode re-runs this body, so the guest code below the
hook stays the reference implementation."""
import re, sys, glob
FN = '000A39B0'
ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
HOOK = ('#if defined(XV_NATIVE_AIM_BLEND) && XV_NATIVE_AIM_BLEND\n'
        '    { extern int xv_native_aim_blend(xctx *); if (xv_native_aim_blend(c)) return; }   /* recomp/kernel/xk_native_aim_blend.c */\n'
        '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end())
        if 'xv_native_aim_blend(c)' in s[m.end():end]: print(f'{f}: hook already present'); continue
        i = s.index(ANCHOR, m.end())
        if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{FN}')
        i += len(ANCHOR)
        s = s[:i] + HOOK + s[i:]
        open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
