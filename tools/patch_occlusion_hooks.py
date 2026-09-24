#!/usr/bin/env python3
"""Install the XV_OCCL hooks (recomp/kernel/xk_occlusion.c) into a stage's shards (idempotent).
Usage: patch_occlusion_hooks.py <stage>/recomp

1. In f_0005B760 the model loop's call (return address 5B7B7)
       X_PUSH32(0x5B7B7u);
       f_0005B4A0(c);
   becomes xv_occl_render(c) under XV_OCCL_HOOK (it calls f_0005B4A0 unless the object is skipped).
2. In f_0005D410, right after the call of f_0005B760 (return address 5D4F1), xv_occl_after_models() records the
   frame's proxies while the depth buffer holds the opaque world."""
import re, sys, glob
CALL = '    X_PUSH32(0x5B7B7u);\n    f_0005B4A0(c);\n'
CALL_NEW = ('    X_PUSH32(0x5B7B7u);\n'
            '#if defined(XV_OCCL_HOOK) && XV_OCCL_HOOK\n'
            '    { extern void xv_occl_render(xctx *); xv_occl_render(c); }   /* recomp/kernel/xk_occlusion.c */\n'
            '#else\n'
            '    f_0005B4A0(c);\n'
            '#endif\n')
AFTER = '    X_PUSH32(0x5D4F1u);\n    f_0005B760(c);\n'
AFTER_NEW = (AFTER + '#if defined(XV_OCCL_HOOK) && XV_OCCL_HOOK\n'
             '    { extern void xv_occl_after_models(void); xv_occl_after_models(); }\n'
             '#endif\n')
def body(s, fn):
    m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
    if not m: return None
    return m.end(), s.index('\n}\n', m.end())
def main():
    root = sys.argv[1]; done = {'0005B760': 0, '0005D410': 0}
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); changed = False
        for fn, old, new, mark in (('0005B760', CALL, CALL_NEW, 'xv_occl_render(c)'), ('0005D410', AFTER, AFTER_NEW, 'xv_occl_after_models()')):
            b = body(s, fn)
            if not b: continue
            a, e = b; txt = s[a:e]; done[fn] += 1
            if mark in txt: print(f'{f}: f_{fn} already hooked'); continue
            n = txt.count(old)
            if n == 0: sys.exit(f'{f}: call site not found in f_{fn}')
            s = s[:a] + txt.replace(old, new) + s[e:]; changed = True; print(f'{f}: {n} site(s) hooked in f_{fn}')
        if changed: open(f, 'w').write(s)
    bad = [fn for fn, n in done.items() if n != 1]
    if bad: sys.exit('expected exactly one definition of ' + ', '.join('f_' + fn for fn in bad))
if __name__ == '__main__': main()
