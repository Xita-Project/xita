#!/usr/bin/env python3
"""Install the XV_NATIVE_1721B0 entry hook into a stage's shards (idempotent).
Usage: patch_native_1721b0_hooks.py <stage>/recomp

f_00088E90 (the BSP segment cast under f_001721B0 / f_001731D0 and seven other callers, `ret 14h`) gets, first thing
after its locals:
    #if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0
        { extern int xv_native_1721b0_ray(xctx *); if (xv_native_1721b0_ray(c)) return; }
    #endif
xv_native_1721b0_ray (recomp/kernel/xk_native_1721b0.c) returns 0 - the translated body runs - when XV_NATIVE_1721B0=0
(default, unless XV_NATIVE_1721B0_TIME=1: then it times the translation itself), when the native declines the layout,
and for its own call of the translation (verify mode, timing); otherwise it has run the whole cast (native, or verify:
both, the guest's result kept) and the body returns at once."""
import re, sys, glob
FN = '00088E90'
ANCHOR = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
HOOK = ('#if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0\n'
        '    { extern int xv_native_1721b0_ray(xctx *); if (xv_native_1721b0_ray(c)) return; }   /* recomp/kernel/xk_native_1721b0.c */\n'
        '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read()
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1; end = s.index('\n}\n', m.end())
        if 'xv_native_1721b0_ray(c)' in s[m.end():end]: print(f'{f}: f_{FN} hook already present'); continue
        i = s.index(ANCHOR, m.end())
        if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{FN}')
        i += len(ANCHOR)
        if not s[i:].startswith('    uint32_t fk_a = 0, fk_b = 0, fk_r = 0;'): sys.exit(f'{f}: unexpected preamble in f_{FN}')
        s = s[:i] + HOOK + s[i:]; open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
