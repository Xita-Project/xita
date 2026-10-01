#!/usr/bin/env python3
"""Diagnostic: cold/warm timing probe for the native BSP sphere query (recomp/kernel/xk_native_4b9d0.c).

XV_NATIVE_4B9D0_WARM=K (native mode 2 only): every K-th native query runs twice on the same state - first with the
write journal (timed, "cold": the BSP data as the tick left the caches), then every journaled write is undone in
reverse order and the context restored, and the query runs again (journaled, timed, "warm": the same lines now
cached). The second run's result is kept; guest memory and context end exactly as after one run. The report line
gains "warm-probe N cold X warm Y us/call". The difference bounds what better data locality could save; it is a
measurement only (the probe doubles the cost of the probed calls). Default 0 (off). Exact-match patch; refuses drift.

usage: patch_native_4b9d0_warm_probe.py <stage-root>
"""
import sys
from pathlib import Path

p = Path(sys.argv[1]) / 'recomp/kernel/xk_native_4b9d0.c'
t = p.read_text()
if 'XV_NATIVE_4B9D0_WARM' in t.split('/* xk_native_4b9d0.c', 1)[1].split('#include "xk.h"', 1)[1]:
    raise SystemExit('already patched')

pairs = [
    ('static unsigned n4_counter[N4_COUNTERS], n4_mismatch_total;\nstatic uint64_t n4_native_ns, n4_guest_ns;',
     'static unsigned n4_counter[N4_COUNTERS], n4_mismatch_total;\nstatic uint64_t n4_native_ns, n4_guest_ns;\n'
     '/* XV_NATIVE_4B9D0_WARM=K: cold/warm probe of every K-th native query (tools/patch_native_4b9d0_warm_probe.py) */\n'
     'static unsigned n4_warm_every = ~0u, n4_warm_tick, n4_warm_n, n4_warm_fail;\nstatic uint64_t n4_cold_ns, n4_warm_ns;'),
    ('    if (mode == 1) { n4_verify(c, timed, guest_query); return; }\n    n4q s;\n',
     '    if (mode == 1) { n4_verify(c, timed, guest_query); return; }\n'
     '    if (__builtin_expect(n4_warm_every == ~0u, 0)) { const char *e = getenv("XV_NATIVE_4B9D0_WARM"); n4_warm_every = e ? (unsigned)atoi(e) : 0u; }\n'
     '    if (n4_warm_every && ++n4_warm_tick >= n4_warm_every) { n4_warm_tick = 0; n4_warm_probe(c); return; }\n'
     '    n4q s;\n'),
    ('/* The hook: in place of query_fused_172c95_171f94(c) (recomp/kernel/xk_query_reuse.c). */',
     '/* Cold/warm probe (diagnostic): run, undo from the journal, run again; keep the second result. */\n'
     'static void n4_warm_probe(xctx *c)\n'
     '{\n'
     '    const xctx before = *c;\n'
     '    n4q s;\n'
     '    n4_j.n = 0; n4_j.overflow = 0;\n'
     '    const uint64_t t0 = n4_ns();\n'
     '    n4_query(c, &s, 1);\n'
     '    const uint64_t t1 = n4_ns();\n'
     '    if (n4_j.overflow) { __atomic_fetch_add(&n4_warm_fail, 1, __ATOMIC_RELAXED); n4_budget(c, s.be); n4_count(&s); return; }\n'
     '    { const n4_mem *m = &s.m; for (unsigned i = n4_j.n; i-- > 0;) memcpy(N4_P(n4_j.e[i].addr), n4_j.e[i].old, n4_j.e[i].size); }\n'
     '    *c = before;\n'
     '    n4_j.n = 0; n4_j.overflow = 0;\n'
     '    const uint64_t t2 = n4_ns();\n'
     '    n4_query(c, &s, 1);\n'
     '    const uint64_t t3 = n4_ns();\n'
     '    n4_budget(c, s.be); n4_count(&s);\n'
     '    __atomic_fetch_add(&n4_cold_ns, t1 - t0, __ATOMIC_RELAXED); __atomic_fetch_add(&n4_warm_ns, t3 - t2, __ATOMIC_RELAXED);\n'
     '    __atomic_fetch_add(&n4_warm_n, 1, __ATOMIC_RELAXED);\n'
     '}\n\n'
     '/* The hook: in place of query_fused_172c95_171f94(c) (recomp/kernel/xk_query_reuse.c). */'),
    ('                 (double)guest_ns / 1000.0 / n[N4_TIMED_GUEST], n[N4_TIMED_GUEST]);\n',
     '                 (double)guest_ns / 1000.0 / n[N4_TIMED_GUEST], n[N4_TIMED_GUEST]);\n'
     '    {   const unsigned wn = __atomic_exchange_n(&n4_warm_n, 0u, __ATOMIC_RELAXED), wf = __atomic_exchange_n(&n4_warm_fail, 0u, __ATOMIC_RELAXED);\n'
     '        const uint64_t cn = __atomic_exchange_n(&n4_cold_ns, 0, __ATOMIC_RELAXED), wns = __atomic_exchange_n(&n4_warm_ns, 0, __ATOMIC_RELAXED);\n'
     '        if (wn) snprintf(timing + strlen(timing), sizeof timing - strlen(timing), "; warm-probe %u cold %.2f warm %.2f us/call fail %u",\n'
     '                         wn, (double)cn / 1000.0 / wn, (double)wns / 1000.0 / wn, wf); }\n'),
    ('    if (!n[N4_CALLS] && !n[N4_TIMED_GUEST] && !n[N4_DECLINED]) return;\n    char timing[112] = "";',
     '    if (!n[N4_CALLS] && !n[N4_TIMED_GUEST] && !n[N4_DECLINED]) return;\n    char timing[192] = "";'),
]
for old, new in pairs:
    if t.count(old) != 1:
        raise SystemExit(f'expected exactly one match for:\n{old[:120]}')
    t = t.replace(old, new)
p.write_text(t)
print('patched', p)
