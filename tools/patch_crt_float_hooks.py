#!/usr/bin/env python3
"""Install the XV_NATIVE_CRT_FLOAT entry hooks into a stage's hand-maintained shards (idempotent).
Usage: patch_crt_float_hooks.py <stage>/recomp   (f_0001EC1F -> 0, f_0001EABA -> 1, f_00019E7B -> 2; see recomp/kernel/xk_crt_float.c)"""
import re, sys, glob
HOOKS = {'0001EC1F': 0, '0001EABA': 1, '00019E7B': 2}
ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
def main():
    root = sys.argv[1]; done = 0
    for fn, idx in HOOKS.items():
        for f in glob.glob(root + '/code_*.c'):
            s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            if 'xv_native_crt_float(c, %d)' % idx in s: done += 1; break
            i = s.index(ANCHOR, m.end()) + len(ANCHOR)
            s = s[:i] + '#if defined(XV_NATIVE_CRT_FLOAT) && XV_NATIVE_CRT_FLOAT\n    { extern int xv_native_crt_float(xctx *, unsigned); if (xv_native_crt_float(c, %d)) return; }   /* recomp/kernel/xk_crt_float.c */\n#endif\n' % idx + s[i:]
            open(f, 'w').write(s); done += 1; break
    print(f'crt-float hooks present: {done}/3')
if __name__ == '__main__': main()
