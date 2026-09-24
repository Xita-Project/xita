#!/usr/bin/env python3
"""Install XV_CACHE_PROBE entry hooks (recomp/kernel/xk_cache_probe.c) into a stage's shards (idempotent).
Usage: patch_cache_probe.py <stage>/recomp

Each listed function gets, first thing after its locals (before a chunk entry's goto):
    #if defined(XV_CACHE_PROBE) && XV_CACHE_PROBE
        { extern void xv_cache_probe(uint32_t); xv_cache_probe(0x<fn>u); }
    #endif
The list is every function that reads or writes the shared resource cache ([2E2D2C] allocator, [2E2D24] requests,
[2E2D28]), the allocator and its free path (A6620, A65A0), the file-request queue (33A20 and its submitters, the file
fiber 33AF0) and the other A6620/A65A0 callers."""
import re, sys, glob
FNS = '''000A6620 000A65A0 00032A70 00032510 00032B00 00032BC0 00032CD0 00032740 000328A0 000326D0 00033A20 00033AF0
0017A750 000353B0 0003542A 00035510 00032561 00114D30 00115423 00115FDF 000729E0 00114940 00114B50
000265A0 000266B6 0002673E 000269D0 00026A04 000264E0 00025A00 00025A40 00025A80 00029590 00026160
000282B0 000282E8 000282ED 00028320 00028396 000283A0 00032B2D 000328B4'''.split()
ANCHOR = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
def hook(fn):
    return ('#if defined(XV_CACHE_PROBE) && XV_CACHE_PROBE\n'
            '    { extern void xv_cache_probe(uint32_t); xv_cache_probe(0x%su); }\n#endif\n' % fn)
def main():
    root = sys.argv[1]; want = set(FNS); done = set()
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); changed = False
        for fn in FNS:
            m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            done.add(fn); end = s.index('\n}\n', m.end())
            if 'xv_cache_probe(0x%su)' % fn in s[m.end():end]: continue
            i = s.index(ANCHOR, m.end())
            if i > end: sys.exit(f'{f}: preamble anchor not found inside f_{fn}')
            i += len(ANCHOR); s = s[:i] + hook(fn) + s[i:]; changed = True
        if changed: open(f, 'w').write(s)
    missing = want - done
    print(f'cache probe: {len(done)} functions hooked' + (f'; not found: {" ".join(sorted(missing))}' if missing else ''))
if __name__ == '__main__': main()
