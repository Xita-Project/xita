#!/usr/bin/env python3
"""Install the XV_OBJTRACE entry hook (recomp/kernel/xk_objtrace.c) at f_0005B4A0 (idempotent).
Usage: patch_objtrace.py <stage>/recomp"""
import re, sys, glob
FN = '0005B4A0'
ANCHOR = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
HOOK = ('#if defined(XV_OBJTRACE) && XV_OBJTRACE\n'
        '    { extern void xv_objtrace_note(uint32_t); xv_objtrace_note(X_M32(c->r[7])); }   /* [edi] = object handle */\n#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1; end = s.index('\n}\n', m.end())
        if 'xv_objtrace_note' in s[m.end():end]: print(f'{f}: hook already present'); continue
        i = s.index(ANCHOR, m.end()) + len(ANCHOR); s = s[:i] + HOOK + s[i:]; open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected one f_{FN}, found {found}')
if __name__ == '__main__': main()
