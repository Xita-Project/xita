#!/usr/bin/env python3
"""Route f_0002B460's obstruction ray through xv_sound_ray (recomp/kernel/xk_sound_obstruction.c) in a stage's shards.
Usage: patch_sound_obstruction_hook.py <stage>/recomp          (idempotent; also removes the older entry hook)

Inside f_0002B460 the call at 2B549
    X_PUSH32(0x2B54Eu);
    f_001721B0(c);
becomes
    X_PUSH32(0x2B54Eu);
#if defined(XV_SOUND_OBSTRUCTION_HOOK) && XV_SOUND_OBSTRUCTION_HOOK
    { extern void xv_sound_ray(xctx *); xv_sound_ray(c); }   /* recomp/kernel/xk_sound_obstruction.c */
#else
    f_001721B0(c);
#endif
xv_sound_ray calls f_001721B0 unless the env knob is on and a ray with nearly the same ends was cast recently."""
import re, sys, glob
FN = '0002B460'
OLD_ENTRY = ('#if defined(XV_SOUND_OBSTRUCTION_HOOK) && XV_SOUND_OBSTRUCTION_HOOK\n'
             '    { extern int xv_sound_obstruction(xctx *); if (xv_sound_obstruction(c)) return; }   /* recomp/kernel/xk_sound_obstruction.c */\n'
             '#endif\n')
CALL = '    X_PUSH32(0x2B54Eu);\n    f_001721B0(c);\n'
NEW = ('    X_PUSH32(0x2B54Eu);\n'
       '#if defined(XV_SOUND_OBSTRUCTION_HOOK) && XV_SOUND_OBSTRUCTION_HOOK\n'
       '    { extern void xv_sound_ray(xctx *); xv_sound_ray(c); }   /* recomp/kernel/xk_sound_obstruction.c */\n'
       '#else\n'
       '    f_001721B0(c);\n'
       '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read()
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1; end = s.index('\n}\n', m.end()); body = s[m.end():end]
        body = body.replace(OLD_ENTRY, '')
        if 'xv_sound_ray(c)' in body: print(f'{f}: f_{FN} already routed')
        else:
            n = body.count(CALL)
            if n == 0: sys.exit(f'{f}: the 2B549 call to f_001721B0 was not found in f_{FN}')
            body = body.replace(CALL, NEW); print(f'{f}: {n} call site(s) routed in f_{FN}')
        s = s[:m.end()] + body + s[end:]; open(f, 'w').write(s)
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
