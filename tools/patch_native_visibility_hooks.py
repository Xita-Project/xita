#!/usr/bin/env python3
"""Install the XV_NATIVE_VISIBILITY entry hook into a stage's hand-maintained shards (idempotent).
Usage: patch_native_visibility_hooks.py <stage>/recomp

f_00052E10 (the BSP subcluster visibility pass under f_000539C0) gets, after its local-cache preamble:
    #if defined(XV_NATIVE_VISIBILITY) && XV_NATIVE_VISIBILITY
        { extern int xv_native_visibility(xctx *); if (xv_native_visibility(c)) return; }
    #endif
The native (recomp/kernel/xk_native_visibility.c) returns 0 when XV_NATIVE_VISIBILITY=0 (default) or while its
verify mode re-runs this body, so the guest code below the hook stays the reference implementation."""
import re, sys, glob
FN = '00052E10'
ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
HOOK = ('#if defined(XV_NATIVE_VISIBILITY) && XV_NATIVE_VISIBILITY\n'
        '    { extern int xv_native_visibility(xctx *); if (xv_native_visibility(c)) return; }   /* recomp/kernel/xk_native_visibility.c */\n'
        '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end())
        if 'xv_native_visibility(c)' in s[m.end():end]: print(f'{f}: hook already present'); continue
        i = s.index(ANCHOR, m.end())
        if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{FN}')
        i += len(ANCHOR)
        s = s[:i] + HOOK + s[i:]
        open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
