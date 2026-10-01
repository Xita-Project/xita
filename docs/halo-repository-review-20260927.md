# Halo repository review — refreshed September 27

## New evidence

The previous SDK blocker in `native-upstream-build-experiment.md` is historical.
At `cybersecurity/halo-ce-universal` revision
`3012128fa5d9882cd9d36038f9aceae741bde78e`, native ports have replacement
declarations under `port/include/xdk`. This review used a separate private
worktree, preserving the earlier build attempt and checkouts.

Executed `python3 configure.py`: exit 0 without SDK installation. Executed
`ninja -j4 linux`: compilation reached step 496/496, linking, then failed because
`/lib/libSDL3.so` is incompatible with elf32-i386. No executable was produced.
Evidence: `../private-references/halo-ce-universal-review-3012128/` contains
`configure-review.log`, `build-plan-review.log`, and `native-build-review.log`.
This establishes progress past header compilation, not runtime correctness or
a Vita port. The Linux build needs 32-bit SDL3. The native engine still requires
2342 maps; our previously inspected a30 data identifies itself as 2276.

## Roles of the supplied repositories

| Repository / inspected revision | Useful role | Limitation |
| --- | --- | --- |
| bnunu/halo-1, local `8bad9d8d5ee4307270a2dab6b1ed71c6eaa96f78`; remote HEAD observed `93f8ec8127876fef2b9e574f6e8c3d0a38cfd8d5` | Named engine source and rendering behavior; downstream of punpckhdq | Targets 2342; not independent corroboration of its parent |
| punpckhdq/halo, `ae820f05e9db43809c2548928fb1464061bec214` | Original matching decompilation and symbol/type reference | Matching build requests PAL debug executable plus August 2001 SDK; not a Vita backend |
| cybersecurity/halo-ce-universal, `3012128fa5d9882cd9d36038f9aceae741bde78e` | Native platform adaptation, graphics state conversion, source-build reference | Linux/OpenGL and Android support are not ARM32/GXM support |
| stianeklund/halo, `4e7d53c68d8b1442e6e87fe9ddb512d273ab8a95` | Retail 2276 reconstruction, function mapping and collision regression research | Same version string does not prove each reconstructed function; verify binary identity and behavior |

## Concrete follow-ups, in priority order

1. Use the retail reference's collision/actor mappings to explain measured NPC
   callees. Its `docs/handover-rocket-collision-regression.md` records how raycast
   zone tracking and hit classification affected rockets. This is a useful
   differential-test case, not proof Xita has the same bug.
2. Compare native fork `port/linux/src/d3d8_gl.c` uniform conversion with Xita's
   recording path: upstream keeps input snapshots and serials, converts packed
   colors/fog/bump parameters only when inputs change, and uploads changed
   constant ranges. Xita already has state/sampler/index caches. Measure remaining
   conversion and copy work before adding overlapping caches. Preserve per-slot
   ownership and shader-dependent invalidation.
3. Establish the native Linux reference with 32-bit SDL3 and compatible game data.
   Use it for isolated function correctness and renderer behavior; do not replace
   retail maps or bypass version checks to claim compatibility.
4. Use named transparent-geometry, model-material and screen-effect routines to
   interpret captured tree/reflection/effect draws. Confirm the retail counterpart
   and actual GXM state before changing rendering.

The latest upstream actor-perception edits reviewed are explanatory comments;
the obstacle-avoidance diff chiefly restores names/disclosures. They are not
new performance algorithms. Its sound-obstruction cache is once-per-tick and
omits listener position from the cache key; Xita already has its own mode-6
reuse policy. Copying that cache is not an established additional gain.

No upstream game code or SDK files were merged, no hardware performance claim
is established by this review, and no new Vita deployment was performed.
Separately, the earlier perf298 updater receipt confirms boot, but the current
status request timed out; hardware validation remains outstanding.

## Follow-up: uniform-path audit and measurement correction

`recomp/kernel/xd3d.c:xd3d_ps_sync` already compares all 18 packed colors and
expands only changed values under the default-enabled `XV_PREP_STATE_CACHE`.
`runtime/xv_d3d.c:record_textures` already memoizes texture scales in draw mode 2.
The upstream conversion cache is therefore not a new wholesale optimization.

Found and corrected a narrower measurement problem: `fragment-uniform` timed
only `sceGxmReserveFragmentDefaultUniformBuffer`, excluding the subsequent
packing and `sceGxmSetUniformDataF` operations. Moved the existing timer to the
complete `bind_fragment_constants` call, matching vertex-binding attribution.
No nested timer remains. Counts now mean helper attempts (including shaders
without uniforms), not reservations; old/new timings must not be compared as
identical spans. Failures still return through the original draw-skip path.

The existing extracted-function harness could not compile because it predated
the loading-border uniform. Updated its mock layout/API and added border
coverage: all 32 parameter-presence combinations, reservation/null/write errors,
fresh retries, absent-texture defaults and point-filter dimensions. Both alpha
override runs pass 256 cases each under ASan/UBSan. The existing depth-store
suite also passes. `git diff --check` passes. This is a source-side profiling
correction, not a measured speedup or deployed build. Vita status timed out again;
perf298 ordinary gameplay validation is still pending.

## Pi configuration audit

The `cpu-sample-297/run.sh` command omitted `XV_REC_QUAD`, `XV_REC_VSC`, and
`XV_REC_ENTRY`, whose shared selector defaults to zero. Ordinary Vita297 logs
explicitly load all three as 2 from xita.cfg. Therefore the Pi sample's
`xd3d_count_slow` share is not evidence of a missing hardware optimization.
The same audit also found Pi's `native-1721b0` log says off, whereas Vita config
loads `XV_NATIVE_1721B0=2`. Do not label the old Pi sample production-equivalent.

A bounded follow-up run changes only the three recording knobs to 2, keeping
the same existing executable, input schedule, and isolated fresh save folder.
It deliberately isolates that mismatch; it is not yet full settings parity.
Private receipts: `../cpu-sample-297-aligned/config-audit.json` and `run.sh`.
Remote run: `pi:/home/birchwoodgod/xita-70110/runs/codex-cpu-sample297-aligned-20260927`.
Started under exec handle 52961 with a 180-second timeout; live harness PID13059
and timeout PID13058 verified, log advanced through frame180. Poll the same
handle and collect results before scheduling another Pi workload. Next parity
step must account for native1721b0 and distinguish Vita-only controls from
shared engine settings. No Vita gain is claimed from these supporting runs.

Draw-settings follow-up completed with the planned timeout status 124 (handle
52961 terminal). Fetched complete game log, samples and exit receipt. From frame
1500, 47 windows: scene 7,437 samples, guest reads 6.4%, 53E90 3.0%, A2380 2.3%,
native material core 2.2%, 66510 2.0%. No samples resolve to `xd3d_count_slow`;
100 (1.3%) resolve to `xd3d_count` instead. This is attribution change, not proof
the entry cost vanished; total sample shares and independent runs do not prove
frame-time savings. The prior 1.6% slow-entry observation must not be presented
as a new Vita target. Owner Linux syscall share remains 44.4%, a host-specific
observation. No observed crash in the bounded headless run.

After that process terminated, started a separate 180-second run adding
`XV_NATIVE_1721B0=2` on top of the three recording settings, same executable.
Private script `../cpu-sample-297-native-aligned/run.sh`; remote run directory
`runs/codex-cpu-sample297-native-aligned-20260927`. Exec handle 65687 is live;
timeout PID13359 and harness PID13360 verified. Poll this existing run before
another Pi workload. Gameplay rendering, NPC combat and Vita FPS gates remain
unmet. No new hardware changes.

Native-aligned run 65687 completed its planned 180-second timeout (124), and
fetch63901 completed0. No Pi harness remains from these runs. Logs confirm
native1721b0 calls (last two batches 4,266 and 4,727 per60 frames, zero declines);
zero detail counters do not mean zero traversal. From frame1500, 49 windows:
scene 7,772 samples; guest reads7.1%, 53E90 3.0%, A2380 2.6%, 66510 2.0%,
native material core2.0%. Owner syscall44.7% remains host-specific. No observed
crash; no rendered/combat acceptance is inferred from a headless fixture.

Private `cpu-sample-297-native-aligned/attribute_reads.py` resolves sampled PCs
using DWARF inline chains and records `guest-read-attribution.json`. Of551
scene samples labelled x_guest_read,129 are inside x87_load_f32,63 inside5BA10,
39 inside111A0,25 inside597CB,24 inside5A430,18 inside5C5E0; the rest are spread
across many functions. This rules out interpreting all7.1% as one removable
out-of-line call. Attribution is supporting Pi evidence, not measured Vita
cost. Next inspect the actual Vita instruction paths for any proposed candidate
and preserve guest mappings/yield boundaries; no full-buffer cache is justified
by these shares. Fresh Vita status82052 timed out; perf298 validation remains
pending, no restart/update was attempted.

## Private 5BA10 mapping-root experiment

Actual perf298 `code_010.o` disassembly for5BA10 contains58 MRC sites. The
generated body has70 float loads,12 stores, no guest callees and no scheduler
handoffs. This makes per-call mapping-root retention a narrower experiment
than a cross-frame value cache. Private `../sprite-lighting-audit/` records
the disassembly, source counts and candidate. Reversing helper substitutions
reproduces the original body exactly; arithmetic and accesses remain ordered
in source, split-page operations retain existing helpers, checked builds fall
back to original access primitives. This is not production integration.

2048 synthetic cases compare the complete xctx and4MiB arena, covering x87 TOP,
aligned/unaligned and split-page inputs/stacks, remapped pages, stack/input
aliasing, finite values, infinities and NaNs. Host O2 fails case1077: context
bytes99..101 differ while memory matches (NaN payload difference). Do not
normalize away the failure. Actual VitaGCC O2 helper objects linked into the
ARM Linux harness pass all2048 on Pi (job20265 completed0), but this does not
qualify the candidate: host discrepancy needs explanation, full-shard flags
and TPIDR code need auditing, and no performance measurement exists. The first
ARM link warns about enum-size attributes; establish matching ABI flags before
using that run as final qualification. No hardware/build change was made.

Follow-up: ARM harness rebuilt with `-fshort-enums`, again passes2048 (75714
terminal0). Linker warning persists because Linux runtime objects use other
enum attributes; it was not silently resolved. A separate compiled layout
receipt compares sizeof/alignment and register/x87/MMX/SSE offsets of xctx
between VitaGCC and ARM Linux GCC; `.rodata` bytes match exactly. These
interfaces use xctx pointers and fixed-width types, not enum arguments.

Full code010 candidate95910 and reference20041 both completed0 using exact
code010 build flags and perf298 headers. `audit-shards.py` confirms reference
`.text` byte-identical to installed code010. 5BA10: installed/reference5388
bytes,58 MRC sites,12 local float-helper calls; candidate5496 bytes,1 MRC site,
zero float-helper calls. +108bytes is about2%; counts are static, not per-frame
cost or an FPS gain. Private commands/logs/assembly/full-shard-audit.json retained.
PC NaN payload failure1077 remains unresolved; no relaxation of comparison and
no deployment. All jobs in this subsection are terminal; Pi idle. Next obtain
isolated ARM cost and fuller branch/alias coverage, then establish whether the
full-shard candidate preserves ARM FP behavior. Hardware perf298 still untested
because endpoint is unreachable; asked user to reopen dashboard when available.

Pi leaf-cost run28360 completed0, pinned to core2, actual VitaGCC isolated
function objects with ARM Linux harness. Five alternating-order pairs of200k
calls each: early-return medians71.755/69.622ns; arithmetic421.549/404.307ns;
split-page475.934/460.513ns (reference/candidate). About3–4% isolated savings,
including identical context-reset overhead. No headless frame or Vita FPS
benefit inferred; TPIDR accesses are not executed by this global-table fixture.
All2048 correctness cases pass first. Logs and median JSON retained privately.
Host failure1077 narrowed to st[2]: candidatefff8000000000000 versus
referencefff82468a0000000, both negative quiet NaNs; full arena matches. This
is payload selection, not proof of harmlessness. Keep exact comparison and
candidate unqualified until code-generation effects and relevant ARM full-shard
behavior are sufficiently checked. No running job remains and no deployment.

## Broader Pi workload started

Prepared `../cpu-sample-297-route/`: linked the same cpu-sample297 object set
with retained `perf291-host/navigation/nav-camera.o` and pad-poll linker wrapper.
No game routine or hardware artifact changed. Uses the corrected recording
knobs and native1721b0, existing bounded pod-exit input schedule, fresh private
save, sampler, and a separate coordinate-feedback controller after frame1500.
Controller checks starting position, freshness, height and blocked movement,
and releases input in finally. Target waypoints24,-98.3 and3,-98.3 are route
evidence only, not proof of multi-NPC combat. Avoid frame-time comparisons:
headless navigation logging and pad polling add observer work.

Upload86786 completed0; Pi checked idle first. Harness exec70662 now live under
480-second timeout, controller75844 separately live with300-second deadline.
Remote run `runs/codex-cpu-sample297-route-20260927`; local `guide.log`.
Poll both handles before any more controls or Pi jobs. Inspect terminal route
history and actual encounter telemetry before labelling samples as combat.

Initial controller75844 terminated1 at blocked movement near26.44,-98.49,58.97;
finally released input (pad seq14 hold0 confirmed). Harness remained live.
Reused the known hardware route constraint: suppress Y correction while
25.5<X<27.5 near the pod obstruction. New controller93424 continued the same
game, not a restart, and completed0. Reached both waypoints, finalcamera
3.22384,-98.40967,57.67988 atframe6939; deck height remained about57.62 through
the bridge crossing. Pad seq210 hold0 verified. Histories fetched privately.
This establishes a working Pi navigation route; camera/trigger-box crossing
alone does not prove NPC combat or successful scenario-script execution.
Harness70662 remains active under its original480-second limit (PID16054
verified at4m44s); no controller remains. Continue sampling the existing run,
then collect full log/samples and qualify workload before hotspot conclusions.

## Completed route capture and next-target check

Recovered the final game.log, samples.txt and exit-code from the existing Pi
route run. Exit 124 confirms its planned timeout; this is not a crash receipt.
Final profile from frame7200 covers101 windows: owner41927 samples, scene11722,
other5442. Retained exact output in ../cpu-sample-297-route/final-profile.txt.
Owner guest reads8.6%, 8DDF0 pose update1.3%, native object collection1.1%,
1721B0 query1.0%, reject_object0.9%, 171AF0 traversal0.8%. Scene guest reads8.0%,
53E90 3.7%, 80360_body1.7%, A2380 1.5%, 66510 and native70110 each1.4%.
These are sampled self shares on Pi, not inclusive Vita frame costs. Linux
syscall shares23.8% owner/4.7% scene are not transferable GPU-wait estimates.

Inspected xk_object_collect.c: the existing native rejection preserves guest
frame stores, sibling handling and FP status; objects not safely rejected
continue through the translated callee. Removing those stores or skipping
accepted objects would change behavior. The old disabled-by-default collection
audit does not describe today's explicitly enabled run. Do not duplicate that
optimization or revive its old benchmark result as current evidence.

Next use the complete route samples to select residual work, accounting for
the existing model-register experiment and its full-routine validation status.
Navigation remains established, NPC combat remains unverified. No new Vita
update: status handle70481 terminated with a connection timeout. This does not
establish a crash, sleep state, or which gameplay screen is showing.

## Existing pose optimization and observer-sensitive candidate

Read the full model-register continuation history: root-enabled full-call
replays passed and the candidate became perf291. Thus 8DDF0 in the newer route
profile is residual cost after this work, not evidence that register lowering
has yet to be enabled. Do not propose the existing experiment as a fresh gain.

Private sprite-lighting-audit/trace-host.py adds operation-result tracing to
both 5BA10 bodies for failing case1077. The traced binary passes all2048 cases,
while immediately rerunning the untouched test-host reproduces case1077's
NaN payload mismatch (memory equal). Trace-build.log and trace-host.log retained.
This shows observation changes compiled behavior; it does NOT resolve the
uninstrumented failure or qualify the candidate. No arithmetic normalization,
production change or deployment was made. Given only3–4% isolated leaf savings
on Pi and unresolved full-shard correctness, defer integration rather than
spending a hardware build on an unqualified minor candidate. Next prioritize
the complete fragment-uniform timing correction and ordinary perf298 hardware
validation when the endpoint returns, then choose residual CPU work from those
measurements. Hardware target remains unmet.

## Perf299 profiling build ready, not deployed

Previous continuation progressed by reproducing the uninstrumented leaf failure
and ruling out already-integrated model work. Prepared a separate reflink stage
from perf298 at ../uniform-profile-299. Copied only the two reviewed rendering
profiler files after asserting the baseline copies matched tracked HEAD. Updated
version.json to0.2.0-perf.299; no generated guest-code edits. An initial VERSION
filename assumption stopped preparation before build; corrected to version.json.
Build4851 completed0. audit-package.py completed0 and source copies/hash were
verified. Object changes exactly xv_d3d plus four version-bearing objects;
package changes only game-a.self and boot-game.txt. All other assets and updater
contract match298. Package xita-perf299c.vpk is private and NOT deployed.

Runtime SHA256:4f3d3f3a193920e5a0a4cb17f202700965d0bd1a3394f6c86a49a4b33bb6968c
Package SHA256:fe80b2991799ff07eec079e1cc93ad4e61265a7c88d4dc83700e88c168e52f8c

Vita status73778 terminated with connection timeout. No updater or restart
requested. Next recover live status and lease, protect save, and obtain ordinary
perf298 gameplay before applying299 so installed298 is not silently left
unvalidated. Then use299's whole-helper uniform measurements; old reservation-
only measurements are not directly comparable. Existing512 binding cases and
depth-store tests cover source behavior; build/package qualification adds no
claim of physical performance or long-session correctness.

## Vita reconnected; ordinary perf298 validation started

User confirmed online. Live status succeeded:0.2.0-perf.298, dashboard timing0,
444MHz, awake3581s. Renewed lease3600. Confirmed no existing local route/driver
processes before starting the prepared scripts. Separate stream session19457
and driver session13663 are live; do not start duplicates or deploy while
controls are active. Stream confirms frame progression and timing1 during
launch, readiness still false at23s. Driver uses protecteda30-perf211 save and
ordinary scene-phases0, existing route and bounded firing capture. Poll both
handles, inspect screenshot after sequence and route/measurement receipts.
Perf299 remains staged, not deployed; no new hardware FPS conclusion yet.

## User changes baseline to Silent Cartographer

User explicitly replaces a30 with Silent Cartographer (b30); stop a30
performance testing. Preserve the original sustained20FPS, slow-frame,
multiple-launch,15-minute stability, AI/weapons/audio/save correctness gates
on the new baseline. Opening landing and active beach combat become primary
workloads; no claim that this guarantees all campaign missions. Original goal
attachment's a30 wording is superseded by this user instruction.

Perf298 a30 driver13663 completed0 before the change: settled692frames
65.400ms/15.29FPS,p95116.705ms; firing75frames70.621ms/14.16FPS,p9599.226ms;
recovery605frames74.460ms/13.43FPS,p95109.978ms. No improvement established.
Screenshot confirms rendered outdoor tree/waterfall view, not multi-NPC combat.
Collector19457 intentionally stopped130 after driver completion; controls were
released by driver. Save backup10941 completed0,15files two-read stable.
Perf299 updater now started; collect install.log and its terminal receipt before
launching. New b30 test-save namespace is needed, no reuse of a30 checkpoint.

Perf299 updater54296 completed0:34871722bytes, runtime SHA matches staged
4f3d3f3a193920e5a0a4cb17f202700965d0bd1a3394f6c86a49a4b33bb6968c,verifiedtrue,
restartrequestedtrue,slot1,bootconfirmedtrue. b30 map162877440bytes verified
and fresh isolated b30-perf299 save directories created. Baseline requirements
and settings recorded in docs/silent-cartographer-baseline.md. No a30 tests
remain active. New b30 stream76652 and initial launch86649 live; poll rather
than duplicating. Initial launch only opens Halo and takes one menu screenshot
after45s; fresh profile menu must be inspected before further controls.
