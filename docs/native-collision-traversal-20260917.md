# Collision traversal: rejected localization prototype and reusable oracle

This isolated experiment is **not a performance candidate for integration or
hardware**. It implements both primary collision BSP traversals, but keeping
guest registers in native locals did not reduce complete-call ARM instruction
cost. The substantial typed traversal remains unfinished. Repository feature
and startup defaults are OFF; there is no live control, selector, lock change,
new worker, guest lifetime release or deployment.

The source changes are confined to this experiment's game profile, generator,
immutable startup control, build wiring and fixtures. Generated original and
candidate bodies stay in the private output directory. No guest image or map
bytes are checked in.

## Implemented boundary

The owned-image-qualified primary functions are `87EA0–88103` and
`87E10–87E91`, both `void f_ADDRESS(xctx *restrict c)`. ECX is the query context,
EDX the signed node/leaf encoding. Their original four-byte return-stack effects
remain. Independent interior entries are untouched. Outer `88110` and leaf
visitor `86F50` remain original; the currently enabled vertex-ring and
segment/sphere helpers remain active. The object transaction is unchanged.

The production profile derives the candidate from the owned emitted bodies,
retaining their FP operations, all lazy flags, guest memory access order,
recursion, capacities, duplicates and taken branches. Only eight GPR storage
locations change between observation boundaries. Every guest child call and
actual preempt callback receives all eight registers and reloads them afterward.
All five traversal backedges remain: `87E7F`, `87F18`, `87F8D`, unconditional
`87F96`, and `880F6`. A callback's changes affect the already-selected target.

The existing pure `xv_bsp_sphere_plane_distance` helper reads EAX/ECX/EBP and
writes EAX plus x87 state. Only those GPRs cross that helper boundary; it has no
scheduler or observer call. Guest children still receive complete context.
`X_POP32` retains its global mapping roots, unlike emitted memory macros using
captured roots. A targeted page-table-pointer/stack remapping test detects a
wrong captured-root pop. Floating-point operations were not reassociated or
replaced by FMA.

`XV_NATIVE_COLLISION_TRAVERSAL=1` compiles the candidate; immutable
`XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT=1` selects it before any threads. Both
default to zero. The control exposes only a const mode and passive getter.
Feature and startup stamps are separate; only the control object receives the
startup define. The Makefile wiring is provided for isolated continuation,
not recommended package settings. It has not received a full retained-stage
incremental build qualification.

## Validation actually completed

Private evidence is `E/native-collision-traversal`, where `E` is the existing
direct-cluster-query validation directory. `stage-oracle-identity.json` proves
the freshly lifted raw primary bodies for `88110`, `87EA0`, `87E10`, `86F50`,
and `B0CB0` equal the earlier private staged originals (removing only the known
BSP-distance insertion). The oracle executes the cumulative baseline and that
same baseline plus the traversal transform. Raw bodies are emitted for identity
checks, not executed as a third lane. Collection is outside `88110` and is not
executed by this fixture; no collection code changes are made.

* Host: 256 complete query/traversal comparisons pass; final scalar prototype
  also passes ASan/UBSan for 256 comparisons and 213,450 matched yield observations.
  All five traversal backedges are observed. The fixture compares the complete
  xctx, full 8 MiB synthetic arena, page tables/root selection and callback trace.
* Strict ARM: 246 comparisons pass with full context, memory, mappings, yield
  observations and entire FPSCR equality. They exercise incoming x87 stack tops,
  four rounding modes, FZ/DN/cumulative-status state and NZCV state. Finite fixture
  inputs are simple exact values; this is not exceptional-FP qualification.
* A later focused 64-case host probe covers pointer replacement plus modified
  saved-EBX stack mapping. A wrong captured-root pop fails at context byte 12;
  omitting publication before a yield causes the negative fixture to fail.
  A separate 64-case run also hashes complete context at every real wrapper,
  traversal, visitor and segment entry; all entry/yield observations match.
* Actual feature-OFF preprocessing is identical to the baseline for both
  functions. Wrong image spans decline; independently dispatched interior
  entries receive no traversal hook. Default-OFF ARM execution also matches.

The fixture includes full `88110` calls and direct entries, deep node walks,
recursive crossings, ancestor matches/misses, repeated surfaces, capacity-full
output arrays on direct calls, unaligned stack, mutations of flags/x87/data and
mapping entries/root pointers at yields. It does not yet establish arbitrary
guest input/output/scratch alias safety for a typed rewrite, exception-enabled
FP behavior, every NaN/infinity payload, real fiber cancellation/STOP behavior
or multiprocessor runtime behavior. No hardware frame-time claim follows.

## Complete production-shaped ARM costs

The cost runs use `--no-site-trace`: no fixture backedge-site writes or entry
observers are inserted. All generated wrapper, traversal, real visitor, current
native-helper and startup-control instructions are counted. Firmware bulk-copy
bytes/imports are recorded separately. These are instruction counts, not cycles
or Vita FPS. `arm-production-cost-on/result.json` contains 44 complete comparisons;
the separate OFF run contains the first eight mode-control comparisons.

| Workload | Original | Candidate ON | Candidate OFF |
|---|---:|---:|---:|
| One 3D node, empty result | 681 | 709 | 694 |
| 16 3D nodes, empty result | 5,316 | 5,809 | 5,374 |
| 128 3D nodes, empty result | 39,924 | 43,889 | 40,318 |
| 128 2D nodes, one final surface | 40,950 | 42,367 | 40,825 |
| One leaf, surface work | 14,733 | 14,897 | — |
| 16 levels, four-surface work | 689,738 | 692,305 | — |
| 16-level direct ancestor-scan-heavy query | 284,003 | 283,034 | — |

The tiny 0.34% instruction saving in the last row does not rescue losses of
9.93% on a genuine 128-node 3D walk and 3.46% on the 2D walk. This is not just a
mode-check cost: the former's OFF delta is 394 instructions while its ON delta
is 3,965. Function-level traces locate 2,667 ON instructions in newly outlined
`x87_load_f32` calls plus 1,298 extra traversal-body instructions; the existing
plane helper stays exactly 16,512 instructions in both. Register storage changes
alter compiler inlining/spilling as well as publication. On the surface-heavy
case, 513,029 original instructions are in `86F50`, 81,920 in `B0CB0`, and 83,149
in the two traversal bodies; these are synthetic instruction shares, not current
physical time attribution.

## Required typed continuation, and precise unresolved work

The largest bounded next component remains the same two primary functions,
including `87FE7–880DF` projection/axis preparation; leave `86F50` and the guard
unchanged. Merely localizing GPRs leaves essentially all x87 stack/status and
generic flag machinery intact. A useful implementation must derive node and
projection decisions with typed scalar values, keep the transient x87 state
local, and reconstruct exact observed state at the actual boundaries. No known
ownership blocker requires a map snapshot or lock release. The missing work is
the exact state/alias/continuation implementation:

1. **3D node state, `87EAD–87F18`.** Preserve the radius-negation float spill,
   the plane-distance helper's popped x87 slots, both comparisons' status/TOP,
   partial EAX/DL/AL updates and lazy flags. In particular `87F96` yields before
   `87F0F` selects a child using AL. A typed traversal cannot preselect that child
   before the callback; resumption may change AL. `87F18` instead resumes its
   already-taken node-descent target with the callback's state.
2. **Ancestor and leaf work, `87F23–87FE6`, `880E6–880F6`.** Keep the signed
   16-bit scan index behavior, every repeated count read, capacity-256 append
   order, signed ancestor keys and first-child/second-child recursion order.
   At `87F8D`, a callback may change EAX/EDX/ECX/query pointer or the ancestor
   count; an immutable host set or captured loop bound is not equivalent.
   Reload the original fields after each child and yield rather than retaining
   a host pointer across them.
3. **Projection, `87FE7–880DF`.** Original stores to `ESP+14/+18/+1c` are
   interleaved with further centre/plane reads. Those ranges can alias through
   the guest mapping, even when virtual addresses differ. Capturing all inputs
   up front needs a physical mapping/disjointness proof after the original
   prologue writes, otherwise execute the original sequence. Preserve axis
   ties, orientation, lookup order, query `+21c/+21e/+220/+224` stores and all
   stale x87 slots/status, not just the resulting two coordinates.
4. **2D nodes, `87E20–87E7F`.** A straight-line node can compute its two tests
   in native scalars, but must reproduce both comparisons, partial registers
   and stale slots. After first-child recursion at `87E71`, the second child
   pointer is read from the returned ESI at `87E7A`; do not hoist it. Original
   stack writes/pop mapping and `87E89` visitor context remain observable.
5. **Continuation rather than restarting.** If a fast path needs finite-input,
   trap-control or alias admission, check before its first changed effect.
   Recheck after callbacks if needed. A decline after an append or recursive
   child must resume an exact original label with reconstructed state; restarting
   either primary entry would duplicate writes and consume the wrong budget.
   The five backedge targets and the post-child labels form explicit continuation
   points. A private per-node alias proof suffices where there are no writes or
   callbacks; a broad claim of immutable geometry does not.

Implementing and qualifying this typed state machine is substantial remaining
work. The reusable complete-query fixture is ready to compare it, but it must
grow exceptional-value and adversarial mapped-alias cases before that rewrite
can be considered qualified. The rejected localization experiment should not
be deployed while that work is pending.

To reproduce privately, use the repository tool with the owned XBE/manifest and
an output directory outside the checkout:

```
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/new-run --sanitize
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/new-arm --arm
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/new-cost \
  --arm --arm-limit 44 --no-site-trace
```

Python optimization (`-O`) is rejected. The ARM runner uses the existing
instruction-fixture machinery, not Vita3K or device access. Selective production
regeneration is deliberately not recommended for this rejected experiment.
