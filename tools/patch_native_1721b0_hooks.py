#!/usr/bin/env python3
"""Install the XV_NATIVE_1721B0 hook into a stage's shards (idempotent; upgrades the first, in-body form).
Usage: patch_native_1721b0_hooks.py <stage>/recomp

f_00088E90 (the BSP segment cast under f_001721B0 / f_001731D0 and seven other callers, `ret 14h`) becomes a wrapper in
front of its translated body, which is renamed f_00088E90_body:

    #if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0
    void f_00088E90_body(xctx *restrict c);
    void f_00088E90(xctx *restrict c) { extern int xv_native_1721b0_ray(xctx *); if (!xv_native_1721b0_ray(c)) f_00088E90_body(c); }
    #define f_00088E90 f_00088E90_body
    #endif
    void f_00088E90(xctx *restrict c)          <- the translated body, compiled as f_00088E90_body under the flag
    { ... }
    #if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0
    #undef f_00088E90
    #endif

xv_native_1721b0_ray (recomp/kernel/xk_native_1721b0.c) returns 0 - the body runs - when XV_NATIVE_1721B0=0 (default)
without XV_NATIVE_1721B0_TIME, and when the native declines the layout; otherwise it has run the whole cast (native;
verify: both, the guest's result kept; timing: the body, timed). It calls f_00088E90_body itself for the translation,
so no flag or thread-local state tells its own call from a caller's. Without the build flag the shard is unchanged in
effect (the body keeps its name)."""
import re, sys, glob
FN = '00088E90'
PRE = ('#if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0\n'
       '/* recomp/kernel/xk_native_1721b0.c: the native cast in front of the translated body (renamed f_00088E90_body), which\n'
       ' * the hook calls itself when it declines, verifies or times */\n'
       'void f_00088E90_body(xctx *restrict c);\n'
       'void f_00088E90(xctx *restrict c) { extern int xv_native_1721b0_ray(xctx *); if (!xv_native_1721b0_ray(c)) f_00088E90_body(c); }\n'
       '#define f_00088E90 f_00088E90_body\n'
       '#endif\n')
POST = ('#if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0\n'
        '#undef f_00088E90\n'
        '#endif\n')
OLD_HOOK = ('#if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0\n'
            '    { extern int xv_native_1721b0_ray(xctx *); if (xv_native_1721b0_ray(c)) return; }   /* recomp/kernel/xk_native_1721b0.c */\n'
            '#endif\n')
def main():
    root = sys.argv[1]; found = 0
    for f in sorted(glob.glob(root + '/code_*.c')):
        s = open(f).read()
        m = re.search(r'^void f_%s\(xctx \*restrict c\)\n\{\n' % FN, s, re.M)
        if not m: continue
        found += 1
        end = s.index('\n}\n', m.end()) + 3
        body = s[m.start():end]
        upgraded = OLD_HOOK in body
        if upgraded: body = body.replace(OLD_HOOK, '')            # the first (in-body, thread-local flag) form
        if s[:m.start()].endswith(PRE) and s[end:].startswith(POST) and not upgraded:
            print(f'{f}: f_{FN} wrapper already present'); continue
        head, tail = s[:m.start()], s[end:]
        if not head.endswith(PRE): head += PRE
        if not tail.startswith(POST): tail = POST + tail
        s = head + body + tail
        open(f, 'w').write(s)
        print(f'{f}: wrapper installed in front of f_{FN}' + (' (the in-body hook of the first version removed)' if upgraded else ''))
    if found != 1: sys.exit(f'expected exactly one definition of f_{FN}, found {found}')
if __name__ == '__main__': main()
