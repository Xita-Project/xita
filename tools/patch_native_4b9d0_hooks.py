#!/usr/bin/env python3
"""Install the XV_NATIVE_4B9D0 hook into a stage (idempotent).
Usage: patch_native_4b9d0_hooks.py <stage>/recomp

The BSP sphere query under f_0004B9D0's collision move runs as the fused guest function query_fused_172c95_171f94
(recomp/query_fusion.c, generated), which recomp/kernel/xk_query_reuse.c calls at five sites (declined / busy /
unlocked lanes / original / failed capture). The hook is one edit in xk_query_reuse.c, right after that function's
extern declaration, inside `#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0`: the call sites below it are renamed to
xv_native_4b9d0_query (recomp/kernel/xk_native_4b9d0.c), which runs the fused guest query itself when
XV_NATIVE_4B9D0=0 (default), the native when 2, and both with a comparison when 1. No generated file is edited (the
query-fusion generator owns code_028.c and query_fusion.c)."""
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

def main():
    f = Path(sys.argv[1]) / 'kernel' / 'xk_query_reuse.c'
    s = f.read_text()
    if 'xv_native_4b9d0_query' in s: print(f'{f}: hook already present'); return
    if s.count(ANCHOR) != 1: sys.exit(f'{f}: declaration anchor not found once')
    head, tail = s.split(ANCHOR)
    n = tail.count(CALL)
    if n < 1: sys.exit(f'{f}: no call of the fused query after the declaration')
    f.write_text(head + ANCHOR + HOOK + tail)
    print(f'{f}: hook installed ({n} call sites)')
if __name__ == '__main__': main()
