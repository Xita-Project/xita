# Engine update restructuring on Vita

The target remains sustained 20 FPS in representative campaign gameplay on
physical Vita hardware. Silent Cartographer is the current stress baseline.
The separate source-port experiment is cancelled; its reference source remains
useful for understanding and replacing pieces of the existing Xita runtime.

## Evidence and scope

The perf313 strict-gate capture places the owner update at approximately
115–117 ms/frame, including 96–98 ms of object updates and about 16 ms of AI.
The render helper consumes about 72 ms of CPU time concurrently. These spans
are nested or overlapping and must not be added. The last collision windows
attribute about 17.5 ms/frame to native sphere queries and 1.0 ms to feature
tests. Two simulation ticks occur per rendered frame; do not discard ticks to
inflate FPS. Broad whole-object workers worsen frame time to roughly 190–209 ms.

Evidence: ../collision-detail-313/critical-path-summary.json and
../collision-detail-313/collision-summary.json, relative to the source checkout.

## Reference-guided decomposition

`source/objects/objects.c` in the private reference separates `objects_update`
into visibility/activity handling, active-object callbacks, creation/deletion,
and garbage collection. Its `object_update` dispatches type-specific behavior,
damage, exported function values, node matrices, function/color calculations,
light reconnection, child updates, and matrix postprocessing.

The Xbox 3925 call structure suggests these correspond to 900E0 (outer update),
8FB70 (per-object update), and 8ECA0 (final collection). This is a semantic
mapping to verify against instructions and field accesses, not proof that the
reference structures or executable addresses can be copied unchanged.

## Implementation sequence

1. Separate once-per-tick lifecycle/collection cost from per-object callbacks.
   Attribute the currently unmeasured 8ECA0 tail while preserving guest return
   state. Sample callback subphases instead of timing every tiny function.
2. Select the largest measured component for a native ARM implementation with
   explicit input/output and mutation contracts. Retain original dispatch as
   fallback and a differential oracle. Avoid spending a build on a thin
   dispatcher whose inclusive time belongs to its callees.
3. Check whether repeated matrix, lighting, or collision work has unchanged
   inputs. Reuse only with complete dependencies, including animation, attached
   children, geometry changes, and object lifetimes. Existing caches and native
   implementations must be audited before duplicating them.
4. Parallelize bounded computations over owned snapshots; publish at the original
   engine boundary. Keep creation/deletion and shared-list mutation ordered.
   Do not revive whole-object worker default merely to raise core utilization.
5. Use the Pi for ARM differential and concurrency checks, then test ordinary
   gameplay on Vita. Keep improvements only as performance wins when whole-frame
   evidence supports them; record correctness-only changes separately.

## Lifecycle attribution (perf314, hardware tested)

The sparse owner timer now also selects `8ECA0`. The diagnostic shard patch
wraps the final tail call in `900E0`; it preserves every guest instruction,
call, and return while preventing host tail-call elimination at that site.
Nested timing tests cover the new lifecycle scope, 20,000 ignored callback
scopes, and helper exclusion. Twelve selector cases and five patcher tests
pass on the host with the timing fixture under ASan/UBSan.

The reference mapping has stronger instruction-level support: `8ECA0` checks
the force flag at object-globals offset 2, contiguous-free-memory thresholds
`0xCCCC` and `0x19999`, the 2,048-object limit with 102 slots remaining, and
the 50-active-garbage threshold. Those agree with the reference collection
policy. It calls `A5FF0` on the compaction path. This does not establish that
collection is expensive in current gameplay; that is the purpose of the new
measurement.

Private stage and reproduction artifacts: `../engine-lifecycle-314/` relative
to the source checkout. The existing patcher command is:

```sh
python3 tools/patch_scene_phase_timers.py /path/to/stage/recomp \
  --parents 000900E0 --any-call --tail-calls
```

Run with `XV_SCENE_PHASES=0 XV_TICK_PHASES=2` and the established strict worker
gate. A small `900E0>8ECA0` result rules out this final collection pass as the
dominant cost; the residual still includes all other work in `900E0`, including
activation and creation/deletion, and must not be called callback-only time.
This is attribution, not an optimization or a claim of reaching 20 FPS.

The physical Vita completed the 600-second capture, including loading and
stationary gameplay. Forty complete gameplay windows contain the lifecycle
scope. In the last three, the enclosing object pass averages **93.89 ms/frame**
and final collection **0.01 ms/frame**, with 120 calls to each per 60 frames.
Thus final garbage collection is not the current bottleneck. This does not
rule out deletion or allocation work elsewhere in the object update.

The final 600 present intervals (report ends 7080–7620) average 124.28 ms
(8.05 FPS), with p95 133.57 ms, p99 284.32 ms, and ten intervals above 200 ms.
All 600 exceed the 50 ms target. This stationary diagnostic capture is not a
matched speed comparison or a 15-minute combat/stability qualification.
The extra timing build did not establish an FPS improvement.

Next, target the remaining **8DDF0 animation/model preparation boundary**.
Earlier perf305 attribution placed this around 28 ms inclusive, with roughly
18 ms outside its selected child timers. Those older instrumented spans are
not exact current CPU self costs. Current hardware confirms hierarchy, root
matrix, and aim-blend natives are already admitted heavily. Audit the remaining
guest-state bookkeeping and repeated setup around them, retain their existing
math, and define input/output and lifetime contracts for a bounded native
replacement. Do not duplicate the native helpers or treat the thin `90770`
callback dispatcher as if all of its inclusive time were dispatcher overhead.


## First native engine-boundary candidate

The root-matrix chain under 8DDF0 now has an opt-in native candidate joining
three operations while retaining their arithmetic and intermediate output.
Host and physical Pi differential checks pass, including full lifted hierarchy
cases with confirmed admission. This is not yet a Vita FPS result. Details,
limits and reproduction artifacts are in [native-root-chain.md](native-root-chain.md).
The broader engine update work and 20 FPS qualification remain incomplete.

## Object-pass boundaries prepared after perf315

Perf315's native root chain did not demonstrate a whole-frame improvement:
late stationary b30 intervals remain 124.97 ms. Before broadening replacement
scope, distinguish three passes in 900E0 using
`tools/patch_object_pass_timers.py`. It requires the exact reviewed function
SHA256, rejects changed control flow, is idempotent, and removal of its tagged
lines recovers the original shard byte-for-byte.

Synthetic elapsed scope IDs (not guest callees):

- F0900001: first instruction through 9022D, setup and activation changes.
- F0900002: 9022D through the instruction at 902A9, ordinary update loop
  including its existing worker join.
- F0900003: 902A9 through 90314, new/deleted object handling and finalization,
  excluding the separately timed final 8ECA0 garbage collection.

The first boundary follows object-jobs begin; its cost remains in the parent's
residual. Both emitted 9022D entry paths are covered, without inserting timers
inside per-object loops. All guest instructions, callback ordering and joins
remain intact. The synthetic IDs are selected by sparse owner diagnostics.
ASan/UBSan selector tests cover nested siblings, ignored per-object calls and
helper exclusion. Actual perf315 shard round-trip and mutation checks passed.
These new spans are **not built or deployed yet**; perf315 has only the older
outer-object/final-collection scopes. Next: build the diagnostic candidate and
measure the same b30 opening before choosing the next native engine boundary.

Reference cross-check: `objects_update` in the private punpckhdq tree has
the same cluster-activation comparison, regular active/non-new object pass,
then new/deleted-object pass as the reviewed 3925 shard. `object_update` also
recurses through attached children/siblings. Therefore callback counts cannot
be equated with top-level object counts, and a future transform cache must
include attachment and postprocessing dependencies. The no-recompute flag is
not permission to skip arbitrary unmoved objects.

Perf316 is prepared in `../object-passes-316/`, derived from perf315 with only
the phase selector, reviewed code_014 observer sites and version rebuilt.
Pi ARM32 Cortex-A9 Thumb selector fixture passed sparse and disabled modes;
all twelve host modes passed under ASan/UBSan. The permanent shard check is
`tools/test_object_pass_timer_patch.py` with the reviewed SHA in the private
patch receipt. Build/deployment state must be checked in that stage; these
qualification checks alone do not establish a hardware result.

## Perf316 early hardware attribution

Perf316 booted in slot0 with independently verified runtime SHA256
`626c51684f69fd3b1c33bd4a1ebb2f6442e8e5d4a1a4d2cac9d54e5d1d6b1926`.
The first three complete gameplay windows have matching 120-call counts
for each object pass per 60 frames. Enclosing object update averages 96.69 ms,
activation/setup 0.023 ms, regular updates including their join 95.31 ms,
new/deleted objects 0.587 ms, final garbage collection 0.01 ms. These are early
elapsed scopes, not CPU self costs or a qualified performance comparison.

The dominant work is in the ordinary active-object loop, not the activation
or lifecycle passes in this opening. Next attribution should partition 8FB70
(type-specific update/physics, node preparation, function/color calculation,
lighting, recursive children and postprocessing), with sampled subtrees to
avoid clock reads on every small callback. Include existing join time before
claiming the entire regular-pass duration is computation. Current evidence
remains far above 50 ms and does not establish a speed gain.

Collector session 11401 remains live with its original 600-second bound;
early snapshot: `../object-passes-316/early-object-pass-summary.json`.
Revalidate the live handle and complete/restore this capture before another
hardware deployment. Normal a30 save namespace remains untouched.

### Perf316 completed capture

Collector11401 completed its 600-second bound with exit0. There are51 complete
gameplay windows. Last-three weighted values: enclosing object pass93.41ms,
regular pass92.51ms, activation0.013ms, lifecycle0.603ms, finalGC0.01ms.
Last600 present intervals (7560–8100) average123.47ms/8.10FPS, p95 134.14ms,
p99 273.25ms, max570.51ms; eleven above200ms and all600 above50ms. This is
stationary attribution, not full combat qualification or a speed comparison.

### Sampled regular-object diagnostic prepared

`XV_TICK_PHASES=3` now selects sparse outer timers plus approximately1/64
whole 8FB70 subtrees using a private xorshift sequence. Recursive children
inherit the sampling choice; it does not consume game RNG or alter updates.
Only sampled trees time 90950,96430,90900,8DDF0,8BD50,8C0F0,8D760,8C570
using already-present call wrappers. These correspond to type update, damage,
export functions, node prep, object functions/colors, lights and postprocessing;
reference names remain subject to address-level validation.

`[object-sample]` reports roots seen/selected/open. Detail timings are sampled
elapsed totals, not full-frame costs; no naive64x extrapolation or CPU-self
claim is justified. Self summaries are suppressed in this diagnostic mode.
Open sampled roots at a frame report invalidate that window's attribution.
Host ASan/UBSan tests cover10,000roots, recursive decisions, helper exclusion
and zero unsampled clock reads. Concurrent helper/worker tests also pass.
The ARM32 Cortex-A9 Thumb Pi fixture passes sampled, sparse and disabled modes.
Private perf317 stage `../object-samples-317/` changes only timing code and
version; generated game shards are unchanged from316. Build session14176 was
started; inspect its terminal result before packaging or deployment.

Perf317 deployment completed (updater23695 exit0), independently confirmed
slot1 runtime SHA7180fe116748e12285f70ad1d2484bdbca0042bf0feac9e649000d235ced773c.
Menu launch43438 and fresh b30 controls82009 both completed0. Collector56997
is live on its original600second bound; at75seconds gameplay readiness is
false (normal loading), sampled rows open0. Do not restart solely for loading.
`tools/summarize_object_samples.py` accepts completed gameplay report groups,
rejects open/count-mismatched trees, and excludes recursive inclusive totals
from root totals. Five synthetic parser tests pass. Use it once loaded;
no perf317 gameplay or speed claim exists yet.

## Perf317 sampled evidence and next callback targets

The live capture now has31 complete qualified gameplay sample windows and0
rejected windows. Last-five sample contains3,142selected root trees. Type update
90950 contributes48.1% of their elapsed time, node preparation8DDF0 22.1%,
light connection8D760 10.2%. These are sample-local shares, not full-frame
CPU percentages or a promise that removing that fraction saves equivalent time.
Early windows were33%/28%/11%; actor state and samples change.

The original90950 already wraps its indirect callback target in phase
observers. An owned-image read of the12type definitions at1FCB78 resolves
callbacks44AD0,4C980,39450,C5230,C0EA0,174440,C3A00,8A750,118440,
1188F0,118EA0,118DD0. Private `object-type-callbacks.json` records each
definition chain and target. No generated guest shard change is needed to
select those existing scopes while a sampled object tree is active.

Perf318 in `../object-types-318/` adds those sampler targets only. Build5796
completed0, host ASan/UBSan and concurrent-isolation tests pass, physical Pi
ARM32 test28980 completed0. Package audit53130 records exact changed objects
and runtime hash. It is NOT deployed: finish317capture56997 and restore
before applying another build. The next hardware result should distinguish
type callbacks from dispatch overhead instead of guessing that90950 itself
needs rewriting.

Perf317 collector56997 completed0 at its600second bound. Final report has48
complete gameplay sample windows and0rejected; last-five sampled3,263root
trees, with90950 47.4%,8DDF0 22.7%,8D760 10.8%. Last600present intervals
(7740–8280) average128.17ms/7.80FPS, p95136.28ms,p99280.15ms,max655.88ms;
ten exceed200ms and all600 exceed50ms. This diagnostic adds observer work
and has different live actor states; it is not a controlled speed regression
or performance improvement claim. It identifies type-specific updates as the
next measured engine target. Private final artifacts are sample-summary-final.json
and settled-frame-summary.json. Restore79138 was started to return to dashboard.

### Perf318 hardware capture started

Updater 70115 completed successfully. Independent status confirms perf318 in
slot 0 with runtime SHA `c1c7e56a5f96f241db95d8dbcdb078ebc91e5fe625b4748ee1fcd7017d1dc3da`.
Menu launch 64478 completed; collector 49007 and fresh-start controller 68278
are active. Collector retains the standard 600-second bound and lease renewal.
Revalidate both handles before more input. This is not yet a gameplay result.

Cross-checking older work confirms the collision chain was already known.
The present sample is current attribution after many changes, not a new
discovery of biped physics. `docs/native-4b9d0.md` identifies 868F0 as an
unimplemented native candidate (old Pi share 9.5% of 4B9D0). The actual staged
868F0 still calls 86170, 862A0 and 86440 through translated code; the private
feature-build-closure.json records their dependencies. Do not duplicate
already-native 88110/864C0, or assume this remaining feature builder alone
will close the 20 FPS gap.

Perf318 capture 49007 completed its 600-second bound with exit 0. There are
48 complete gameplay sample windows and no rejected windows. Last-five type
shares: 4C980 67.3%, 44AD0 13.2%, 39450 8.7%. Last 600 present intervals
(7860–8400) average 128.96 ms / 7.75 FPS, p95 137.74 ms, p99 293.22 ms,
maximum 612.71 ms; twelve exceed 200 ms and all exceed 50 ms. Diagnostic
overhead and actor variation remain; no speed comparison or gain is claimed.
Restore session 36612 was started. The next implementation track is documented
in [native-feature-build.md](native-feature-build.md): a 13-function owned-image
reference was extracted, and initial host/Pi smoke checks passed. No native
feature-building implementation or perf319 hardware package exists yet.
