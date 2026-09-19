# Collision query reuse: memory recorder and ARM qualification

Historical qualification note. Subsequent work added [selective hardware
reuse](query-selective-reuse-20260919.md) and [separate record retention](query-retention-20260919.md).
The status below describes the original recorder-only stage.

This is preparation for query reuse, **not an enabled game optimization**.
The Vita remains on `0.2.0-perf.19`; there is no new hardware FPS result.
Halo2 remains parked while Halo CE campaign performance is the priority.

Follow-up: [actual query capture qualification](query-capture-qualification-20260919.md)
now covers the instrumented ARM closure, including full context/memory/FP
comparisons and measured capture overhead. It remains outside production.

The [hardware input census](query-repeat-census-20260919.md) found matching
inputs for 54.5% of selected world collision searches. That establishes an
opportunity to investigate, not permission to reuse their results. A repeated
descriptor pointer does not prove the underlying geometry is unchanged.

## What is implemented

`recomp/kernel/xk_query_memory.{h,c}` records a bounded memory transaction:

* Initial bytes read before the query first writes them.
* Every byte written, including stores equal to the previous value.
* The final values of written bytes and all registered guest-page mappings.

Before replay it checks the arena/page-table roots, sizes, every registered
mapping and exact initial-read bytes. It completes all validation before any
store. Replay writes only recorded bytes and preserves unrelated current data.
Physical aliases share one record; read-after-write is not falsely treated as
an initial input. Bounds errors, capacity overflow, remaps, unknown access and
callbacks invalidate recording. There are no allocations, clocks or global
mutable state in the module.

Storage is 128 blocks of 64 bytes plus 128 mappings: 21,024 bytes per record on
ARM32, 21,040 on the host. Integration needs a bounded persistent pool, not a
large automatic object on a worker stack. The module is not linked into the
game, does not capture accesses automatically, and does not replay CPU state.

## Evidence

Normal and ASan/UBSan tests pass. The independent byte-program oracle covers
unaligned/cross-block accesses, same-value stores, aliases, changed write-only
data, atomic rejection, hash collisions, capacity, remaps and restart. All 16
word masks are exercised in both halves of a block, including an unaligned
arena base.

Fourteen synthetic searches executed the retained perf19 ARM query. Their
traced results were compared with untraced whole-arena/context/FPSCR execution.
The C recorder then consumed their ordered access receipts:

* 66–671 distinct initial-read bytes; 70–481 written bytes.
* 11–33 total 64-byte blocks and 7–15 observed page mappings.
* 3,906 individual dependency-byte rejection checks.
* 336 block/mapping replay rejections checked for no partial writes.
* Exact final memory with changed write-only and unrelated bytes.

These are synthetic footprint measurements, not campaign cache sizing.

The initial validator scanned a 64-bit mask one byte at a time. Actual ARM
instruction execution showed that this erased the benefit on most fixtures.
The implementation now compares native words with exact byte masks and writes
whole words only when all four bytes were originally written.

| Synthetic case | Original full query instructions | Initial memory-only replay | Revised memory-only replay |
|---|---:|---:|---:|
| Split traversal |103,835|63,460|5,222|
| Winding |40,161|48,939|3,429|
| Edge |38,338|48,999|3,459|
| Positive traversal |2,856|30,415|2,403|
| Negative traversal |1,215|21,741|1,109|

All 14 revised ARM memory replays matched the final arena and preserved the
seeded native FPSCR. These counts exclude cache lookup, context/FP replay,
firmware copy cost and real scheduling/cache behavior. They are not timings
or FPS predictions. Short searches have very little remaining margin.

Recording every individual access is still expensive: the split receipt takes
720,319 instructions to record, and winding takes 256,555. This is a receipt
driver, not the integrated instrumented query; it also does not include a
mapping notification on every access. Do not enable recording for every miss.
The next prototype should prefer expensive searches with sustained repeated
keys, and measure how long records survive before admitting a broad cache.

## CPU state findings

An exploratory exit recipe passed 2,016 input perturbations over 112 cold paths
(14 fixtures and all eight x87 stack positions). It covered incoming ST/XMM
payloads, FCW, FSW, native sticky/NZCV bits and lazy arithmetic flags, at the
tested native control setting. The geometry outputs and complete arena were
compared; no game query has been skipped on hardware.

An earlier difference-only recipe failed 448 cases: fields explicitly written
by the final comparison sometimes already equaled their cold input. Both
normal 88110 return paths execute that comparison, so its seven flag-field
writes must be represented even when before/after bytes match. The corrected
recipe includes those writes. ST/XMM masks still need explicit path-level
capture/proof; synthetic sentinels are not a production write-mask mechanism.

Preserve untouched ST/XMM slots and FCW. Guest FSW comparisons preserve and OR
TOP bits rather than replacing them. An arbitrary cold exit cannot reveal TOP
contributions or native sticky exceptions already set on entry. Capture these
effects explicitly or use an exact entry-state key until a general recipe is
qualified. Callbacks, budget exhaustion and unclosed external fallback paths
must retain original execution. The dynamic-object continuation after the
world search still runs normally.

## Capture requirements and next work

Capture must include **all** guest reads, stores and translations: emitted
loads, pre-expanded x87/runtime helpers, typed spans, direct copies and native
fallbacks. A late `X_G` override is insufficient. The memory recorder handles
physical aliases, but cannot infer an omitted access, enforce lifetime/actor
ownership, or know when a callback occurred. Caller guards remain mandatory.

The next step is a gated query adapter with complete access capture, explicit
CPU/FP effects and repeated-key admission. Compare its complete original and
replayed outcomes before hardware deployment, then measure capture cost, hits,
invalidations and ordinary campaign frame time on a fresh launch. Retain the
cumulative perf19 optimizations.

## Reproduction and evidence location

```sh
python3 tools/test_query_memory.py
SANITIZE=1 python3 tools/test_query_memory.py
python3 tools/test_query_memory_trace.py /path/to/trace-split.json
```

Private qualification artifacts are in
`../collision-reuse-fp/`: build/source receipts, FP results (including the
rejected difference-only attempt), ordered synthetic traces, recorder results,
the ARM harness and its byte-loop baseline. The geometry lifetime audit is in
`../collision-geometry-lifetime/`. Generated game code and owned assets remain
outside the repository.

Local Unicorn 2.1.4 memory hooks exposed a Thumb IT-block issue: a false
conditional store could make a subsequent stack adjustment disappear. A
20-byte independent reproducer isolated it. Each accepted trace was therefore
compared against a separate untraced execution; fresh instances alone were
not considered sufficient. This is a limitation of this tracing experiment,
not a Vita crash or a Vita3K performance result.
