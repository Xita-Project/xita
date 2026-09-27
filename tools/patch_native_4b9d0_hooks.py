#!/usr/bin/env python3
"""Install the XV_NATIVE_4B9D0 hooks into a stage (idempotent).
Usage: patch_native_4b9d0_hooks.py <stage>/recomp

1. The BSP sphere query under f_0004B9D0's collision move runs as the fused guest function query_fused_172c95_171f94
   (recomp/query_fusion.c, generated), which recomp/kernel/xk_query_reuse.c calls at five sites (declined / busy /
   unlocked lanes / original / failed capture). The hook is one edit in xk_query_reuse.c, right after that function's
   extern declaration, inside `#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0`: the call sites below it are renamed
   to xv_native_4b9d0_query (recomp/kernel/xk_native_4b9d0.c), which runs the fused guest query itself when
   XV_NATIVE_4B9D0=0 (default), the native when 2, and both with a comparison when 1.
2. The solver's feature test f_000864C0 runs inside the fused solver (recomp/solver_fusion.c, generated: the 170CD1
   call jumps into its fused copy). Three edits there, all inside `#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0`:
   the declarations; at the call, when xv_native_4b9d0_features_on() the registers are published and
   xv_native_4b9d0_features(guest) runs the call (native or verify; it returns 0 when it declines, and the fused code
   runs as before), else xv_native_4b9d0_features_t0() stamps the time for XV_NATIVE_4B9D0_TIME; after the
   continuation label xv_native_4b9d0_features_t1() books it. With XV_NATIVE_4B9D0=0 the fused path is unchanged but
   for the two calls (a thread-local store when timing is off)."""
import sys
from pathlib import Path

ANCHOR = 'extern void query_fused_172c95_171f94(xctx *);\n'
HOOK = ('#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0\n'
        '/* recomp/kernel/xk_native_4b9d0.c: the native BSP sphere query (f_00088110 subtree) in place of the fused guest\n'
        ' * query at every call below; it runs query_fused_172c95_171f94 itself when XV_NATIVE_4B9D0=0 (default). */\n'
        'void xv_native_4b9d0_query(xctx *);\n'
        '#define query_fused_172c95_171f94 xv_native_4b9d0_query\n'
        '#endif\n')
CALL = 'query_fused_172c95_171f94(c)'

S_DECL = 'void f_000864C0(xctx *);\n'
S_DECL_HOOK = ('#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0\n'
               '/* recomp/kernel/xk_native_4b9d0.c: the native feature test (f_000864C0 subtree) at the 170CD1 call */\n'
               'int xv_native_4b9d0_features_on(void);\nint xv_native_4b9d0_features(xctx *);\n'
               'void xv_native_4b9d0_features_t0(void);\nvoid xv_native_4b9d0_features_t1(void);\n'
               '#endif\n')
S_CALL = ('    /* 00170CD1  call 000864C0h */\n'
          '    X_PUSH32(0x170CD6u);\n'
          '    if(depth==4) {\n')
S_CALL_HOOK = ('    /* 00170CD1  call 000864C0h */\n'
               '    X_PUSH32(0x170CD6u);\n'
               '#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0\n'
               '    if (xv_native_4b9d0_features_on()) { NS_PUBLISH(); if (xv_native_4b9d0_features(guest)) { NS_RELOAD(); goto NS_CONT_1; } }\n'
               '    xv_native_4b9d0_features_t0();\n'
               '#endif\n'
               '    if(depth==4) {\n')
S_CONT = '        goto NS_ENTRY_000864C0;\n    }\nNS_CONT_1: ;\n'
S_CONT_HOOK = ('        goto NS_ENTRY_000864C0;\n    }\nNS_CONT_1: ;\n'
               '#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0\n'
               '    xv_native_4b9d0_features_t1();\n'
               '#endif\n')

def patch_solver(text):
    """Install all three guarded solver edits, or reject partial/drifted input."""
    if 'xv_native_4b9d0_features' in text:
        if any(text.count(anchor) != 1 for anchor in (S_DECL_HOOK, S_CALL_HOOK, S_CONT_HOOK)):
            raise ValueError('partial or duplicate native solver hooks')
        return text
    if any(text.count(anchor) != 1 for anchor in (S_DECL, S_CALL, S_CONT)):
        raise ValueError('native solver call-site anchors missing or ambiguous')
    return (text.replace(S_DECL, S_DECL + S_DECL_HOOK, 1)
                .replace(S_CALL, S_CALL_HOOK, 1).replace(S_CONT, S_CONT_HOOK, 1))

def main():
    recomp = Path(sys.argv[1])
    f = recomp / 'kernel' / 'xk_query_reuse.c'
    s = f.read_text()
    if 'xv_native_4b9d0_query' in s: print(f'{f}: hook already present')
    else:
        if s.count(ANCHOR) != 1: sys.exit(f'{f}: declaration anchor not found once')
        head, tail = s.split(ANCHOR)
        n = tail.count(CALL)
        if n < 1: sys.exit(f'{f}: no call of the fused query after the declaration')
        f.write_text(head + ANCHOR + HOOK + tail)
        print(f'{f}: hook installed ({n} call sites)')
    f = recomp / 'solver_fusion.c'
    s = f.read_text()
    patched = patch_solver(s)
    if patched != s: f.write_text(patched)
    print(f'{f}: solver hooks verified')
if __name__ == '__main__': main()
