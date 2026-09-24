#!/usr/bin/env python3
"""Install the XV_SOUND_OBSTRUCTION entry hook (recomp/kernel/xk_sound_obstruction.c) into a stage's shards (idempotent).
Usage: patch_sound_obstruction_hook.py <stage>/recomp

f_0002B460 (a looping sound's obstruction ray + gain, `ret 4`) gets, first thing after its locals:
    #if defined(XV_SOUND_OBSTRUCTION_HOOK) && XV_SOUND_OBSTRUCTION_HOOK
        { extern int xv_sound_obstruction(xctx *); if (xv_sound_obstruction(c)) return; }
    #endif
xv_sound_obstruction returns 0 (the guest body runs) unless the env knob is on and the sound and its listener have not
moved since the last computation, in which case it pops the frame and the sound keeps the last computed values."""
import re, sys, glob
FN = '0002B460'
ANCHOR = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
HOOK = ('#if defined(XV_SOUND_OBSTRUCTION_HOOK) && XV_SOUND_OBSTRUCTION_HOOK\n'
        '    { extern int xv_sound_obstruction(xctx *); if (xv_sound_obstruction(c)) return; }   /* recomp/kernel/xk_sound_obstruction.c */\n'
        '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read()
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1; end = s.index('\n}\n', m.end())
        if 'xv_sound_obstruction(c)' in s[m.end():end]: print(f'{f}: f_{FN} hook already present'); continue
        i = s.index(ANCHOR, m.end())
        if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{FN}')
        i += len(ANCHOR); s = s[:i] + HOOK + s[i:]; open(f, 'w').write(s); print(f'{f}: hook installed in f_{FN}')
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
