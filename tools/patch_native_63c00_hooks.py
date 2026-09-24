#!/usr/bin/env python3
"""Install the XV_NATIVE_63C00 entry hook into a stage's hand-maintained shards (idempotent).
Usage: patch_native_63c00_hooks.py <stage>/recomp

f_00063C00 (the lens-flare visibility test under f_00062240/f_00060560) gets, after its local-cache preamble (after the
x87-regs locals in an --x87-regs shard, so the memory-lowering copy is covered too):
    #if defined(XV_NATIVE_63C00) && XV_NATIVE_63C00
        { extern int xv_native_63c00(xctx *); if (xv_native_63c00(c)) return; }
    #endif
The native (recomp/kernel/xk_native_63c00.c) returns 0 when XV_NATIVE_63C00=0 (default), for inputs it declines, and
while its verify/timing mode re-runs this body, so the guest code below the hook stays the reference implementation."""
import re, sys, glob
FN = '00063C00'
ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
HOOK = ('#if defined(XV_NATIVE_63C00) && XV_NATIVE_63C00\n'
        '    { extern int xv_native_63c00(xctx *); if (xv_native_63c00(c)) return; }   /* recomp/kernel/xk_native_63c00.c */\n'
        '#endif\n')
def main():
    if len(sys.argv) != 2: sys.exit(__doc__)
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end())
        if 'xv_native_63c00(c)' in s[m.end():end]: print(f'{f}: hook already present'); continue
        i = s.find(ANCHOR, m.end())
        if i < 0 or i > end: sys.exit(f'{f}: preamble anchor not found inside f_{FN}')
        if s.find('\nL_%s:' % FN, m.end()) < i: sys.exit(f'{f}: anchor is not in the preamble of f_{FN}')
        i += len(ANCHOR)
        s = s[:i] + HOOK + s[i:]
        open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
