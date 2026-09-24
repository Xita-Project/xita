#!/usr/bin/env python3
"""Install the XV_CACHE_DEFER entry hooks (recomp/kernel/xk_cache_defer.c) into a stage's shards (idempotent).
Usage: patch_cache_defer_hooks.py <stage>/recomp

f_00032A70 and f_00032510 (the resource-cache allocate + request + queue paths) and f_000A65A0 (block free) get, first thing after their locals:
    #if defined(XV_CACHE_DEFER) && XV_CACHE_DEFER
        { extern int xv_cache_defer(xctx *, uint32_t); if (xv_cache_defer(c, 0x<fn>u)) return; }
    #endif
xv_cache_defer returns 0 (the guest body runs) except on the scene helper during an overlapped scene, where it queues
the request, leaves the function's own failure result and pops the frame."""
import re, sys, glob
FNS = ('00032A70', '00032510', '000A65A0')
ANCHOR = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
def hook(fn):
    return ('#if defined(XV_CACHE_DEFER) && XV_CACHE_DEFER\n'
            '    { extern int xv_cache_defer(xctx *, uint32_t); if (xv_cache_defer(c, 0x%su)) return; }   /* recomp/kernel/xk_cache_defer.c */\n'
            '#endif\n' % fn)
def main():
    root = sys.argv[1]; found = {}
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); changed = False
        for fn in FNS:
            m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            found[fn] = found.get(fn, 0) + 1; end = s.index('\n}\n', m.end())
            if 'xv_cache_defer(c, 0x%su)' % fn in s[m.end():end]: print(f'{f}: f_{fn} hook already present'); continue
            i = s.index(ANCHOR, m.end())
            if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{fn}')
            i += len(ANCHOR); s = s[:i] + hook(fn) + s[i:]; changed = True; print(f'{f}: hook installed in f_{fn}')
        if changed: open(f, 'w').write(s)
    bad = [fn for fn in FNS if found.get(fn) != 1]
    if bad: sys.exit('expected exactly one definition of ' + ', '.join('f_' + fn for fn in bad))
if __name__ == '__main__': main()
