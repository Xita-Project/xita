# World collision query capture qualification

The capture adapter now executes the actual retained perf19 ARM query, with
explicit memory recording throughout its private helper closure. It is not
linked into the game and does not skip any query on hardware. The Vita remains
on perf19. Halo 2 remains parked.

Follow-up: [complete CPU and memory replay qualification](query-cpu-replay-20260919.md)
now includes actual floating-point write masks, current-budget consumption and
changed-caller replay. It remains outside the game pending selective admission.

This extends the [memory recorder qualification](query-reuse-contract-20260919.md).
`tools/query_memory_capture.py` accepts only the reviewed source hashes and
writes a separate query variant. Original query/runtime headers remain intact.
Generated game bodies and ARM fixtures stay in private build storage.

## Capture behavior

`xk_query_capture.{h,c}` passes caller-owned state explicitly; no mutable global
capture pointer or TLS is introduced. The transformed query covers:

* Scalar loads/stores and the original global-root stack pop.
* Page-split runtime copies, x87 loads/stores and typed collision helpers.
* Mapping-only alias/contiguity checks and direct world-run read spans.
* Abandonment before callbacks or any of eight external fallback call sites.

Store setters evaluate the right-hand side before notifying a write, preserving
read-before-write dependencies and same-value stores. Typed accesses retain
their original physically contiguous behavior across a guest-page boundary;
page-split helpers continue translating each page independently.

The selected entry copies global arena/page-table roots and callbacks abandon
capture. Scalar notifications rely on captured roots remaining equal to global
roots during this callback-free interval. These private helpers are not APIs
for arbitrary independently supplied roots. Checked-address policy builds are
explicitly rejected until their epoch/watch/fault effects are qualified.

## Validation

Normal and ASan/UBSan capture-module tests pass. They cover aliases, page-only
dependencies, unaligned contiguous crossing with a noncontiguous next mapping,
wrong roots/pointers, overflow, bounds, callbacks and inactive capture. Five
generator tests include an executed read-modify-write ordering check and
rejection of unreviewed source or compound stores.

Fourteen synthetic full queries were compared in three ARM lanes: original,
capture variant with null recorder, and active capture. Complete guest memory,
CPU context and native FPSCR match. Memory replay reproduces the original final
arena. Recorded initial dependencies cover all reads from the independently
qualified original access traces; written-byte footprints match exactly.
Whole admitted world-run spans can add conservative read dependencies.

Another 208 ARM comparisons cover eight x87 stack positions, signaling-NaN
incoming stack payloads, page aliases, native rounding/flush/default-NaN modes,
and preemption. Every complete arena/context/FPSCR comparison passes, and all
16 preempting cases abandon recording before the callback. This is instruction
emulation with modeled firmware copies, not Vita3K or Vita frame-time testing.

Native FP controls can change which bytes the query reads. Reference traces
and capture must use identical controls; they cannot be treated as irrelevant
to future cache keys merely because some final geometry outputs match.

## Cost and deployment decision

| Synthetic case | Original query | Query plus capture | Memory-only replay |
|---|---:|---:|---:|
| Split traversal |103,835|1,660,754|5,234|
| Winding |40,161|596,383|3,441|
| Edge |38,338|558,666|3,471|
| Negative traversal |1,215|17,560|1,121|

These are ARM instruction counts, excluding firmware copy cost and real cache,
scheduler and memory timing. They are not FPS estimates. Even in this optimistic
model, the expensive examples need about 15–16 subsequent successful replays
to amortize capture; the short negative example needs 174. CPU-state replay and
lookup will add costs. Passing a null recorder to the transformed query also
costs more than the original, so normal calls must retain the original path.

Do not record every miss or enable a broad cache from the 54.5% input-match
census alone. That census does not measure surviving exact dependencies or
consecutive useful hits. The next adapter needs explicit CPU write effects,
actor/lifetime admission, and a measured policy favoring expensive sustained
repeats. A miss must retain the existing query and cumulative optimizations.

## Reproduction

```sh
python3 tools/test_query_capture.py
SANITIZE=1 python3 tools/test_query_capture.py
python3 tools/test_query_memory_capture.py
python3 tools/query_memory_capture.py \
  --recomp-dir /private/perf19/recomp --out /private/capture-generated
```

Private evidence is under `../collision-query-capture/`: the reviewed access
inventory, generated hashes, and `qualification/` build/check scripts, retained
ELF, 14 detailed results, 208 variants and SHA256 receipt. These are synthetic
qualification cases, not proof of campaign stability or a delivered speedup.
