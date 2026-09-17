# Rejected residual collision edge/face localization

The private `8709A..872BA` prototype is **not admitted for integration**. It
preserves the existing query route and helpers, but every tested complete query
executes more ARM instructions than the retained fused baseline. No production
hook, build flag, startup setting, math header, transaction or solver is changed.

This work is based on `7301a1bda8e3299a9921265eac63279a580872e2` in the isolated
`work/2026-09-17-query-edge-face` branch. It is separate from the query primitive
inlining experiment; their results have not been combined.

## Experiment and boundary

`tools/prototype_collision_edge_face.py` generates the complete owned-image
query oracle, then changes only the candidate's residual `86F50` interval after
`L_0008709A` and before the first `872BA` epilogue. The original vertex pass,
`B0CB0` helper, typed BSP traversal, guest stack stores and output order remain.
The generated original bodies and generic interior entries are unchanged.

The first variant keeps the eight x87 slots in a local array. The second proves
from the exact generated interval that every branch, loop target and child
boundary has zero net x87 stack-depth change, then uses four fixed local slots
(the only slots written by this interval). Both publish x87 state before the
real `87103 -> B0CB0` child and before an exhausted original backedge budget,
then reload after the callback. The intended fallback resumes the selected
original loop target or post-child `87108`, with no instruction replay.

The four distinct backedges are `87133 -> 87120`, `87162 -> 870A0`,
`87281 -> 871C1` and `872A6 -> 87294`; the emitter contains two copies of
`87162`. Generation checks the owned image SHA, exact region SHA, labels,
child-call inventory, five emitted preempt operations and absence of live
emitter flag caches. Python `-O`, instruction drift and an output path inside
the checkout are rejected. Guest memory reads/stores are kept in their original
order, with existing captured/global mapping rules. No geometry cache or alias
assumption is introduced. Subtraction uses the already-qualified ordered VFP
primitive to prevent multiplication/subtraction contraction.

These are design properties of an unfinished experiment, **not a completed
callback/alias/exceptional-FP correctness qualification**.

## Early cost gate

`tools/test_collision_edge_face_cost.py` compiles the separate query TU with the
actual retained production compiler flags. Its baseline `.text` is byte-for-byte
identical to the retained production query object. It links the unchanged
retained `code_013.o` and `code_016.o`, existing control objects with current
helpers enabled, and the complete `88110` oracle. No helper-inlining variant is
included. Generic objects are not rebuilt or changed.

All 14 cost cases pass final full-context, entire 8 MiB guest-memory, both page
tables, root identity, observer counters and full ARM FPSCR comparisons against
the original generic query. Cases cover shallow/deeper trees, rejection,
ordinary/surface-heavy and node-heavy queries. They use budget 100000 and
ordinary finite geometry. Seven of these cases also passed each variant in the
initial compact oracle build. The 14 retained-object cases are the cost evidence:

| Query fixture | Baseline | Dynamic slots | Fixed slots |
| --- | ---: | ---: | ---: |
| shallow, ordinary | 13,739 | 14,700 (+7.0%) | 14,781 (+7.6%) |
| shallow, four surfaces | 37,560 | 41,299 (+10.0%) | 40,942 (+9.0%) |
| shallow, rejection | 728 | 739 (+11 instructions) | 741 (+13 instructions) |
| depth 16, ordinary | 225,929 | 240,765 (+6.6%) | 242,226 (+7.2%) |
| depth 16, four surfaces | 599,370 | 658,114 (+9.8%) | 653,827 (+9.1%) |
| depth 16, projected faces | 234,875 | 241,431 (+2.8%) | 239,979 (+2.2%) |
| depth 16, node-heavy | 1,638,809 | 1,758,285 (+7.3%) | 1,770,546 (+8.0%) |

`.text` grows from 33,404 bytes to 41,372/41,524. The query's local native stack
frame grows from 2,152 bytes to 2,416/2,360 (dynamic/fixed); the existing adapter
is unchanged. Instruction counts are neither hardware cycles nor an FPS result.
Firmware copies in the runner are modeled, as in the retained oracle.

The local-slot representation adds publication and reload work at every edge's
child call and increases register/stack pressure. Code growth also makes GCC
outline additional existing flag/cleanup helpers: in the shallow ordinary case,
these add 210 executed instructions, while the fused body itself adds 751/832.
Existing outlined float-load/store instruction totals are unchanged in that
case. Thus simply replacing dynamic x87 indices with scalar slots does not
recover the added cost. This does not reject a future larger typed edge/face
kernel with a different child-state boundary; it rejects this implementation.

## Scope limits and reproduction

The cost gate failed, so qualification stops here. No new yield mutation,
physical-alias, cross-page, exceptional NaN, PC53, overflow, host sanitizer or
full `172BF0` suite was run for this change. Passing the finite complete-query
cases is insufficient to deploy the experiment. In particular, local state
across continuation jumps and exact observer publication still require the
expanded tests if this representation is ever revived. There is no runtime or
hardware change to roll back.

Private evidence is under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/query-edge-face-prototype`:
`production-cost/{build.json,result.json}`, `generation-gates.json`, the compact
oracle results, `report.md` and `receipt.json`. Generated game bodies stay there.

Example using the existing private retained stage and oracle:

```sh
B=/home/birchwoodgod/xita-backups/2026-09-13-worker-sizing
E=$B/validation/engine-restructure-20260914T2300Z/direct-cluster-query
PY=/home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/venv/bin/python
$PY tools/test_collision_edge_face_cost.py \
  --xbe /home/birchwoodgod/github/xboxvita/haloce/default.xbe \
  --manifest "$B/parallel-objects-20260914/local/halo_ce_3925/game_manifest.json" \
  --retained-build "$E/query-fusion-startup/build" \
  --compile-log "$E/query-fusion-startup/build.log" \
  --fixture-dir "$E/native-query-interface-combined/final-typed-cost" \
  --out "$E/query-edge-face-prototype/new-production-cost"
```
