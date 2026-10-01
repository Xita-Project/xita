#!/usr/bin/env python3
"""Insert the semantic BSP sphere query (tools/n6_query.c.in) into recomp/kernel/xk_native_4b9d0.c.

XV_NATIVE_4B9D0=3 runs it (the exact transliteration on every decline); 4 compares it against the exact native on
live outputs and keeps the exact result. Modes 0-2 are unchanged. Exact-match edits; refuses drift or a repeat.

usage: patch_native_4b9d0_semantic.py <stage-root>
"""
import sys
from pathlib import Path

p = Path(sys.argv[1]) / 'recomp/kernel/xk_native_4b9d0.c'
t = p.read_text()
if 'n6_query' in t:
    raise SystemExit('already patched')
body = (Path(__file__).resolve().parent / 'n6_query.c.in').read_text()
pairs = [
    ('/* ---- verify mode ------------------------------------------------------------------------------------------- */',
     body + '\n/* ---- verify mode ------------------------------------------------------------------------------------------- */'),
    ('        if (mode < 0 || mode > 2) mode = 0;\n        int expected = -1;\n        if (__atomic_compare_exchange_n(&n4_mode_value,',
     '        if (mode < 0 || mode > 4) mode = 0;\n        int expected = -1;\n        if (__atomic_compare_exchange_n(&n4_mode_value,'),
    ('                   mode == 2 ? "native" : mode == 1 ? "verify (native vs fused guest, guest result kept)" : "off");',
     '                   mode == 4 ? "semantic compare (semantic dry vs exact native, exact result kept)" : mode == 3 ? "semantic (exact native on decline)" :\n'
     '                   mode == 2 ? "native" : mode == 1 ? "verify (native vs fused guest, guest result kept)" : "off");'),
    ('    if (mode == 1) { n4_verify(c, timed, guest_query); return; }\n',
     '    if (mode == 1) { n4_verify(c, timed, guest_query); return; }\n'
     '    if (mode == 3 && n6_run(c, timed)) return;\n'
     '    if (mode == 4) { n6_compare(c, timed); return; }\n'),
    ('    XK_LOG("[native-4b9d0] %u frames: calls %u verified %u mismatched %u (total mismatches %u) declined %u journal-fail %u; "',
     '    { const unsigned sc = __atomic_exchange_n(&n6_calls, 0u, __ATOMIC_RELAXED), sd = __atomic_exchange_n(&n6_declined, 0u, __ATOMIC_RELAXED),\n'
     '                     sp = __atomic_exchange_n(&n6_compared, 0u, __ATOMIC_RELAXED), sm = __atomic_exchange_n(&n6_mismatched, 0u, __ATOMIC_RELAXED);\n'
     '      if (sc || sd || sp) XK_LOG("[native-4b9d0] semantic %u frames: ran %u declined %u compared %u mismatched %u (total %u)\\n",\n'
     '                                 frames, sc, sd, sp, sm, __atomic_load_n(&n6_mismatch_total, __ATOMIC_RELAXED)); }\n'
     '    XK_LOG("[native-4b9d0] %u frames: calls %u verified %u mismatched %u (total mismatches %u) declined %u journal-fail %u; "'),
]
for old, new in pairs:
    if t.count(old) != 1:
        raise SystemExit(f'expected exactly one match for:\n{old[:140]}')
    t = t.replace(old, new)
p.write_text(t)
print('patched', p)
