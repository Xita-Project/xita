# Private semantic vertex-distance prototype

This prototype replaces one arithmetic interval in the caller-specific query's
native `86F50` vertex visitor. It keeps the existing complete query, generic
fallbacks, guards, numerical precision and guest memory operations. It is based
on `7fdca4c` and has no production generator/default/build integration.

The candidate is suitable for independent review, with a **modest** modeled
gain. It is not the proposed whole-query semantic rewrite and does not prove
that x87 state or guest scratch memory can be discarded.

## Representation and exact state

After the unchanged vertex/center loads, the original computes four float32
differences, squares each, shuffles XMM0 twice and sums three coordinates. Two
four-lane loops repeatedly read/write `xctx.xmm`. The candidate keeps those ten
scalar VFP operations in local registers, then writes the exact final state:

* XMM0 = `(squared_z, squared_x, squared_x, squared_y)`.
* XMM1 is unchanged; XMM2.low is the original ordered sum; XMM2.high is unchanged.
* Every other context field is unchanged by this interval.
* The original guest distance store follows in its original position.

All four subtraction/multiplication lanes remain, including padding. No NEON
arithmetic, reassociation, float/double conversion change, guest-store omission,
alias assumption or worker-specific transaction assumption is introduced. There
is no call or backedge inside the replaced interval. The next original preempt
therefore sees the already reconstructed state through the existing original-
pointer publication mechanism. The x87 calculations surrounding it are untouched.

Native exception-enable bits select the original arithmetic/store sequence.
Unicorn clears those enable bits on readback, so this branch is statically
reviewed but **not claimed as dynamically qualified native-trap behavior**.
Seven such unsupported probes are reported separately from passing cases.

`prototype_query_semantic_leaf.py` proves the shuffle permutation and validates
that its transformation changes exactly the one arithmetic interval, keeps its
guest store, and restores the original source exactly when OFF. The retained
baseline `.text` hash is pinned because NaN payload propagation depends on actual
VFP operand order. Changing compiler/baseline requires a fresh review.

## Whole-query cost

The baseline already includes selective `x87_load_f32`/`x87_store_f32` inlining.
The private tool rebuilds it with the retained **production** compiler command
and requires all initialized allocated sections and normalized relocations to
equal the retained object. Generic and fixture objects remain unchanged.

Depth-16 whole-query modeled ARM instruction counts:

| Case | Current inline query | Semantic slice | Change |
|---|---:|---:|---:|
| Ordinary overlap | 220,104 | 214,949 | −2.34% |
| Four surfaces | 582,971 | 569,336 | −2.34% |
| Rejected traversal | 4,631 | 4,631 | unchanged |
| Empty traversal | 1,098 | 1,098 | unchanged |
| Outside vertices | 228,897 | 224,380 | −1.97% |
| Four outside surfaces | 525,249 | 514,972 | −1.96% |
| Combined 3D/2D traversal | 1,583,784 | 1,543,589 | −2.54% |

The corresponding seven depth-1 cases pass; busy cases improve 1.85–2.26%.
All 14 complete queries compare the full 360-byte context, full 8 MiB guest arena,
both page tables, roots, event state and FPSCR against retained generic code.
These are modeled instructions, not hardware cycles or FPS. The helper does not
reduce the ten required arithmetic operations; it reduces context/loop/shuffle
bookkeeping. Larger semantic work is still needed for a larger reduction.

Query text grows 33,624 → 34,140 bytes (+516). Its local native frame grows
2,136 → 2,176 bytes (+40); the adapter remains 24 bytes. Undefined imports are
unchanged. A final rebuild reproduces the exact tested baseline/candidate
allocated sections and relocations. No hardware cache/high-water claim is made.

## Correctness evidence

In addition to the 14 cost cases:

* **33 complete-query cases** cover the prior profiling, expiration, unaligned
  stack, cross-page center, physical center/output alias, root replacement,
  same-table remap, TOP/rounding/FZ/DN/sticky flags and capacity-1 generic fallback
  fixtures. New forced-vertex cases exercise signed zero, fractional values,
  subnormals, minimum normals, overflow, every rounding mode, and physical
  vertex/output aliases. All compare complete state plus ordered guest-write
  traces. The forced-vertex cases assert the semantic instruction interval ran.
* **30 exceptional prefixes through actual retained objects** cover infinity,
  distinct quiet/signaling NaNs and mixed-sign payloads across the FP modes.
  The original full query can fault later on this invalid geometry, so these
  runs stop at the first existing preempt callback after the distance store.
  No new callback or execution-time state mutation is injected. At that real
  observation, original pointer identity, complete context, full arena, both
  page tables, roots, FPSCR and the complete ordered guest-write prefix match
  in the retained generic, retained inline and candidate lanes. The candidate
  semantic interval is explicitly counted. These are prefix checks, not claims
  that nonfinite geometry is supported through complete query return.
* **210 supplemental arithmetic cases** compare complete contexts/FPSCR against
  a per-instruction reference whose additions use the retained VFP operand order.
  This supplements the retained-object prefix checks; it does not replace them.

The strict query oracle is never masked. Capacity 1 exists only in private
fixtures; production remains 32. Callback site labels in the production objects
are not instrumented, so observation totals are not a census proving every
one of the 12 query backedge sites. Actual worker abort behavior and full actor
callers were not rerun because the existing observation adapter is unchanged.

An initial independently compiled C arithmetic reference commuted the first
addition and selected a different NaN payload. That is why the supplemental
reference was tied to retained instruction order and authoritative exceptional
checks were moved to actual query objects. The original object performs
`squared_y + squared_x` first; the candidate preserves it explicitly. Initial
whole-query nonfinite fixtures faulted in the original lane, and remain saved as
diagnostic attempts rather than passing complete queries.

## Private reproduction

Evidence is under `direct-cluster-query/query-semantic-leaf-prototype`:
`cost/`, `observations/`, `remaining-observations/`,
`exceptional-prefix-final/`, `retained-order-values-qualified/`,
`final-object-check/`, `state-recipe.json` and the top-level receipt/report.
The first 29 complete observation cases are retained in `observations/result.json`;
the four remaining cases are in `remaining-observations/result.json`. Their
union supplies the 33-case receipt, without repeating already passing cases.

```sh
python tools/prototype_query_semantic_leaf.py \
  --cost-dir "$QUALIFIED_INLINE_COST" --out "$PRIVATE_OUTPUT/cost"
python tools/test_query_semantic_leaf.py \
  --cost-dir "$PRIVATE_OUTPUT/cost" --out "$PRIVATE_OUTPUT/observations"
python tools/test_query_semantic_leaf_prefix.py \
  --cost-dir "$PRIVATE_OUTPUT/cost" --out "$PRIVATE_OUTPUT/prefixes"
python tools/test_query_semantic_leaf_values.py --out "$PRIVATE_OUTPUT/values"
```

The first two tools use the existing private owned-image query artifacts;
generated source, objects and guest data are never added to this patch. No Vita
or Vita3K validation occurred. A production proposal should remain query-only,
track the authored helper dependency, preserve the exact OFF object and caller
guards, and qualify its final compilation before any enable decision.
