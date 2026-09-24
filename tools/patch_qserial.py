#!/usr/bin/env python3
"""Route the object-query serial globals through X_QS8/X_QS32 (recomp/kernel/xk_qserial.h) in every function that uses
them, except the bodies the query-fusion generator validates by text (f_00171F10 and its chunk f_00172078, which the
scene helper never runs). Idempotent. Usage: patch_qserial.py <stage>/recomp

Each patched function is preceded by `#include "kernel/xk_qserial.h"` (include-guarded) inside
`#if defined(XV_QSERIAL) && XV_QSERIAL`, and its accesses are rewritten under the same condition by duplicating the
body is avoided: the macros themselves fall back to X_G when XV_QSERIAL is off at build time (see below)."""
import re, sys, glob
SKIP = {'00171F10', '00172078'}
ADDR = r'0x(?:2D2FA9|2D2FAC|2FC684)u'
PATS = [(re.compile(r'X_IMG32\((%s)\)' % ADDR), r'X_QS32(\1)'),
        (re.compile(r'X_IMG8\((%s)\)' % ADDR), r'X_QS8(\1)'),
        (re.compile(r'X_M32\((\(\(c->r\[[0-7]\]\*4\)\+0x2D2FB0u\)|\(c->r\[[0-7]\]\+0x2D2FB0u\))\)'), r'X_QS32(\1)')]
INC = ('#if defined(XV_QSERIAL) && XV_QSERIAL\n#include "kernel/xk_qserial.h"\n#else\n'
       '#ifndef X_QS32\n#define X_QS8(a) X_IMG8(a)\n#define X_QS32(a) (*(xu32_u *)X_G(a))\n#endif\n#endif\n')
def main():
    root = sys.argv[1]; total = 0; fns = []
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); out = []; pos = 0; changed = False
        for m in re.finditer(r'^void f_([0-9A-F]{8})\(xctx \*restrict c\)\n\{\n', s, re.M):
            fn = m.group(1); end = s.index('\n}\n', m.end()) + 3
            body = s[m.start():end]
            if fn in SKIP: continue
            nb = body; n = 0
            for rx, rep in PATS: nb, k = rx.subn(rep, nb); n += k
            if not n: continue
            out.append(s[pos:m.start()]); out.append(INC if INC not in s[max(0, m.start() - len(INC)):m.start()] else ''); out.append(nb); pos = end
            total += n; fns.append(fn); changed = True
        if changed: out.append(s[pos:]); open(f, 'w').write(''.join(out))
    left = sum(len(re.findall(r'X_IMG32\(%s\)|X_IMG8\(%s\)' % (ADDR, ADDR), open(f).read())) for f in glob.glob(root + '/code_*.c'))
    print(f'qserial: {total} accesses in {len(fns)} functions routed; {left} direct accesses left (in {", ".join(sorted(SKIP))})')
if __name__ == '__main__': main()
