#!/usr/bin/env python3
"""Install the XV_NATIVE_EFFECTS hooks into a stage's shards (idempotent).
Usage: patch_native_effects_hooks.py <stage>/recomp

Each hooked function (the list is recomp/kernel/xk_native_effects.c's NX_HOOKS; this tool carries the same list and
checks it against the unit when it sits next to the shards) becomes a wrapper in front of its translated body, which is
renamed f_XXXXXXXX_body:

    #if defined(XV_NATIVE_EFFECTS) && XV_NATIVE_EFFECTS
    void f_0007E530_body(xctx *restrict c);
    extern int xv_native_effects_0007E530(xctx *); void f_0007E530(xctx *restrict c) { if (!xv_native_effects_0007E530(c)) f_0007E530_body(c); }
    #define f_0007E530 f_0007E530_body
    #endif
    void f_0007E530(xctx *restrict c)          <- the translated body, compiled as f_0007E530_body under the flag
    { ... }
    #if defined(XV_NATIVE_EFFECTS) && XV_NATIVE_EFFECTS
    #undef f_0007E530
    #endif

The hook returns 0 (the body runs) when XV_NATIVE_EFFECTS=0 (default) without XV_NATIVE_EFFECTS_TIME, for object-job
contexts and for the declines; it calls f_XXXXXXXX_body itself for verify and timing. Without the build flag the shard
is unchanged in effect. The wrapper line starts with the extern declaration: the Makefile's XV_RENDER_GUEST_SIZE check
counts the lines that start with `void f_0007E530` (one definition expected).
A stage patched by an earlier form of this tool is upgraded (the old wrapper block is replaced; the wrappers of
functions no longer hooked are removed)."""
import re, sys, glob, os
FUNCS = ['0007E530', '0007E420', '00056F20', '00080360', '00011B60', '00011610', '00011BD0']
def pre(fn):
    return ('#if defined(XV_NATIVE_EFFECTS) && XV_NATIVE_EFFECTS\n'
            '/* recomp/kernel/xk_native_effects.c: the native in front of the translated body (renamed f_%s_body) */\n'
            'void f_%s_body(xctx *restrict c);\n'
            'extern int xv_native_effects_%s(xctx *); void f_%s(xctx *restrict c) { if (!xv_native_effects_%s(c)) f_%s_body(c); }\n'
            '#define f_%s f_%s_body\n'
            '#endif\n') % ((fn,) * 8)
def post(fn):
    return '#if defined(XV_NATIVE_EFFECTS) && XV_NATIVE_EFFECTS\n#undef f_%s\n#endif\n' % fn
def main():
    root = sys.argv[1]
    unit = os.path.join(root, 'kernel/xk_native_effects.c')
    if os.path.exists(unit):
        m = re.search(r'#define NX_HOOKS\(X\) \\\n(.*?)\n#define', open(unit).read(), re.S)
        listed = re.findall(r'X\(([0-9A-F]{8}),', m.group(1)) if m else []
        if sorted(listed) != sorted(FUNCS): sys.exit(f'hook list differs from {unit}: {sorted(listed)} vs {sorted(FUNCS)}')
    found = {fn: 0 for fn in FUNCS}
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read(); changed = False
        for fn in FUNCS:
            m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % fn, s, re.M)
            if not m: continue
            found[fn] += 1
            end = s.index('\n}\n', m.end()) + 3
            if s[:m.start()].endswith(pre(fn)) and s[end:].startswith(post(fn)):
                print(f'{f}: f_{fn} wrapper already present'); continue
            head, body, tail = s[:m.start()], s[m.start():end], s[end:]
            if 'xv_native_effects' in body: sys.exit(f'{f}: f_{fn} carries an in-body hook')
            old = re.search(r'#if defined\(XV_NATIVE_EFFECTS\) && XV_NATIVE_EFFECTS\n/\* recomp/kernel/xk_native_effects.c[^\n]*\n(?:[^\n]*\n){3}#endif\n$', head)
            if old: head = head[:old.start()]
            if not head.endswith(pre(fn)): head += pre(fn)
            if not tail.startswith(post(fn)): tail = post(fn) + tail
            s = head + body + tail; changed = True
            print(f'{f}: wrapper installed in front of f_{fn}')
        if changed: open(f, 'w').write(s)
    for f in sorted(glob.glob(root + '/code_*.c')):             # wrappers of functions this version no longer hooks
        s = open(f).read(); n0 = len(s)
        for m in list(re.finditer(r'#if defined\(XV_NATIVE_EFFECTS\) && XV_NATIVE_EFFECTS\n/\* recomp/kernel/xk_native_effects.c: the native in front of the translated body \(renamed f_([0-9A-F]{8})_body\) \*/\n(?:[^\n]*\n){3}#endif\n', s))[::-1]:
            if m.group(1) in FUNCS: continue
            post_ = '#if defined(XV_NATIVE_EFFECTS) && XV_NATIVE_EFFECTS\n#undef f_%s\n#endif\n' % m.group(1)
            k = s.index(post_, m.end()); s = s[:m.start()] + s[m.end():k] + s[k + len(post_):]
            print(f'{f}: wrapper of f_{m.group(1)} removed (no longer hooked)')
        if len(s) != n0: open(f, 'w').write(s)
    bad = [fn for fn, n in found.items() if n != 1]
    if bad: sys.exit(f'expected exactly one definition of each hooked function; not so for {bad}')
if __name__ == '__main__': main()
