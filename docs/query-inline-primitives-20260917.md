# Selective query primitive inlining prototype — 2026-09-17

This private prototype adds `always_inline` to the unchanged `x87_load_f32`
and `x87_store_f32` definitions **only for the separate collision-query unit**.
It is based on `7301a1b`. No production generator, Makefile, startup default,
shared runtime header, generic function or caller eligibility is changed.
The prototype is ready for an integration review; it has not been deployed.

The remaining query work still uses the existing double-precision x87 model,
PC53 behavior, guest-memory operations and compiler floating-point options.
This is not a float32 conversion or a guest-address mapping cache.

## Scope and memory binding

`tools/prototype_query_f32_inline.py` copies the authored tree into a private
build directory and invokes the retained production Make target. The baseline
query `.text` must match the retained query object byte-for-byte before the
candidate is built. The retained generic objects remain unchanged.

For the candidate, four query includes are redirected into a private copy of
its header tree. `xv_x86rt.h` uses `#pragma once`, so all transitive includes must
resolve to the same copied header. Of 32 copied headers, 31 are byte-identical;
the remaining header differs only in the two `always_inline` attributes.
Those attributes are reversible and their exact declaration matches are
validated. The optional transformation's OFF branch returns unchanged source.
The scripts reject optimized Python, and all generated guest output stays
outside the source worktree.

The runtime header is still parsed before the query unit overrides `X_G` for
captured integer roots. Preprocessed `x_guest_read` and `x_guest_write` retain
the original global `g_xram`/`g_xpt` expressions. Inlining therefore preserves
the distinction between global x87 accesses and per-function captured integer
accesses, including after a callback changes the page-table root or one entry.

## Whole-call result

The real Make/compiler flags are retained, with `-fstack-usage` added only for
reporting. Every result below executes the whole query, its existing typed
helpers, generic visitors and fallback closure. These are ARM instruction
counts from the exact production objects in the bounded execution harness;
they are not elapsed hardware time or an FPS prediction.

Depth-16 synthetic cases:

| Case | Original generic | Current fused | Selective inline | Change vs current fused |
|---|---:|---:|---:|---:|
| One surface, overlapping vertices | 254,616 | 225,929 | 220,104 | -2.58% |
| Four surfaces, overlapping vertices | 705,169 | 599,370 | 582,971 | -2.74% |
| Rejected traversal | 4,898 | 4,628 | 4,631 | +0.06% |
| Empty traversal | 1,133 | 1,092 | 1,098 | +0.55% |
| One surface, outside vertices | 255,663 | 234,875 | 228,897 | -2.55% |
| Four surfaces, outside vertices | 573,535 | 541,915 | 525,249 | -3.08% |
| Combined 3D/2D traversal | 1,772,376 | 1,638,809 | 1,583,784 | -3.36% |

The same seven cases also pass at depth 1. Busy-query instruction reductions
are about 2.5–3.4%. Rejection adds three instructions; empty traversal adds six.
The compiler layout change therefore is not a universal improvement.

Query `.text` grows from 33,404 to 33,624 bytes (+220). Its local native frame
shrinks from 2,152 to 2,136 bytes; the 24-byte adapter is unchanged. Whole-call
observed peaks on these normal cases shrink by 16–24 bytes. Both out-of-line
f32 helper symbols disappear, while undefined imports remain identical.
No full native-thread high-water or hardware cache/timing claim is made.

## Correctness coverage

The 14 cost cases compare three lanes: retained generic reference, retained
fused baseline and selective-inline candidate. Final comparisons include the
complete 360-byte context, full 8 MiB guest arena, both page tables, logical
root identity, event state and full FPSCR, with no masked fields.

`tools/test_query_f32_inline.py` adds 19 targeted three-lane cases:

* Unaligned guest stack; cross-page center reads; physical center/output alias.
* Enabled motion profiling and expired-budget callbacks.
* Original root-replacement fixture; a different page-table root plus remapped
  plane/value; and changed plane mapping/value under the same root pointer.
* Fractional inputs with TOP 7 under all four rounding modes and FZ/DN/sticky
  FPSCR settings; subnormal, signaling-NaN and infinity input cases.
* One-record continuation-capacity fixtures with and without both new mapping
  mutations. They enter the original generic fallback without restarting.

These cases produce 277 observer snapshots in each lane. Every callback and
profile observation sees the original context identity, complete context,
full-arena hash, complete page tables, roots and FPSCR. Final guest memory and
context compare byte-for-byte. The mutations are authored fixture callbacks
applied after the real fixture callback returns; they are not firmware tests.
The production capacity remains 32; capacity 1 is a private overflow fixture.

## Reproduction and limits

The prototype requires an owned XBE and matching manifest, the retained build
with generic objects and `query_fusion.o`, its recorded Make command, and the
previous complete-query fixture objects. Use a Python environment containing
the recompiler, Unicorn and pyelftools dependencies:

```sh
python tools/prototype_query_f32_inline.py \
  --retained-build "$RETAINED_BUILD" --build-command "$BUILD_COMMAND_JSON" \
  --fixture-dir "$QUERY_FIXTURE" --xbe "$XBE" --manifest "$MANIFEST" \
  --out "$PRIVATE_OUTPUT/cost"
python tools/test_query_f32_inline.py \
  --cost-dir "$PRIVATE_OUTPUT/cost" --out "$PRIVATE_OUTPUT/observations"
```

The private receipt records exact paths, commands, input hashes and object
reports. Local evidence is under `direct-cluster-query/query-inline-primitives`:
`qualified-cost/`, `observations-final/`, and `root-binding.json`.

This bounded pass reuses the qualified query eligibility and caller contract;
it does not reopen every unrelated oracle. Firmware copy operations are
modeled by the harness. No Vita or Vita3K run occurred. No public guest source,
object, image or map data belongs in this patch. The separate edge/face
prototype is not combined here; any later combination must remeasure the
whole call because compiler layout and overlapping savings can change.

A subsequent production proposal should generate the private specialization
with normal dependency tracking, preserve the exact OFF source/object and
caller guards, and repeat the final object identity/cost check. This prototype
intentionally leaves that integration decision to the owner of the cumulative
build. Its modest instruction gain alone does not establish a frame-rate gain.
