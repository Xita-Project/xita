# Optional exact-state clip-wrapper region

`XV_NATIVE_CLIP_REGION=1` builds an experimental native region for Halo 3925's
B7F10 wrapper, 117B0 plane builder and B71C0 clipper. Runtime admission defaults
**OFF**. Benchmark kind **36**, `clip-region`, performs OFF/ON/OFF at the same
resolution; completion, cancellation and lost-view restoration all return OFF.
This is a candidate for physical comparison, not a measured FPS improvement.

The original B7F10 setup, large-frame probe, saved guest-stack writes and first
signed count tests execute before admission at B7F55. Negative/empty inputs never
enter the helper. Only the full entry's initial branch attempts admission;
loopbacks and independently dispatched B7F50/B8000 entries retain original code.
The whole owned image and all four function spans (including 1D130) are checked.
Generation refuses disabled Python assertions and changed instruction shapes.

## Contract

The region retains all eight guest registers, all eight raw x87 slots, FP TOP,
FSW and lazy flags, every original guest-memory operation and every original
preemption edge. It adds no numeric/layout prepass. Ping-pong buffers, mapped
aliases, overlapping REP copies, capacity failures and original scratch writes
remain observable. Full context is published before each original clip guard;
all state is reloaded **after** the guard can park. There is no enclosing region
lock, and recursive guards still perform their original parking checks.

The current native clip's five ARM operand-order corrections are present in both
benchmark arms. Reversed-source VFP add/multiply instructions preserve the NaN
operand priority observed in the original instruction lift compiled by this
VitaSDK at B723A/B728A/B7306/B7314/B735D. The region also preserves the original
117CC/1181D plane additions. No NaN normalization or finite-value scan is used.
This proves equivalence to the project's compiled original lift, not independent
Xbox x87 hardware conformance. Non-ARM arithmetic expressions remain unchanged;
inherited exceptional-value host differences are **not** claimed fixed. The
prior host exceptional matrix had 819/2,048 current-native discrepancies and
845/2,048 fused discrepancies before this integration.

Actual diagnostic emission keeps its function markers. The native clip's
worker-specific suppression of its probe-return watch/marker is retained.
Active function histograms or watch callbacks decline region admission before
mutation; the original calls then execute, including callback changes to state.
The ordinary B7F10 phase scope remains outside the hook and still cleans up on
return. The audited profile has no independent 117B0/B71C0 phase scope. Adding
such child scopes requires extending the fusion instrumentation or disabling it.
Runtime watch setup and histogram switches occur at their existing frame edges.

The same current-clip mode variables control ordinary and fused paths. Disabling
`XV_NATIVE_CLIP`, choosing FP-only via `XV_CLIP_REGISTERS`, or the existing register
override makes the region decline. Existing clip and register call totals include
fused inner calls under their original guards. Register mode/configuration must
remain stable within an active guest call, as in the current runtime.

Initialization binds the eventual recording native thread at a joined/idle
worker boundary; it does not bind a bootstrap/network thread. Workers may already
exist. Controls/reporting enforce owner identity and drained pool/region state.
Admission uses an atomic enabled/active count without an OS thread query. It
remains active across guest yields; controls abort instead of changing modes
across a suspended region. Counter completions use atomics, then the joined owner
drains them. No new syscall is made from an unregistered/native scratch stack.

## Workload reporting

Each existing periodic native-math report includes:

```
[clip-region] N frames: regions R planes P clips C input-vertices V capacity-failures F max-clips M
```

These are completed admitted regions, executed plane calls, executed clip calls,
sum of positive input counts already loaded by the original clip, observed
`AX=FFFF` clip failures, and the greatest clip count within one region. No extra
guest read, timing call or per-call thread identity is needed. Totals use unsigned
interval counters. OFF or a rejected diagnostic/mode reports no admitted work;
common `[native-clip]` totals still count the ordinary calls. Compare ON workload
per frame across views/effects, rather than interpreting zero OFF regions as no
clipping. These counters do not attribute periodic log/SD hitches or prove their
cause. Percentile frame pacing and average FPS require separate physical data.

## Generation and selective build

Use a private checkout/stage with the owned `haloce/default.xbe` and
`local/halo_ce_3925/game_manifest.json`. The image SHA256 must be
`4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae`.
With the ordinary recompiler interpreter:

```sh
python -B tools/gen_native_clip.py
python -B tools/gen_native_clip_region.py
```

Both generated kernel files are private/ignored. **Regenerate the B7F10 lifted
function through the updated Halo hooks** with the stage's existing tracing and
phase settings. In the audited 32-unit stage it is in `recomp/code_016.c`; shard
numbering is not an API, so locate the function by name. This stage uses phase
scope 44 and no `XV_FN` function markers. Its original body, after removing only
that outer phase statement, matches the independent raw emitter. Retain that
emission mode; the hook passes `markers=0`. No other lifted function needs a
region hook, and interior-entry functions must not be patched.

Selective outputs needing refresh are:

- `recomp/kernel/xk_clip.c`: shared correctness/config/counter bridge, both arms.
- `recomp/kernel/xk_clip_region.c`: new owned native body, optional build only.
- The generated unit containing `f_000B7F10`: single late-entry hook.
- `recomp/host/build/clip_region_original.json` and
  `recomp/host/build/original_000B71C0.c`: private oracle inputs only.

The Makefile tracks mode changes for the affected unit, new helper/control and
archives, including removing the optional objects when changing 1 back to 0.
Build with `XV_NATIVE_CLIP_REGION=1`; this alone does not enable runtime admission.
The existing authorized remote controller can request `benchmark OUTPUT --kind
clip-region`. This work made no hardware, emulator or deployment changes.
Generated game bodies, owned input images and embedded packages must not be
committed or distributed. The release audit rejects the new generated helper.

## Validation and limits

The implementation is exercised by these source-controlled checks (ARM tests
need VitaSDK, Unicorn and pyelftools; guard generation needs iced-x86):

```sh
python -B tools/test_clip_region_guards.py
python -B tools/test_clip_region_control.py
python -B tools/test_native_clip_region.py --out PRIVATE/host --no-markers
python -B tools/test_native_clip_region.py --out PRIVATE/host-asan --sanitize --no-markers
python -B tools/test_clip_region_workers.py --out PRIVATE/workers --no-markers
python -B tools/test_clip_region_workers.py --out PRIVATE/workers-asan --sanitize --no-markers
python -B tools/test_clip_region_diagnostics.py --out PRIVATE/diagnostics
python -B tools/test_arm_clip_region.py --out PRIVATE/arm --no-markers
python -B tools/test_arm_clip_region.py --out PRIVATE/arm --reuse --off --no-markers
python -B tools/test_arm_clip_region_exceptional.py --out PRIVATE/arm --no-markers
python -B tools/test_arm_clip_region.py --out PRIVATE/arm-park --park --no-markers
python -B tools/test_clip_region_build.py
python -B tools/test_pipeline_candidates.py
python -B tools/test_frame_acquisition.py
python -B tools/test_remote.py
```

The commands above select the current stage's ordinary no-marker mode. The
traced mode is also validated separately; diagnostic checks use real emitted
`XV_FN` macros. Both references use the generated unit's cached mapping/image
macro prologue, while header inline helpers retain their original mapping rules.

The host oracle compares original/current/fused full context, 2 MiB memory,
mappings and scheduler snapshots for 4,096 fixtures, then repeats with park
mutations, normally and with ASan/UBSan. One adversarial DF=1 input makes the
original overwrite its own outer-loop index indefinitely; its matching 4,096-event
prefix is bounded, not presented as a completed function. The other 4,095 return.

Production-worker acceptance executes the unmodified pthread pool, callback
execution, semaphores, park acknowledgment and quiescent owner-service paths.
The fixture only orders lane wakeups to retain identical guest addresses. Tests
include no outer guard, recursive outer guard, contention and non-fast locking,
with timed and polling waits. At the actual parked callback the owner changes
TOP/FSW/FCW/GPR/lazy flags/all raw slots/live plane scratch; original/current/fused
outputs and event snapshots must remain identical. Diagnostic tests use the
actual emitted function-marker macros and callback mutations. Platform Vita
mutex syscall timing/deadlock freedom still requires the parent's physical test.

The ARM oracle executes all three linked arms with the actual control and common
counters: 1,024 layout/rounding cases, an 8,192-case exceptional matrix, and a
further 1,024 parked-state cases including FPSCR changes. All comparisons require
bit equality, including exception status. Memory-copy imports are modeled; counts
are instructions, not cycles, thermal behavior, SD latency or physical FPS.

The survey matching the current stage's no-marker mode measured current
21,821,076 versus region 18,241,388 modeled instructions (16.40% fewer). Runtime
OFF was 21,843,556 (+22,480, 0.103%). The 48 positive-entry/degenerate-plane cases
without an inner clip grew from 54,336 to 68,880 (+26.77%); workload distribution
matters. These totals include the test observation/call scaffolding in all arms;
they are not a profile of a linked game package. Native stacks are 840 bytes
for the region plus 40 for admission and 48 for the retained wrapper (928 total
excluding callees), versus 536+72=608 for the current clip/wrapper. The region's
linked body is 17,568 bytes. These costs include bookkeeping and do not establish
a hardware win. Confirm actual stack headroom and representative scenes before
considering any default change.
