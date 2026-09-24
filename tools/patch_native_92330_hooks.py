#!/usr/bin/env python3
"""Install the XV_NATIVE_92330 hooks into a stage's hand-maintained shards (idempotent).
Usage: patch_native_92330_hooks.py <stage>/recomp

f_00056670 (the light cluster query f_00092330 calls at 925AB; also called from f_0008E970) gets three edits, all
inside `#if defined(XV_NATIVE_92330) && XV_NATIVE_92330`:
  1. after its local-cache preamble:  void *xn92_ = 0;            (verify/timing token, declared before any goto)
  2. just before L_00056670 (after the phase scope, object-math guard, census token and typed worker query):
         if no light-census token is open: if (xv_native_92330(c, &xn92_)) return;
  3. at every `ret 10h` site of the body:  if (xn92_) xv_native_92330_post(c, xn92_);   (after the esp update)
The native (recomp/kernel/xk_native_92330.c) returns 0 when XV_NATIVE_92330=0 (default), so the guest code below
the hook stays the reference implementation; in verify mode it runs the native, undoes it and lets this body run on
the same state, and the post hook compares. Works on the memory-lowered and the --x87-regs stage alike (the query
body is not converted by --x87-regs)."""
import re, sys, glob
FN = '00056670'
DECL_ANCHOR = '    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;\n'
DECL = ('#if defined(XV_NATIVE_92330) && XV_NATIVE_92330\n'
        '    void *xn92_ = 0;   /* recomp/kernel/xk_native_92330.c: verify/timing token */\n'
        '#endif\n')
LABEL = 'L_00056670:\n'
HOOK = ('#if defined(XV_NATIVE_92330) && XV_NATIVE_92330\n'
        '#ifdef XV_LIGHT_QUERY_CENSUS\n'
        '    if (!xv_query_work_.lane)\n'
        '#endif\n'
        '    { extern int xv_native_92330(xctx *, void **); if (xv_native_92330(c, &xn92_)) return; }   /* recomp/kernel/xk_native_92330.c */\n'
        '#endif\n')
RET = '    c->r[4] += 20; return;\n'
POST = ('    c->r[4] += 20;\n'
        '#if defined(XV_NATIVE_92330) && XV_NATIVE_92330\n'
        '    if (xn92_) { extern void xv_native_92330_post(xctx *, void *); xv_native_92330_post(c, xn92_); }\n'
        '#endif\n'
        '    return;\n')

def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end())
        body = s[m.end():end]
        if 'xv_native_92330(c, &xn92_)' in body: print(f'{f}: hooks already present'); continue
        if body.count(DECL_ANCHOR) != 1 or body.count(LABEL) != 1: sys.exit(f'{f}: preamble anchors not found once inside f_{FN}')
        if body.index(DECL_ANCHOR) > body.index(LABEL): sys.exit(f'{f}: unexpected preamble order in f_{FN}')
        nret = body.count(RET)
        if nret < 1: sys.exit(f'{f}: no `ret 10h` site in f_{FN}')
        body = body.replace(DECL_ANCHOR, DECL_ANCHOR + DECL, 1)
        body = body.replace(LABEL, HOOK + LABEL, 1)
        body = body.replace(RET, POST)
        s = s[:m.end()] + body + s[end:]
        open(f, 'w').write(s); print(f'{f}: hooks installed in f_{FN} ({nret} return sites)')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
