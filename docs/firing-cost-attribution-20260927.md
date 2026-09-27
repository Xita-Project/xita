# Firing cost follow-up

The perf276 local-context trial did not establish a whole-frame improvement and
was rolled back to independently confirmed perf275. This follow-up targets the
firing penalty rather than repeating that trial. Preserve the existing stack.

In the perf276 ordinary-gameplay log, reports near the firing window show scene
helper CPU rising from about 42 to 57 ms/frame, with corresponding scene wall
time about 46 to 60 ms. FA920 owner elapsed reports rise from about 49 to 66 ms.
These are concurrent/nested measurements and must not be added. Async report
proximity is coarse association, not exact per-frame causal attribution.
Recording drain waits grow from about 3.3 to 5.1 ms, with zero queue-full waits.
The observed firing penalty is therefore not explained by those waits alone.
The frame-slow records place nearly all the long interval before Present; this
still includes other CPU work/waits and does not independently rule out GPU
backpressure earlier in a frame.

A single diagnostic launch on perf275 is staged under ../firing-phase-hardware/:
XV_SCENE_PHASES=1 and XV_REC_WORKER_TIMING=1, omitting the frame-queue timing
override to stay within the 32-entry launch limit. Existing protected save,
360p, native hooks and worker settings remain. The phase timers already cover
owner and scene callees in this retained build. Their overhead makes this an
attribution run, not an FPS comparison against ordinary perf275/276 captures.
No new package or runtime policy was installed.

The batched launch is running. Once it completes, wait for loaded/active/director
telemetry, collect the pod interval and the normal firing/movement sequence,
review screenshots, and inspect nested parent/child timing deltas. Check scope
omission/overflow before selecting a native boundary. Missing rows from the
bounded top-cost report are not zero cost. Restore normal diagnostic settings
on the next normal launch. No new optimization is claimed yet.

## Completed diagnostic and next boundary

The pod and firing/movement collectors completed on perf275. Screenshots show
the intended pod, reduced ammunition after firing, and canyon after movement.
No scope-omission/overflow/fatal matches were found in the bounded log scan.
Instrumentation raised the pod mean to 73.626 ms and firing to 94.415 ms;
these are observer-affected numbers, not a regression in the normal build.

Representative report near frame 6300 (trigger bracket 6249–6306), compared
with the settled pod report:

| Nested scope | Pod ms/frame | Trigger ms/frame |
|---|---:|---:|
| FA920 owner total | 71.90 | 85.58 |
| 4B9D0 under biped update | 21.10 | 20.92 |
| 8DDF0 transforms | 13.41 | 13.52 |
| C0EA0 impact branch | not in selected top rows | 4.81 |
| Scene 5DBC0 | 50.32 | 58.35 |
| Model pass 5B760 | 19.34 | 21.65 |
| 59D80 pass | 8.99 | 10.21 |
| 59550 beneath 59D80 | 6.63 | 7.45 |
| Effects 5E270 | 3.11 | 5.43 |
| Recording worker elapsed busy | about 25.5 | 31.18 |

Nested/concurrent rows are not additive. Async logging, report boundaries,
selective output and timer overhead limit precise causal attribution. Batch
frame IDs exist internally, but plain phase lines do not expose each ID; use
nearby frame receipts as bounded association, not exact event matching. The
impact branch was already traced on Pi through C02F0 and effects/sound/collision
(impact-phase-diagnostic.md); no new wrapper-rewrite claim is justified.

59550 remains a substantial unpartitioned rendering path in these captures.
A private probe under ../render-59550-phase-pi/ adds 28 direct-call observer
pairs, including its two tail calls. Its original body matches the retained
perf275 target exactly. Removing observers and joining the two known split
call/return lines restores the complete original shard. The initial audit
rejected those line splits; the corrected audit explicitly checks both.
The ARM harness built, then a single 120-second Pi cores-0/1 run was started.
No competing harness was observed before launch. The surrounding headless
runtime is older, so results cannot establish current Vita FPS or rendering.
run-command.json and run-result.json retain the actual command and completion.
Do not restart while its process is live.

The Vita diagnostic session was ended via companion and dashboard relaunch
requested. Confirm status/lease in restored-dashboard.json before assuming the
restart finished. Normal launch must retain XV_SCENE_PHASES=0 and omit the
record-worker timing override. No save or production optimization was changed.

## Pi child result and private stack-translation candidate

The 120-second Pi job ended at its planned timeout (124), returning 31 reports
with 59550. The bounded fatal/mismatch/scope-omission scan found no matches.
Settled examples: 59550 inclusive 1.84–2.62 ms, remainder 0.68–0.96 ms;
597CB contributes 0.55–0.80 ms, followed by 59200 at 0.19–0.27 ms. These are
instrumented Pi numbers, not predicted Vita savings. The native 11B60 child is
already present. The result selects the vertex-building continuation 597CB for
a memory-access experiment rather than another replacement of that native leaf.

`tools/prepare_sprite_stack_candidate.py` prepares a private candidate from the
pinned installed body. It caches only the guest stack page translation for the
loop before the first pop: 48 float loads, six float stores, 28 word accesses.
All actual reads/writes and float conversion/rounding remain in place. The
page pointer refreshes after every actual scheduler handoff; crossing-page
stacks fall back to individual original translations. The post-loop callee and
unwind remain original. Checked-address builds take the original routine.

This is **unqualified, not deployed**, and changes no production policy.
Private ../sprite-stack-candidate/reference.c contains generated code, excluded
from commits. VitaSDK O2 Thumb compilation succeeded; original/candidate text
sizes are 0x1d22/0x1d1a. An eight-byte difference is not evidence of runtime gain.
Next construct whole-context/arena differential cases covering four-corner loop
branches, stack/output aliases, page boundaries, scheduler stack/mapping changes,
x87 TOP/status and the 5BA10 callee. Only after correctness qualification should
Pi timing or a private Vita build be considered. Avoid caching stack values or
changing double intermediates to float as a shortcut.

Vita dashboard restoration was independently confirmed on perf275; the latest
3600-second lease succeeded. No Pi or Vita capture job remains active.

## Stack candidate differential qualification

`tools/tests/sprite_stack.c` compares the entire xctx, 8 MiB arena, page table,
and ordered full-context callee/handoff observations. Expanded 2,048 cases cover
all eight x87 TOP values, four corner-loop entry indices, both output-format
branches, aligned/unaligned within-page and crossing-page stacks, stack/output
aliasing, handoff remapping/stack relocation/input edits, and modeled 5BA10
memory/context effects. The callee is modeled, not actual integration coverage.

The first 768-case host sanitizer and Pi runs passed. Code review then found
that the generic pointer fallback for float accesses bypassed the original
split-page helper. Existing cases had not demonstrated a mismatch from that
issue; it was corrected explicitly, without weakening checks. The expanded
candidate uses original x87_load/store_f32 when the complete stack span does
not fit one page. Word accesses retain their original unaligned semantics.

Corrected private split-safe/ host ASan/UBSan and Cortex-A9 Thumb Pi builds each
passed 2,048 cases, 1,536 matching handoffs and 1,024 matching modeled callees.
A mutant omitting stack-pointer refresh after handoff fails the full comparison.
These are bounded synthetic results, not proof of all aliases or concurrent
kernel behavior. The Pi fixture ran on cores 0/1 and is terminal. No build was
installed on Vita; perf275 remains with a renewed lease.

Next: VitaSDK-linked instruction qualification including per-thread mapping,
then a focused cost test. Real-callee integration and ordinary physical-Vita
frame-time checks are still required before promotion. The candidate is not a
proven speedup; generated test bodies and binaries remain private.

## ARM cost and Vita-linked qualification

The source fixture now supports SPRITE_BENCH=1: 50,000 calls per format with
identical stack/object reset and modeled callee overhead included in both sides.
The split-safe formulation improved ordinary one-page cases but cost ~4% more
on crossing-page cases. The generator now declines those at entry to the
original function; post-handoff access fallback remains for relocated stacks.

Refined Pi Cortex-A9 Thumb O2 results (ns/call, original/candidate):
case 0 2473.8/1815.0; case 32 1722.3/1361.7; crossing cases 192
2442.3/2440.0 and 224 1618.0/1616.6. Supporting microbenchmark only, not a
predicted whole-frame gain. Full 2,048-case correctness checks also passed.

The fixture setup is shared by sprite_stack.c and sprite_stack_arm.c.
`test_arm_sprite_stack.py --thread-mapping` compiled using VitaSDK and executed
2,048 Cortex-A9 instruction cases: context, arena, mapping, ordered handoff/callee
trace and FPSCR all matched. Actual test callee/preemption code executed; libc
firmware copies were modeled. A distinct live table and TPIDRURW-bound table
exercise render-view accesses. This is not Vita3K or physical frame-time evidence.

Private gameplay/ replaces only 597CB in the 59550 diagnostic shard. Its first
compile exposed declaration ordering: shard-local X_G uses xram_/xpt_ variables,
so SPRITE_REFRESH must follow their declarations. The generator was corrected
and the rebuilt integration-safe/ candidate is now compiling. Standalone test
headers do not reproduce every shard macro override; before promotion, qualify
that actual preamble and inspect full-game integration. No runtime settings,
Vita package, draw order or save data changed. Poll the existing build rather
than starting another. No integration gameplay run has started yet.

## Final shard qualification and perf277 candidate

The integration build and 120-second Pi run have now completed (planned timeout
124, cores 0/1). The bounded fatal/panic/scope-omission search found no matches;
this headless run does not establish rendering correctness. Private evidence:
`sprite-stack-candidate/gameplay/{run-result,phase-summary}.json` and `run.log`.

The final candidate with the actual generated shard preamble passed all 2,048
VitaSDK-linked instruction cases, including context, memory, mapping, handoff
trace and FPSCR. Within-page executed instruction counts were 3,169,920 original
versus 2,246,528 candidate; crossing-page fallback was 3,197,744 versus 3,210,032.
Modeled copy volume matched. These instruction counts are not physical timing.

Perf277 builds successfully from perf275 with the candidate opt-in enabled only
for code_010.o. Package audit confirms that is the sole changed recompiled
object, and game-a.self/boot-game.txt are the sole changed package entries.
Runtime SHA256: 7d8ba31adb1fea060d8d58084639817a64943e67edccef792204e0c242aec062.
The remote update has started; installation and ordinary hardware gameplay
results remain unverified. Retain perf275 as rollback and protect a30-perf211.

Updater subsequently confirmed perf277 in slot 0 with the expected runtime
hash; independent status confirms V0.2.0-perf.277 / b2172f5a. Lease renewed and
normal protected a30 launch started. Perf275 remains in slot 1. Gameplay
measurement is pending; no frame-time benefit is established yet.

## Perf277 ordinary hardware trial (in progress)

Normal 360p settings, protected a30-perf211 save, scene-phase and worker timers
disabled. Loaded/active/director telemetry qualified readiness; screenshot
confirms the lifepod and AR. The settled 780-frame sample measured 55.702 ms
(17.953 FPS), p95 72.743 ms, maximum 93.019 ms; 523 frames exceeded 50 ms,
none exceeded 100 ms. Perf275's earlier ordinary run was 57.113 ms. This small
between-run difference is suggestive, not proof of a repeatable gain. The
50 ms sustained target remains unmet. Firing/movement capture is running; do
not restart it or claim wider regression qualification from the idle sample.

## Perf277 completed short gameplay sample

| Scene | Samples | Mean ms / FPS | p95 ms | Max ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|
| Lifepod | 780 | 55.702 / 17.953 | 72.743 | 93.019 | 523 / 0 |
| AR firing | 71 | 76.562 / 13.061 | 97.914 | 125.225 | 70 / 3 |
| Walk outside | 90 | 62.594 / 15.976 | 82.253 | 270.353 | 88 / 1 |
| Outdoor stationary | 952 | 47.494 / 21.055 | 57.739 | 88.088 | 147 / 0 |

Screenshots confirm pod/AR, ammunition consumption/reload, and the canyon after
walking outside. No crash observed; bounded fatal/panic/scope-omission/nonzero
mismatch/retirement-failure scan found no matches. This is not proof of AI
combat, audio, opening cutscene, checkpoint resume, or a 15-minute stress test.
The isolated 270 ms movement stall remains; do not ascribe it to a checkpoint
without a correlated write record.

Compared with earlier perf275 ordinary samples, mean times fell by 1.411 ms
(pod), 1.678 ms (firing), 1.716 ms (movement), and 0.975 ms (outdoors). No repeated
A/B loop was run; scene scheduling and run variation prevent a causal FPS
claim from these small differences. Retain perf277 provisionally as a stacked
candidate, perf275 rollback in slot 1. All collectors are terminal, controls
released, lease renewed. Private marks, logs, summaries and screenshots live
under sprite-stack-hardware/. Goal remains unmet: firing and pod exceed 50 ms
and even the outdoor mean hides 147 slow frames.

Next: inspect remaining 59550 effect/model preparation outside the optimized
597CB inner loop, using the existing child profile before adding instrumentation.
Prioritize removing repeated work without changing effect output, scheduler
handoffs or draw order. No further hardware A/B trial is queued.

## Remaining 59550 preparation: focused Pi probe

Source inspection finds a six-dword REP MOVS at 59667 and separate double
sine/cosine operations at 59672/5967F before the sprite loop. Runtime MOVS
already copies page-contained spans in bulk and preserves forward-overlap
semantics; replacing it with a blind memcpy is not a justified optimization.
59200 also contains point/direction-transform children, so its inclusive timing
cannot be treated as removable wrapper overhead.

A private probe in ../sprite-preparation-phase-pi adds scopes at those three
instruction addresses, including the generated x87 fallback copies. Initial
preparation stopped on the duplicate MOVS assertion before emitting a unit;
inspection confirmed one normal and one fallback occurrence. The final audit
requires two MOVS scopes plus normal/fallback sine and cosine scopes. Removing
all six observer pairs exactly restores the original 59550 body. Existing
597CB candidate and child observers remain unchanged.

The ARM harness built successfully. A single planned 120-second run is active
on Pi cores 0/1 (exec session 25252); no other harness/halo/compiler process
was observed on Pi before launch. Poll that handle and inspect run-result.json
and run.log when terminal; do not duplicate the run. Scope timings include
observer overhead and are not Vita FPS. Perf277 remains installed and the Vita
lease was renewed. No production code or math behavior changed this turn.

## Preparation probe result and rotation-transform candidate

The focused Pi run completed its planned timeout (124), with 30 parent reports.
The top-eight child listing censors two of the ten measured children: do not
report absent sine/cosine/copy scopes as zero. Settled parent samples were
roughly 2.0–2.2 ms; sine or copy occupied the eighth position at ~0.06–0.07 ms,
with other omitted children no larger. These scopes do not establish a large
trigonometry/copy bottleneck. Extra observers also increased the parent's
reported self time; that remainder is not solely removable guest work.

Inspection of 59200 identified rotation-only transform B5DF0, whose installed
perf277 body still uses memory-backed x87 slots. Historical perf175/177 splice
reports list a register-lowered version, but that alone does not establish an
accidental regression or authorize restoring all old lowerings. B5E40's known
NaN-payload mismatch remains excluded.

Extended the existing transform differential fixture with `--function rotation`
for B5DF0. The reference extracted from the old stage is byte-identical to the
installed perf277 function (SHA256
1e61bb09b5a6df6a402cdad385af1b406ea1d611a371551dab33ef85f56be95c).
Both host ASan/UBSan and Cortex-A9 Thumb Pi passed 1,728 full-context/4-MiB-memory
cases covering TOP, controls, aliases, page edges and exceptional values.
Timing observers are disabled in this offline test; concurrency and real
callers are not covered. Pi core 0 warm cost, including context reset: original
145.8–147.6 ns/call versus register lowering 122.4–123.6 ns/call. This is not
a Vita FPS estimate. All Pi jobs in this entry are terminal.

Private evidence: rotation-transform-candidate/{host.log,pi.log,baseline-audit.json},
build receipts under host/ and pi/. No candidate is staged or deployed. Next
check B5DF0's frequency beneath 59200 and qualify its real-callee integration
with the current sprite candidate before deciding if this small leaf is worth
a Vita trial. Perf277 and the protected a30 save remain unchanged.

## Rotation real-callee integration completed

Private rotation-transform-candidate/gameplay starts from the previous sprite
integration harness, replaces B5DF0 only in code_016, and adds two 59200 child
scopes in code_010. Its reference body matches both the installed build and
the older Pi harness. A private scene reporter prints uncensored counts for
parent 59200 before the ordinary top-30/top-eight lists consume them. No game
state, scheduling or rendering policy changed. Build succeeded; one 120-second
Pi cores 0/1 run finished with planned exit 124. Bounded fatal/panic/nonzero
mismatch/scope-omission checks found no matches. This headless integration is
not a visual, audio or physical-Vita correctness test.

Across 34 reported 60-frame groups, each transform child ran 387,518 times
(~189.96/frame). B5DF0 totaled 190,111 us (~0.0932 ms/frame); B5EA0 totaled
195,129 us (~0.0957 ms/frame). Timings include observers and are not Vita
measurements. The rotation leaf is a small qualified candidate, not evidence
of a large missing optimization. Retain its fixture and private candidate for
a future justified bundle; do not launch a standalone Vita trial for this
small Pi subtree. Both build and run are terminal; no job needs resuming.

A source inventory lists 302 functions with old memory x87 bodies and an
available historical register body. This is only an inventory: some have
native replacements, intentional exclusions, or changed code. Do not blanket
restore them or call this a regression count. Private lowering-inventory.json
records the candidates. B5E40 remains excluded for the known NaN discrepancy.

Next larger boundary: 66510's 66390 draw-dispatch child (~0.42 ms of ~1.67 ms
on the older Pi profile). Inspection shows calls into 7A130/7A1F0/7A2F0/7A3D0,
so the inclusive cost is not free-standing transform work. Attribute its active
draw path before proposing a wrapper rewrite; preserve existing draw ordering
and consult prior 7A130/material studies to avoid repeating them. Perf277 is
still installed; no hardware update or FPS claim follows this Pi run.

## Effect-specific route attribution and current-worker follow-up

The private effect-draw-route-pi run completed its planned 120-second timeout
(124). Six call scopes in 66390 are reversible to the original body, and
uncensored child records precede top-list consumption. Last ten 60-frame
reports: 7A130 67 calls/frame, 0.6843 ms/frame; 7A3D0 3 calls/frame, 0.0645 ms;
7A540 3.735 calls/frame, 0.0756 ms. Earlier transient reports had much higher
7A540 activity: do not generalize those to settled gameplay. No bounded
fatal/panic/nonzero-mismatch/scope-overflow matches found. These are older
headless Pi elapsed observations, not current Vita costs.

This supports the previous shared indexed-draw finding, not a new wrapper
rewrite. Source inspection confirms sequential-index retention already skips
scan/copy, quad-list rewrite already has the static-prefix optimization, and
vertex capture starts at base_vertex*stride. Do not reimplement those paths.

Next, a single current-hardware detailed draw profile is prepared under
recording277-diagnostic/. Perf277 remains the executable. The normal protected
a30 launch changes only instrumentation: add XV_DRAW_PROFILE=1, omit the
explicit XV_FRAME_QUEUE_TIMING override to fit 32 entries. Scene-phase timers
stay off. These measurements perturb frame times and will not be used as a
normal FPS comparison. Announced interruption, companion quit completed and
launch returned; dashboard boot still needs live confirmation before the
prepared launch script. Restore normal startup settings after the capture.

Dashboard status subsequently confirmed perf277, timing_frame=0. The transient
connection refusal during startup resolved without another launch. Lease
renewed; recording277-diagnostic/launch-a30.py is active (session 44098).
Poll it before starting collect-idle.py, then inspect the pod screenshot and
collect-gameplay.py. Require [draw-prep] and [draw-prep-sub] output before
claiming the diagnostic activated. These collectors' FPS summaries are
instrumented and must not be compared as speed results. After capture, restart
to dashboard to clear startup overrides and renew the lease.

## Current recording diagnostic completed; normal startup restored

Perf277's detailed draw profile activated (draw-prep/sub lines confirmed),
loaded the protected pod, and completed idle, 12-second AR/reload, movement
and outdoor captures. Screenshots confirm ammunition consumption and canyon
geometry. All collectors are terminal; controls released. Companion restart
returned to perf277 dashboard, timing_frame=0; lease renewed. Startup-only
overrides cleared without modifying xita.cfg. Receipt: recording277-diagnostic/
restored-dashboard.json.

Elapsed recording-stage means (ms/frame, diagnostic only):

| Stage | Pod (10 reports) | AR/reload (1 full report) | Outdoors (14 reports) |
|---|---:|---:|---:|
| Setup | 0.435 | 0.612 | 0.512 |
| State | 0.855 | 0.968 | 0.898 |
| Indices | 4.445 | 5.034 | 3.901 |
| Program | 0.859 | 1.006 | 0.841 |
| Streams | 22.701 | 18.732 | 14.278 |
| Constants | 0.752 | 0.914 | 0.994 |
| Textures | 2.598 | 3.104 | 2.710 |
| Diagnostics | 0.242 | 0.230 | 0.251 |
| Capture submit (inside streams) | 21.302 | 17.331 | 12.818 |

Private report-windows.json associates preceding periodic records with the
following frame-us group. This is coarse window association, not exact
per-draw/frame alignment. AR/reload has only one complete window; do not
generalize it to sustained NPC combat. Stages include waits, preemption and
clock overhead; nested rows cannot be added. Instrumented pod 64.477 ms and
fire 82.535 ms are not optimization acceptance measurements.

The firing penalty is not explained solely by capture submission: that
measured component decreased while the frame slowed. Preserve the game-update
lead. Steady capture pressure is nevertheless substantial: diagnostic queue
full events ~22/frame, arena pressure zero. The ordinary perf277 pod log also
shows 737–806 queue-only events per 60-frame tail reports (~12–13/frame), so
this is not solely a profiling artifact, though instrumentation magnifies it.
It differs materially from the old perf202 evidence of 31 events/60 frames
in d3d-record-speed2.md. Existing partial waits retain arena/slot lifetimes.

Next concrete candidate: configurable capture queue depth (32 default, test
64/128 privately), preserving FIFO callback order, immutable inputs, arena
reclaim drains, unsigned ticket wrap and GPU-slot retirement. First extend
the existing real-worker tests for capacity and wrap boundaries; do not
assume fewer queue-full events improve total frame time or bypass callbacks.
No deeper queue has been implemented or deployed yet.

## Configurable capture depth implemented; offline qualification complete

Added build-time XV_CAPTURE_JOBS=32/64/128 (default remains 32), with source
and Make validation. The capacity controls CPU job storage and ring masking;
GPU slots, capture arena, wait batch, callbacks and drain boundaries are
unchanged. Capture storage config tracks depth, so changing it rebuilds the
owning capture object. No larger queue has been installed on the Vita yet.

The real-worker fixture now fills the configured capacity, verifies the first
excess submission, and checks partial retirement at batch 1/8/16. Combined
arena/queue exhaustion scales payload bytes by capacity to reach both limits
simultaneously. Existing wrap, failures, source mutation, reuse, ordering and
three GPU-slot lifetime cases remain active.

Validation completed successfully:
- tools/test_vertex_capture.py --jobs 32, 64, 128: 24 feature/notification
  combinations each, using production capture/upload/copy worker code.
- SANITIZE=1 tools/test_vertex_capture.py --jobs 64: all 24 combinations,
  AddressSanitizer and UndefinedBehaviorSanitizer.
- tools/test_vertex_capture_notify_build.py with and without --depth:
  defaults, changing/repeating/restoring options, capture-only rebuild, invalid
  inputs rejected.
- Pi Cortex-A9 Thumb static binaries, taskset cores 0/1, depth 64 and 128:
  packed/compact/reuse/persistent/ready/notify enabled, all fixture cases pass.
  The Linux cross compiler lacks vld1q_u8_x4; these two queue correctness
  binaries used -U__ARM_NEON -U__ARM_NEON__ to exercise the existing scalar
  packing implementation. This does not qualify Vita NEON performance or
  replace a Vita SDK build/hardware trial.

Private logs/binaries: ../capture-depth-candidate/. All test sessions terminal.
Live status confirmed perf277 at dashboard, timing_frame=0; lease renewed.
Next: build a versioned depth-64 Vita candidate on the retained perf277 build,
verify package/object scope, then one ordinary protected a30 gameplay run.
Measure total frame times and queue/arena pressure; fewer full-queue events
alone are not a success. Preserve perf277 rollback and existing settings.

## Perf278 depth-64 package built

Private capture-depth-candidate/build-x87 retains perf277's generated code and
private build overrides, adds only the qualified Make depth block and capture
source, and sets XV_CAPTURE_JOBS=64. Vita SDK build succeeded. All recompiled
game objects are byte-identical to perf277. Changed objects: capture worker,
plus main/remote/UI/dashboard version-label consumers (their source files
are unchanged). Package replaces only game-a.self and boot-game.txt; update
contract remains 775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
Runtime SHA256 27f4461aaeed4c9bde5f341259bb7975603df5c26ba75a1ce711344d0887bafb;
package SHA256 269b65d1a71fab7ea14e2f8137251d54d89b98505f872f048631343d8f2edcc7.
Hardware upload/apply started; installation and gameplay remain to verify.

Perf278 installed successfully: updater verified runtime hash and confirmed
boot slot 1; independent live status reports 0.2.0-perf.278 / 06e32786,
timing_frame=0. Perf277 preserved in slot 0. Protected normal-settings launch
started via capture-depth-candidate/launch-a30.py; script renews keep-awake
lease and releases controls in finally. No FPS result yet.

Launch sequence completed normally (session 29193 terminal), screenshot
review shows the loading screen, not gameplay. collect-idle.py is live in
session 72365, polling loaded/active/director telemetry before its 45-second
pod capture. Poll that existing handle; do not restart due to loading alone.
After completion inspect idle screenshots, then run collect-gameplay.py for
AR/movement/outdoor measurements. No new scene timing result claimed yet.

## Perf278 ordinary gameplay: depth 64 not retained

The protected a30 load completed. Screenshots reviewed: expected lifepod,
AR firing/ammunition consumption, then outdoor canyon. Both collectors are
terminal, controls released. Measurements are CPU Present intervals, 360p,
normal settings and no detailed draw profiling:

| Scene | Samples | Mean ms / FPS | p95 ms | Maximum ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|
| Pod | 720 | 56.906 / 17.573 | 74.731 | 103.073 | 503 / 1 |
| Five-second AR fire | 61 | 84.737 / 11.801 | 103.018 | 128.112 | 60 / 5 |
| Movement | 79 | 66.965 / 14.933 | 90.629 | 250.920 | 74 / 1 |
| Outdoors | 875 | 51.720 / 19.335 | 61.773 | 103.603 | 464 / 1 |

Previous perf277 samples were 55.702 / 76.562 / 62.594 / 47.494 ms respectively.
These separate ordinary runs are not controlled statistical proof of a
regression, but show no reason to retain depth 64. Queue-only events fell from
737–806 per 60-frame tail reports to 383–495; arena pressure remained zero.
Inline completed-result reuse also fell (106–269 versus 448–507 per 60 frames),
consistent with fewer opportunities for the conservative empty-queue shortcut.
This is a possible mechanism, not a measured causal attribution.

No observed crash in this short run; this does not satisfy long-session,
cutscene, AI/audio or checkpoint validation. One movement interval reached
250.920 ms; do not conceal it in an average or attribute it without correlation.
Rollback requested to perf277. First startup status connection refused;
recheck the same reboot rather than issuing another restart. No depth-128
hardware trial justified by this result. Keep default 32 and preserve the
optional depth tests. Next useful measurement is wait-only capture timing:
current XV_VERTEX_CAPTURE_TIMING instruments every job, while the needed
split is queue partial-wait versus final-drain elapsed time. A separate opt-in
clock around waits would avoid thousands of per-job clock reads and distinguish
worker backpressure from owner-side capture work. Callback collection remains
included unless explicitly timed separately; neither is CPU-cycle measurement.

Rollback independently confirmed: perf277 dashboard timing_frame=0, slot 0,
expected runtime SHA256 7d8ba31adb1fea060d8d58084639817a64943e67edccef792204e0c242aec062.
Lease renewed for 3600 seconds. Receipt: capture-depth-candidate/
restored-dashboard.json. No live collector or held controls remain.

## Wait-only capture instrumentation qualified offline

Implemented XV_CAPTURE_WAIT_TIMING=1 (default off), owner-only timestamps at
wait entry, completion observation before cap_collect, and after cap_collect.
Reports [vertex-capture-wait-only] separately for queue partial waits and full
drains: call counts, observe us, publish us. These intervals are disjoint within
a wait and include scheduling/preemption; they are not exclusive CPU costs or
additional frame time. The existing broad per-job timing is unchanged and can
remain disabled. Empty drains perform no clock reads. Report resets counters;
shutdown resets startup option. No worker or resource lifetime policy changed.

Production-worker tests passed in all 24 host feature/notification combinations.
Then added partial-wait timing assertions and reran all 24 under ASan/UBSan.
Tests verify exactly three owner clock reads per enabled full/partial wait,
zero disabled/empty-drain reads, delayed callback attributed to publication,
exact output/callback counts, reset behavior, batches 1/8/16, plus the existing
ownership/failure/reuse/wrap suite. Pi A9 Thumb static binary (cores 0/1,
all reuse/packed/persistent/ready/notify options) passed. As with the depth
fixture, Linux GCC's missing vld1q_u8_x4 requires existing scalar packing via
-U__ARM_NEON -U__ARM_NEON__; this qualifies queue instrumentation behavior,
not Vita SIMD performance. Private logs: ../capture-wait-diagnostic/.
All tests terminal. Hardware still perf277; lease renewed. Next build perf279
with depth 32 and this instrumentation, use a single diagnostic gameplay run,
then clear startup overrides. Do not claim its timing as an FPS improvement.

## Perf279 wait-only diagnostic package

Built from the retained perf277 private build, depth 32, with capture source
from 55e73b9f and matching Make configuration. Vita SDK build passed. Object
audit: capture plus unchanged-source version consumers main/remote/UI/dashboard;
all generated game objects byte-identical to perf277. Packaged only game-a.self
and boot-game.txt over perf277, same launcher/assets/update contract.
Runtime SHA256 d67fb05291efb885f08824c4678de92db5c33aa0e18d62c0b18f76166111d60c;
package SHA256 1d67d8c7e5640f4278c5332c77a1c8dd689b3ffe26d27cde0d514b1932e74b37.
Upload/apply underway. Private directory: ../capture-wait-diagnostic/.
Prepared launch preserves protected a30/settings, replaces only explicit
XV_FRAME_QUEUE_TIMING override with XV_CAPTURE_WAIT_TIMING=1 to fit 32
startup entries. Broad capture and draw profiling remain off. Diagnostic fire
is 12 seconds to span report windows (not comparable to five-second FPS test).
Source confirms captured_draw_complete only marks failed commands; successful
callback is trivial, but publication includes target writes and scheduling.

Perf279 installed and boot-confirmed in slot 1 with expected runtime hash.
Independent live status: 0.2.0-perf.279 / 55e73b9f, dashboard timing_frame=0.
Perf277 remains slot 0 rollback. launch-a30.py is live in session 90379;
continue polling this handle before collect-idle.py. Script renews lease and
releases controls in finally. After idle screenshots/telemetry qualification,
run collect-gameplay.py and require [vertex-capture-wait-only] records before
claiming diagnostic data. Restore normal startup by dashboard restart afterward.
No current frame-time result or optimization gain claimed.

## Perf279 wait attribution completed

Launch and both collectors completed normally (all sessions terminal). Reviewed
pod, firing/reload and canyon screenshots. Wait-only records confirmed active.
An extra transition screenshot overlapped the early idle sample; excluded the
first 300 timing frames from wait analysis, leaving six complete pod reports.
Private summarize-waits.py associates each periodic report with the following
frame-us offset-0 group, selecting only complete windows within host brackets:

| Scene | Reports / frames | Partial waits/frame | Completion observe ms/frame | Partial publication ms/frame | Full observe / publication ms/frame |
|---|---:|---:|---:|---:|---:|
| Pod | 6 / 360 | 16.261 | 15.319 | 0.068 | 0.016 / 0.014 |
| AR/reload | 2 / 120 | 17.983 | 13.834 | 0.066 | 0.032 / 0.012 |
| Outdoors | 14 / 840 | 11.850 | 6.211 | 0.054 | 0.008 / 0.005 |

Movement has no complete matching report window; no timing attribution there.
These are elapsed owner intervals including preemption; not CPU-only work or
necessarily time removable from the critical path. Publication is tiny, full
lifetime drains nearly empty, partial queue waits dominate. Firing still gets
slower while measured capture waiting decreases: keep separate simulation/
effect cost investigation. Do not subtract these results from older heavily
instrumented draw-profile runs to infer CPU self time.

Diagnostic overall intervals (not candidate speed acceptance): fire 168 samples,
73.864 ms / 13.538 FPS, p95 100.436, max 133.132, 9 >100 ms; movement 78 samples,
68.161 ms / 14.671 FPS, p95 87.675, max 308.844, one >200 ms; outdoor 938 samples,
48.192 ms / 20.750 FPS, p95 57.946, max 88.721, 208 >50 ms. Fire is 12 seconds,
not previous five-second ordinary sample. No long-session or AI/audio/cutscene
qualification claimed. Hardware goal remains unmet.

Thread log confirms capture worker C0 priority153, while existing deferred
recorder is configured C0 priority152. cap_run processes FIFO jobs with a
per-job device barrier and completion/event publication; captured_draw_complete
only marks failed commands. Next investigate reducing same-core handoffs and
worker service cost, not larger capacity or callbacks. Existing wait batch16
is offline-tested; prior hardware batch16 notes refer to deferred recorder
notification, distinct from XV_CAPTURE_WAIT_BATCH (currently8). Verify whole
frame before retaining any scheduling candidate. Do not remove device ordering
without proving each path's writes and publication requirements.

Companion quit/launch completed to clear startup diagnostic overrides; first
status connection refused during boot. Recheck same boot before any restart.

Dashboard restoration is not yet verified. Follow-up status polling session
17001 completed after 30 attempts with connection refusal; this is an observation
timeout, not proof the application terminated. No additional restart issued.
Companion remains reachable (version 1.07) and `nosleep on` returned enabled.
All collection/launch/poll handles are terminal, no controls held. Hardware
has perf279 installed, but in-app lease cannot be renewed until its endpoint
returns; companion suspend prevention is enabled. Check endpoint or obtain
independent application state next, rather than treating timeout as a crash.

## Grouped completion publication candidate (offline only)

The remote endpoint still refused connection on the next turn. Asked user for
screen state through the asynchronous question; no reply yet. No additional
restart issued or sleep/termination inferred. Continued independent work.

Added XV_CAPTURE_PUBLISH_BATCH=1/8 (default1) as a capture-only build option.
With coalesced notifications enabled, worker executes up to eight FIFO jobs
before its device barrier and completed release. It publishes earlier at an
armed owner completion frontier or the current submitted tail; no future job
or notification is needed to finish a short batch. Non-coalesced builds keep
per-job behavior. CPU job capacity remains32; arena and GPU slots unchanged.

Ownership rationale: owner does not collect these jobs until completed acquire;
worker-only result cache is not read by the owner while predecessors remain
unretired. Every group retains a device barrier before completion publication.
New or partial/failed uploads therefore remain ordered before callbacks/targets
become visible. No upload, device fence at a publication boundary, callback,
source snapshot, full drain or GPU-slot retirement is removed. Grouping changes
publication frequency and may reduce barrier/event bookkeeping, not the amount
of game simulation. Improvement remains unproven, especially because armed
wait targets can shorten groups.

Validation:
- Host production-worker matrix: all24 configurations pass with option1 and8.
- ASan/UBSan option8: all24 pass, including unsigned wrap, failure/fallback,
  sparse/packed/private inputs, persistent/reused results and GPU-copy slots.
- Forced eight-job burst verifies one capture-worker device barrier and one
  notification decision with batching8, versus8 with default1; FIFO output
  order/bytes verified. Adjusted the former per-job notification-count assertion
  to the intentionally grouped contract; it was the initial test failure.
- Partial wait batches1/8/16 still stop at their armed frontiers; test gate skips
  count publication groups. Existing completion/wait races retain coverage.
- Pi A9 Thumb all-feature fixture, cores0/1, batch8 passed. Uses existing
  recomp/host/neon_x4_compat.h for GCC13 x4 intrinsics, retaining NEON packing;
  mocked Vita kernel/device services mean this is correctness evidence only.
- Make default/no-op/change/restore/invalid-option tests pass for publication,
  depth and notification options; only capture object rebuilds.

Private artifacts: ../capture-publication-candidate/. All test handles terminal.
No candidate deployed. Next build a versioned batch8 candidate preserving
perf277/279 generated code and queue32, then ordinary hardware gameplay once
connection/application state is resolved. Do not promote default or claim FPS
from fewer barriers alone. Hardware goal and full acceptance remain unmet.

## Perf280 candidate built, awaiting hardware connection

Previous turn made progress through implementation and offline qualification.
Rechecked endpoint this turn: connection refused, user screen-state reply still
pending. Companion `nosleep status` confirms enabled. No additional restart or
claim of application termination made.

Built perf280 / f62e6553 from retained perf279 private tree, synchronized only
capture source and owning Make block, XV_CAPTURE_JOBS=32 and
XV_CAPTURE_PUBLISH_BATCH=8. Vita SDK build session64558 completed successfully.
All generated game objects match perf277 byte-for-byte. Changed objects are
capture plus version-label consumers (main/UI/remote/dashboard sources unchanged).
Repacked only game-a.self and boot-game.txt over verified perf277 VPK; asset
contract unchanged. Reproducible private audit-package.py and receipts under
../capture-publication-candidate/.
Runtime SHA256 349e8686392a2067b901d3bc1879d383b115425e969d0546e76b74f9b1373baf;
package SHA256 23a2da1b266f402575479ee23c78e0671ea35491ef515d19d553964f08456cda.
Package: capture-publication-candidate/xita-perf280c.vpk.

Prepared normal-settings launch/idle/gameplay scripts assert perf280, retain
protected a30-perf211 and five-second fire, and do not enable wait-only or draw
profiling. Candidate is not installed or hardware-qualified. Installed last
confirmed perf279 slot1, perf277 rollback slot0; current application state unknown.
All local build/test handles terminal. Next hardware step: resolve endpoint/
screen state, install verified candidate, then one ordinary gameplay capture.
Do not replace this required validation with more offline success claims.

## Hardware access blocker audit

Endpoint again refuses connection after the same condition across four goal
continuations. Companion reachable; no-sleep remains enabled. User screen-state
question unanswered. Package checksum independently reverified; worktree clean.
Last turn was progress (Vita SDK build/package audit), not an idle wait. No live
build/collector handle remains. The evidence-backed candidate's implementation,
host/sanitizer/Pi qualification and package preparation are complete; its next
required step and acceptance require the physical Vita. More offline checks
cannot establish frame improvement, and further scheduling changes before that
result would obscure attribution. Goal blocked on restoring/identifying the
Vita application state; not complete. Resume with live endpoint/screen state,
install perf280 and ordinary protected a30 capture. Preserve perf277 rollback.

## Hardware connection restored; perf280 deployment underway

On the resumed goal turn, independent status returned perf279 / 55e73b9f at
dashboard timing_frame=0. Blocker cleared; renewed lease3600 and companion
nosleep. Reverified perf280 package checksum and started authorized update/apply
(session15623), not a speculative relaunch. Confirm boot before gameplay.
Slot rotation will retain perf279 as immediate rollback; perf277 verified VPK
remains local. Perf279 with profiling off retains the baseline behavior and
queue32; perf280 adds grouped publication8. No new FPS claim yet.

Perf280 updater completed successfully: slot0, verified runtime hash
349e8686392a2067b901d3bc1879d383b115425e969d0546e76b74f9b1373baf.
Independent status confirms perf280/f62e6553, dashboard timing_frame0, awake3589.
Normal protected a30 launch is live session33657; poll it to completion before
collect-idle.py. Then inspect pod screenshots and collect-gameplay.py (five-second
AR, movement, outdoor). No wait-only/draw profile override. Perf279 retained
slot1. Do not confuse this launch with the completed perf279 diagnostic handles.

## Perf280 ordinary hardware result: grouped publication not retained

Launch33657, idle98912 and gameplay40388 all terminal. Inspected pod,
AR/reload and outdoor screenshots; controls released. No crash observed in this
short run. Ordinary 360p settings, five-second fire, no wait-only/draw profiling:

| Scene | Samples | Mean ms / FPS | p95 ms | Maximum ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|
| Pod | 720 | 58.658 / 17.048 | 76.508 | 101.002 | 561 / 1 |
| AR fire | 65 | 81.669 / 12.245 | 101.903 | 140.516 | 64 / 4 |
| Movement | 78 | 67.974 / 14.712 | 87.082 | 261.939 | 78 / 3 |
| Outdoors | 928 | 48.660 / 20.551 | 58.816 | 93.486 | 206 / 0 |

Perf277 ordinary samples were55.702/76.562/62.594/47.494ms respectively.
Separate ordinary runs are not controlled proof of regression, but no speedup
is demonstrated. Grouping did reduce completion bookkeeping: tail reports show
~1932–1970 done signal+skip decisions/60frames, versus~14K queued jobs. Ready
inline draws only65–146/60frames in these tails. Owner FA920 elapsed51.5–51.7ms
includes waits; recorder drain4.38–5.28ms/frame overlaps and cannot be added.
Do not interpret reduced notifications as reduced critical-path frame time.

Requested rollback to perf279; confirmation poll session24193 active. Preserve
existing cumulative optimizations, keep publication default1, retain optional
candidate/tests and findings. Next evidence-backed scheduling candidate is
capture-worker placement: current capture and deferred recorder both C0,
prior wait-only profile15.3ms owner queue waits in pod. Audit an opt-in C1
capture affinity while preserving priority/FIFO/ownership; assess competition
with C1 model work and total frame time, not aggregate core utilization.
No affinity change implemented yet. Still need full cutscene/combat and15min
acceptance; goal remains unmet.

Rollback confirmed independently by perf279 version and expected runtime hash;
dashboard timing_frame0. Lease renewed3600. Receipt:
capture-publication-candidate/restored-dashboard.json. Poll24193 terminal;
no live collectors or controls remain. Diagnostic startup overrides absent.

## Capture C1 affinity candidate implemented, host-qualified

Added startup XV_CAPTURE_CORE=0/1, default0 and invalid fallback0. Capture
thread uses matching VitaSDK CPU mask; priority remains creator+1, stack32KiB.
Startup log reports actual requested core. Queue capacity, publication, waits,
source/result ownership, upload slots and GPU barriers unchanged. No shared
renderer/gameplay task reassigned. Option is uninstalled; hardware stillperf279,
lease renewed this turn. Candidate should use32jobs/publication1, not perf280's
rejected grouping.

Tests: existing production-worker matrix24 configurations passes with C1
requested; ASan/UBSan matrix24 passes. New selection cases cover absent,0,1,
invalid2/-1/text and shutdown/reinitialization; thread fixture verifies affinity,
unchanged priority/stack and upload worker C0. Added optional XV_TEST_PIN_CORES
host fixture pinning: main producer C0, capture C1, copy worker C0. All24
configurations pass with actual host pthread affinity, retaining FIFO/race/
publication/wrap/source mutation/failure/GPU-slot tests. This is host correctness,
not Vita speed or driver validation.

ARM A9 Thumb/NEON fixture compiled using existing x4 compatibility header.
Initial compile needed -D_GNU_SOURCE before forced include to expose Linux
pthread affinity APIs; corrected. Pi transfer then failed connection closed;
follow-up SSH confirmed no route to192.168.0.9. No ARM execution result claimed.
Private ../capture-core-candidate/ has binary, exact command and host logs;
pi-core1.log records connection failure, not a passing test. All handles terminal.
Continue VitaSDK build/audit while Pi unavailable; repeat Pi pinned run when
reachable. Hardware whole-frame test remains necessary to determine whether
C1 model-worker contention offsets improved C0 recording overlap.

## Perf281 C1-placement build and deployment

VitaSDK build succeeded (session65847 terminal). Private capture-core-candidate
build retains perf279 generated code, queue32, publication1, and adds only the
new runtime capture affinity option plus version281/6e26db37. Package audit
confirms all generated objects identical to perf277; capture object and version
consumers changed, main/UI/remote/dashboard source unchanged. Only game-a.self
and boot-game.txt repacked; assets/launcher contract unchanged.
Runtime SHA256 1a5d477b9caab117f440e72dda5fac193cd71c497c3ec5d762eff275198865df;
package SHA256 934a2de8dae5bae87368dfd09c4a89120098f359aa856da606464ea7d0e97db8.
Prepared launch sets XV_CAPTURE_CORE=1 in place of explicit FRAME_QUEUE_TIMING
override to fit32 entries; other normal settings and protected save unchanged.
Wait-only/draw profiling off. Default remains C0 outside this startup override.

Pi recheck still no route to192.168.0.9; ARM binary execution remains unverified.
Host24/pinned24/sanitizer24 tests passed as recorded. Affinity change preserves
all publication/device barriers and single-producer/worker ownership; vertex
upload implementation has no guest-context/TLS/core binding dependency found
in this audit. Proceeding to authorized physical trial, without presenting host
checks as Vita driver validation. Update/apply active session56241; require boot
hash confirmation and actual C1 thread log before claiming the option exercised.

Perf281 update finished successfully, verified expected runtime hash and boot
slot0. Independent status confirms0.2.0-perf.281/6e26db37 at dashboard,
timing_frame0, awake3591. Perf279 remains rollback slot1. Protected C1 launch
active session86315; poll to completion before collect-idle.py, then normal
collect-gameplay.py. Verify [cpu-thread] vertex-capture affinity00020000/core1
or startup core1 log in readiness/idle logs before attributing results. All
build/update handles terminal; only launch is active. No FPS result yet.

## Perf281 C1 hardware trial: contention moved to scene helper

Launch86315, idle32181, gameplay21458 all terminal. Hardware thread log confirms
capture core1 affinity00020000 priority153 (actual requested placement), not just
an env receipt. Reviewed pod, AR/reload, canyon screenshots. No short-run crash;
controls released. Ordinary360p settings, queue32/publication1, no wait timing:

| Scene | Samples | Mean ms / FPS | p95 ms | Maximum ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|
| Pod | 600 | 67.667 / 14.778 | 77.020 | 120.595 | 600 / 7 |
| AR fire | 71 | 81.767 / 12.230 | 97.198 | 124.995 | 71 / 1 |
| Movement | 71 | 74.240 / 13.470 | 88.091 | 321.529 | 71 / 2 |
| Outdoors | 738 | 61.263 / 16.323 | 70.798 | 113.689 | 738 / 5 |

No speedup: pod and outdoor substantially slower than perf277's55.702/47.494ms.
Tail queue-only pressure became0, recorder drains fell to0.71–0.93ms/frame,
FA920 elapsed41.0–45.1ms. These local improvements did not improve the frame.
C1 utilization84–96%, C0~45–48%, C2~64–72% in tail windows. Scene helper CPU
44.5–47.6ms, wall63.8–67.3ms, ready26–28%, waiting1–2%; wall-minus-CPU~19ms
includes runnable-but-unscheduled time, not solely synchronization waits.
Previous perf277 tail scene CPU42.9–46.2ms, wall43.6–47.9ms. This supports
same-core contention with the scene helper, not more simulation CPU work or
GPU saturation. Frame acquire reports zero busy-slot waits; GPU retirement
latency~40ms overlaps and is not exclusive GPU service measurement.

Requested rollback to perf279; confirmation poll27198 active. Keep C0 default.
Next inspect baseline C2 workload/headroom before proposing alternate placement;
C1 has no demonstrated spare capacity. Preserve separate firing-cost lead.
Do not promote a change based on eliminated queue pressure or shorter FA920.
Pi still untested for this affinity feature; physical trial now supplies Vita
evidence, but no long-session/cutscene/AI/audio/checkpoint acceptance met.

Rollback independently confirmed perf279 and expected boot hash at dashboard,
timing_frame0; lease renewed3600. Receipt capture-core-candidate/
restored-dashboard.json. All handles terminal; no controls held.


## Baseline core-headroom and capture reuse audit

Re-read private `sprite-stack-hardware/idle.log` against the completed C1
trial. Baseline final four utilization windows report C0 74–77%, C1 81–82%,
C2 73–76%. Guest-present is fixed to C2, priority160. Vertex capture is C0,
priority153; texture decode C0/152, vertex upload C0/161, pump C0/160 and
an audio-or-profiler worker C0/64. These are observed thread assignments,
not an exhaustive core workload decomposition. Percent utilization alone
cannot establish an available contiguous scheduling budget.

Do not implement a C2 relocation from the C1 queue-pressure improvement alone:
capture would outrank the existing guest fiber and can move contention into
simulation. First reduce actual preparation work or measure its exclusive CPU
cost; baseline scene helper already spends approximately43–46ms CPU/frame.

The final baseline capture report has13,746 jobs/60frames but only847KiB
of newly captured data across that interval. Reuse reports16,320 exact hits
out of16,391 checks, avoiding110,837KiB staging writes, with4,938 worker
preparations reused and507 completed draws admitted inline. Low new-copy
volume is therefore not evidence that preparation itself is cheap: the
worker may still process sparse masks, cache lookups and upload validation.
`cap_execute` already bypasses upload on persistent and prepared-result hits;
non-sparse reuse is slot-specific. Sparse streams deliberately clear reuse
IDs because results depend on the reference mask. Next inspect the remaining
sparse/upload path and its existing counters before adding another cache or
changing scheduling. Preserve mask identity, slot retirement and FIFO callbacks.

Live endpoint confirms perf279/55e73b9f at dashboard, timing_frame0, with
3578 seconds awake lease remaining at inspection. No build or settings change
was deployed by this audit. Goal remains unmet; no new FPS claim.


## Dense upload revalidation dominates sparse-mask volume

The same perf277 idle log's final60-frame reports narrow the next target:

| Counter | Per60 frames | Per frame |
|---|---:|---:|
| Sparse masks captured |240|4|
| Sparse upload comparisons |120|2|
| Sparse compared KiB |765|12.75|
| All upload compared KiB |64550|1075.83|
| Resident checks/hits |9948/6566|165.8/109.43|
| Resident compared KiB |63785|1063.08|
| Actual copies / copied KiB |3382/14452|56.37/240.87|

These are requested comparison spans (early mismatch can exit early), not
physical bus traffic or exclusive CPU costs. Sparse-mask optimization is not
the leading volume target. The worker's dense resident validation dominates
requested upload comparison bytes. Capture reuse already validates live guest
bytes against its immutable snapshot; the upload worker then independently
validates its retired GPU mirror.

Re-read `persistent-vertex-uploads-20260919.md`: the earlier persistent cache
reduced worker preparation4.781→0.678ms but increased owner capture
5.978→10.716ms, with no total-frame improvement. Do not simply re-enable it.
Unlike that experiment, current capture reuse provides an existing exact
snapshot identity proof which may support eliminating a second validation.

Next candidate design: associate an immutable capture snapshot version with a
persistent GPU allocation only after a successful exact snapshot match. Reuse
that association without a second live-source byte comparison. This requires
explicit generation/liveness checks for BOTH capture-entry recycling and GPU
allocation recycling; an array index or source address is insufficient. The
owner must pin GPU storage before publishing each job, the worker must finish
new copies in FIFO order, and all referencing GPU slots must retire before
reuse/eviction. Sparse and packed streams initially retain existing paths;
changed bytes create distinct versions. Pending upload failures must not expose
uninitialized results. Capacity failures retain ordinary upload fallback.

Before implementation, establish how mappings are invalidated at CPU arena
reset, GPU slot retirement and shutdown, including synchronous fallback. Do
not retain ordinary `cap_results` across drains: existing pools can be reset
or overwritten after that boundary. A dedicated persistent allocation is
required unless a separate explicit upload-generation contract is introduced.
Qualification must exercise snapshot-ID reuse, allocation-ID reuse, partial
failure, three in-flight slots, callback mutation and pressure drains. The
intended gain is removing duplicate validation, not assuming guest data static.

Pi reachability retried with a5-second SSH timeout: connection timed out;
no ARM run occurred. Existing local work remains available. No hardware/source
behavior changed during this audit, and no speedup is established.


## Snapshot-linked persistent GPU prototype implemented (default off)

`XV_VERTEX_SNAPSHOT_GPU=1` is a new startup option compiled only with both
`XV_VERTEX_PERSISTENT=1` and `XV_VERTEX_CAPTURE_REUSE=1`. It selects the new
snapshot-linked path instead of the legacy live-source persistent lookup;
it does not require enabling the legacy runtime `XV_VERTEX_PERSISTENT` option.
All default settings and existing nonpersistent builds retain previous behavior.

The owner first obtains its normal immutable capture snapshot and reuse ID.
Dense raw entries can then associate that snapshot with a persistent GPU
allocation. A hit checks allocation generation, liveness, identity, size and
stride, pins this frame's GPU slot, and performs no additional byte compare.
New associations copy the immutable snapshot to the persistent CPU mirror;
the existing FIFO first pass uploads all promised GPU entries before handling
ordinary streams, even if one of those streams fails. Original GPU barriers,
FIFO callback ordering, and slot retirement remain in force. CPU drains do not
release GPU pins. Sparse/packed, uncached and capacity-failure paths fall back.

Each GPU allocation gets a64-bit monotonically increasing generation; exhaustion
fails allocation rather than wrapping. Recycled capture IDs clear their link.
Shutdown clears all resources and generations together. Mapping caches never
serve as guest immutability evidence: the capture lookup remains responsible
for input validation. Existing optional tag-trust behavior is unchanged.
`[vertex-snapshot-gpu]` reports selected mode and hits without second comparison.

Validation completed:
- All24 production-FIFO host configurations pass with new path default off.
- Persistent+reuse variants additionally exercise the new path: pending uploads
  after guest unmapping, read-only cross-slot reuse, changed same-address bytes,
  recycled GPU IDs with stale CPU links, recycled CPU IDs while old GPU storage
  remains pinned, and generation exhaustion fallback. Assert zero legacy
  persistent comparison bytes in the dedicated snapshot-link fixture.
- New mode also passes existing sparse/packed bypass, allocation/map/partial-job
  failure and240 mixed GPU-slot generation cases.
- Full24-configuration ASan/UBSan run passes, including those new-mode cases.
- VitaSDK compiles production capture with reuse/persistent/packed/compact/
  ready/notify enabled; this is compile-only evidence, not a linked VPK.

Private receipts: `snapshot-gpu-candidate/{host.log,asan.log,capture-vita.o}`.
No handles remain live. Renewed Vita lease3600 during work. Perf279 remains
installed; no frame-time claim. Next qualify new-mode capacity/fragmentation and
callback-pressure coverage explicitly, then build a versioned private candidate
with owning-object audit and conduct ordinary protected a30 hardware gameplay.
Pi remains unavailable from the preceding connection attempt; do not claim ARM
Linux correctness execution. Goal and long-session acceptance remain unmet.


## Perf282 qualification and package

Expanded new-mode coverage to existing page/metadata exhaustion and fragmented
allocation tests. Added a queue-full callback-mutation fixture: preflight sees
a reusable old snapshot, partial-wait callback rewrites its live source, and
the eventual job must capture the new bytes while the first job keeps the old
GPU version. Both versions are checked byte-for-byte. Normal24 configurations
passed capacity additions; ASan/UBSan24 configurations additionally passed the
callback fixture and all capacity cases. Receipts `host-pressure.log` and
`asan-pressure.log` in snapshot-gpu-candidate. No active test handles remain.

Full Vita build completed successfully as perf282/533a6c71. Private stage
based on perf279; changes are current capture/persistent header, version, and
owning capture object's persistent build feature=1. Runtime snapshot mode is
explicit opt-in via launch environment, leaving queue depth32, publication1,
core0 unchanged. No Pi execution claimed. Package audit and deployment receipts
will establish installed state separately; build completion is not deployment
or an FPS result.


Perf282 package audit passed: only capture object and four version-consuming
objects differ from perf277; all generated objects unchanged. Package changes
only game-a.self and boot-game.txt; contract unchanged775a1863...d4897.
Runtime SHA2561aee4affc198c5272a5c3845f56975869127e107890659dd9d01fa8dca8de13e;
package03a6dd968ba3e44646d5884cadb326d7bae98bfb043a88ed9e65602c322a2c89.
Full hashes and build receipt in snapshot-gpu-candidate/qualification-receipt.json.

Deployment session31798 confirmed live at last poll, uploading34,668,544 of
34,836,694 runtime bytes. Log snapshot-gpu-candidate/deploy-apply.log. Re-poll
this SAME handle through verification/reboot; do not restart update or infer
installation from upload. Prior live status was perf279 dashboard with lease
3594sec. After boot confirmation verify perf282/hash and renew lease, then run
prepared launch-a30.py and collect-idle.py (independent processes; no screenshots
inside the measured interval). New env replaces C1 placement trial with
XV_VERTEX_SNAPSHOT_GPU=1, keeps protected a30-perf211 and32 override limit.
No launch/collector has started, no controls held. No new hardware FPS evidence.


## Perf282 installed; Pi restored

Deployment session31798 completed0. Updater confirms verified runtime hash
1aee4affc198c5272a5c3845f56975869127e107890659dd9d01fa8dca8de13e,
slot0 and boot_confirmed=true. Independent status confirms perf282/533a6c71 at
dashboard, timing_frame0. Renewed lease3600; perf279 remains rollback slot1.
An intermediate connection refusal occurred during the authorized reboot;
we waited for the same update handle and did not restart it.

User restored Pi connectivity. Recompiled current production capture fixture
with ARM GCC13, Cortex-A9 Thumb/NEON hard-float and the existing x4-intrinsic
compatibility header (not scalar fallback). `arm-command.json` captures command.
All-feature fixture passed on Pi constrained to cores0/1 (`pi.log`), including
new generation links, pressure/fragmentation, callback mutation and partial
failure cases. A second run pinned producer/copy to core0 and capture to core1
also passed (`pi-pinned.log`). Both SSH runs terminal0. These establish ARM
correctness/concurrency evidence, not physical Vita throughput predictions.

Started ordinary protected a30 launch script session70215; last poll confirmed
live, log snapshot-gpu-candidate/launch.log. No collector started yet. Re-poll
same handle, then run collect-idle.py after launch completion. Source and
hardware unchanged during Pi tests. No perf282 gameplay result available yet.


## Perf282 first ordinary hardware sample: lifepod improvement

Launch70215 and idle collector14388 completed0. Loaded/active/director telemetry
qualified the sample; reviewed idle-before.png and idle-after.png show expected
lifepod scene, weapon and HUD. No extra hardware screenshots inside interval.
840 measured frames: mean49.054ms/20.386FPS, p5047.793ms,
p9559.023ms, p9965.203ms, max73.775ms;317>50ms, zero>100ms.
Earlier perf277 same ordinary pod sample:55.702ms/17.953FPS. Separate live runs
are not deterministic; this is promising evidence, not a sustained20FPS claim.
Private idle-summary.json and idle-comparison.json retain exact values.

Mechanism in final60-frame counters:11,625 snapshot GPU hits, zero second
persistent comparisons,192 new persistent versions uploading1439KiB; ordinary
upload comparisons6121KiB versus baseline64550KiB. Ordinary copies226/28KiB
versus baseline3382/14452KiB. These are separate windows, requested spans rather
than physical bandwidth. Queue pressure153 versus baseline806. Scene helper
CPU44.05ms, wall44.59ms; C0 tail56–59%, C1~89–90%, C2~80%. Scene work remains
large despite removal of capture/upload duplication. Legacy persistent log
labels enabled0 because the new snapshot selector replaces its lookup; the
separate snapshot-gpu row correctly reports enabled1. Do not misread this as
feature disabled.

Gameplay collector60982 is live, firing and movement brackets already captured,
currently proceeding to45-second outdoor sample. Re-poll same handle; inspect
saved screenshots and gameplay-summary.json when terminal. No full15-minute,
cutscene, AI/audio or checkpoint acceptance yet. Keep perf282 provisional.


Gameplay collector60982 completed0; controls released. Reviewed fire-after.png
(reload animation in lifepod) and outside-after.png (expected rocks/canyon,
weapon/HUD). Short-run no observed crash or obvious new corruption; not full
rendering validation. Exact results in gameplay-summary.json:

| Scene | N | Mean ms / FPS | p95 ms | max ms | >50 / >100 ms |
|---|---:|---:|---:|---:|---:|
|5s AR/reload|81|67.027 /14.919|89.115|114.060|79/3|
|movement|86|62.656 /15.960|75.412|280.910|85/1|
|outdoor|887|50.819 /19.678|67.850|355.055|343/6|

Earlier perf277 respective mean76.562/62.594/47.494ms. Pod and firing improve;
movement essentially unchanged; outdoors worse with2 frames>200ms. Keep282
provisional, not accepted globally and not goal complete. Next correlate slow
frames to checkpoint/streaming/tick/capture reports inside exact outdoor
brackets; post-bracket tail windows alone are not sufficient attribution.
Final persistent windows have zero capacity fallsbacks, so do not assume cache
exhaustion caused outdoor stalls without aligned evidence. No further test is
running; perf282 remains live in ordinary a30, lease renewed by collector.


## Enemy-activity report narrows the current wall to guest tick

User reports FPS drops as grunts/elites spawn. Pulled fresh enemy-report.log
(3,205,842 bytes) without controls or settings changes and renewed awake lease.
Latest180 complete frames end9660: mean97.779ms/10.227FPS, p95114.962ms, max123.959ms; all180>50ms,79>100ms. No synchronized
spawn marker, so this is the observed slow state rather than an exact spawn
cost. Screenshot enemy-report.png subsequently shows outdoor view, nearby
Covenant craft and radar contacts; it does not establish enemy counts.

Final windows: C2=99%, C0~29–31%, C1~32%. Guest FA920 outer elapsed
6,204,137us/60frames=103.402ms/frame (nested work/waits included).
Scene helper CPU34.50ms, wall36.18ms, owner join0.01ms; done->noticed70.33ms.
Thus the renderer finishes much earlier than the guest owner proceeds. This
strongly supports guest update pressure as the current critical path, not
insufficient graphics-worker core utilization. It does not distinguish AI,
physics, animation or multiple catch-up simulation ticks. Snapshot cache
continues hitting6675 times/60frames with zero final-window capacity failures.

Next inspect FA920->109760 tick count and its object/AI children during enemy
activity. Existing staged generated phase timers and owner-only mode2 are
available; startup latch means remote env alone cannot enable them live.
Audit counts saved in tick-instrumentation-audit.json. Important diagnostic
confound: XV_SCENE_PHASES>0 disables XV_ROOT_PAIR in existing math helper.
Any such diagnostic run is attribution-only and cannot be used as ordinary
FPS comparison. Preserve282 ordinary logs and protected save before restart;
no diagnostic restart was performed in this turn. No active collectors or
held controls. Enemy combat remains required for acceptance; goal unmet.


## Owner diagnostic prepared; save preservation in progress

Prepared private enemy-tick-diagnostic launch/collector scripts from282 with
XV_SCENE_PHASES=2, same version/cache/protected save. Scripts syntax-check;
not executed. Diagnostic collectors label timing as attribution-only because
phase mode affects root-pair optimization. No runtime code changed.

Paused gameplay and started read-only FTP backup of the isolated test-save
namespace to enemy-tick-diagnostic/save-before-quit, session40163. Save/profile
files copied first; script also recursively includes large cache maps (unneeded
for future save-only backups). Last poll confirms live transfer; do not treat
local partial files as a verified complete backup until receipt.json is written.

Remote menu sequence down3/cross displayed a warning that all level progress
would be lost. Did NOT confirm it. Sent circle to cancel, then down; subsequent
image unexpectedly showed lifepod rather than menu. No further controls sent.
Cannot claim Save and Quit succeeded or attribute this transition confidently.
Initial pause image showed outdoor mission objectives. Preserve images and
pre-quit backup; inspect saved file hashes before restarting. Backup started
before navigation but was still reading cache maps during menu actions, so
verify checkpoint files with a second read before treating them as stable.

Current live status still perf282/533a6c71, timing_frame17628, awake3574sec.
No diagnostic restart or launch performed. Re-poll backup40163 to terminal,
verify save/profile files (exclude map cache), then restart same282 with
owner-only timers through the prepared launch. No current game controls held.


## Save verification completed; owner diagnostic launching

Backup40163 terminated with a size assertion while copying mutable map-cache
files. Do not claim the entire directory image complete. Independently read
all15 save/profile files under udata/tdata again and verified SHA256 against
local copies:15/15 identical. Separately enumerated remote udata/tdata recursively
and verified exact file-set equality. Private save-verification.json and
save-file-set.json establish the protected test checkpoint/profile backup;
cache files are excluded from this claim. No restore or save write performed.

Companion quit XITA00001, waited4 seconds, then launched same282. Observation
session70917 completed0 with independent perf282 dashboard/timing_frame0;
lease3600 renewed after boot. Receipt enemy-tick-diagnostic/restarted-dashboard.json.
Started prepared launch-a30.py session79949 (live); launch.log records sequence.
Owner-only mode2 and snapshot GPU1, same a30-perf211 namespace. No update or
binary change. Current root-pair diagnostic confound remains documented.

After same launch handle completes, start diagnostic collect-idle.py, review
scene, then collect-gameplay.py. Prepared collect-enemy.py supplies another
120-second no-input outdoor observation with status marks and final tick log;
run after movement collector completes. It never toggles settings or calls
benchmark mode. Review pictures/telemetry before identifying enemy activity;
spawn time/count is not automatically known. All diagnostic durations are
attribution data, not ordinary FPS acceptance. No active save transfer remains.


## Owner-only diagnostic completed in quiet outdoor state

Launch79949, idle56250, gameplay93311 and outdoor-observation97020 all completed0.
Saved diagnostics under enemy-tick-diagnostic; reviewed idle-before and
outdoor before/after images. Outdoor images show canyon/trees and no radar
contacts, unlike user's prior enemy-report image with craft/radar contacts.
120seconds without further controls did not reproduce C2 saturation. Do not
label enemy.log as a verified enemy-combat profile merely because of filename.
No further controllers are active. Asked user asynchronously where enemies
appeared; answer pending, not a prerequisite for independent investigation.

New private extract-ticks.py preserves raw owner report rows and parent/callee
inclusive edges/call counts. JSON records next frame report as a contextual
marker, not proven exact alignment. Nested times must not be summed.
Last quiet outdoor report: FA92037.47ms/frame,10976026.22ms across1.517ticks/frame;
900E022.41ms,8FB7021.37ms; realtime108FD011.15ms. Object transform/update
8DDF09.88ms, particle10E7A06.16ms. C2~85%, C1~87–90%; scene wall44.7ms,
done->noticed4.5ms. Prior user slow state had C2=99%, scene36ms and
~70ms done->noticed, so the crucial slow condition has NOT been sampled with
the detailed timers yet. Next reproduce actual enemy-active location before
choosing which native routine to rewrite.

Diagnostic quiet pod final report: FA92052.76ms,10976039.57ms across1.65ticks/frame,
8FB7034.02ms,4B9D010.35ms,108FD013.10ms. These are instrumented elapsed
values including nested work, overhead/preemption and root-pair disabled;
not comparable to ordinary perf28249ms pod frames as a regression claim.
Protected save15-file backup remains verified; hardware still282 with startup
XV_SCENE_PHASES=2. Restore ordinary startup before any FPS acceptance run.


## Traversal reproduces sustained owner pressure; six additional objects

Moved forward8s, turned right0.65s/forward7s, then left0.3s/forward8s, releasing
controls each time. Screenshots encounter-advance1/2/3 show terrain/tree/rock
approach; third view faces a nearby cliff, not confirmed enemies. advance3.log
shows C2~97–98%, helper22–23ms and done->noticed47–51ms. Looked back1.2s right,
released input, then captured30seconds stationary (advance-stationary.log and
marks). All handles6773/71885/15734/12479 terminal; no controls held.

Pressure persists stationary: C2~97–98%, helper44–45ms, done->noticed20–22ms.
This rules out *only* the held movement input as the explanation. Last complete
phase windows compared with prior quiet outdoor report (same diagnostic build):

| Quantity | Quiet | Advanced, stationary |
|---|---:|---:|
|Simulation ticks/frame|1.517|1.783|
|109760 ms/tick|17.288|29.232|
|Top-level 8FB70 calls/tick|208|214|
|8FB70 inclusive ms/tick|14.090|23.142|
|8DDF0 calls/tick|210|222|
|8DDF0 inclusive ms/tick|6.514|8.187|
|4B9D0 biped calls/tick|4|10|
|4B9D0 inclusive ms/tick|0.699|5.209|

AI14A162 now5.12ms/frame (~2.87ms/tick); prior quiet report did not list it
among top30, which is not proof of zero. Realtime particle cost6.16→6.76ms/frame
is comparatively similar. Parent times overlap. Private activity-cost-comparison
JSON stores source values; realtime particle per-tick ratios are descriptive
only, because that pass runs once/frame. This supports additional active objects
and more expensive simulation, not just more catch-up ticks or render geometry.
Visual identification/count of enemies is still unproven; user location question
pending. Do not state six additional objects are six enemies as established fact.

Next audit remaining biped collision work using existing biped-phase-candidate
notes before adding any native routine. Native4B9D0 already covers query/solver
subtrees and remains active. Older docs suggested868F0, but later measurements
showed empty BSP work in an earlier scene, so that old recommendation cannot
be applied blindly. Inspect which children grow under this new active-object
state and preserve collision/simulation behavior. Snapshot cache remains
provisional; no ordinary FPS acceptance from instrumented run. Hardware282
still in diagnostic mode2, lease active, save backup unchanged.


## Current collision diagnostic preparation

Re-read the prior biped investigation before selecting a replacement. Its largest
object-query optimization is already enabled (`XV_NATIVE_OBJECT_QUERY=2`);
repeating that old result would not establish a new gain. The previous response
was a status restatement, not additional performance progress.

Added opt-in `--native-object-collect` to the scene timer installer. It wraps the
actual 1716F0 callback in the native collector, after early-exit rejection and
before register reload. This avoids charging that callback entirely to collection
self time when generated collector code is bypassed. Unknown or ambiguous native
layouts fail before any shard edits; repeated application is idempotent. Ordinary
source/runtime and ordinary builds have no additional timer overhead.

Four patcher tests pass, covering existing modes, ordering, idempotence, conditional
calls, and native-layout rejection without partial shard changes. Applied to
private copies of current perf282 shards in `enemy-collision-scopes/recomp`:
114 generated call sites across 4B9D0, 49600, 172BF0, 171F10, 1716F0, 172F40,
170C10, plus the native callback. This is diagnostic preparation, **not a built or
deployed candidate**. Fused query/solver paths must still be audited for bypassed
observers before using this to infer self time. Keep diagnostic timing separate
from ordinary FPS acceptance, particularly the existing root-pair interaction.

Hardware status verified perf282/533a6c71, awake lease ~3579 seconds. No controls,
save writes, deployments or restarts this turn. Goal remains unmet.


### Fused-path audit completed for whole-call attribution

The active 172BF0 route invokes `nq_collection_172c95` and
`ns_solver_at_172cb8`, bypassing the timed generic collector/solver. The
specialized collector invokes `nq_query_at_171f94`; object-space queries invoke
`nq_query_at_17301b`. Added opt-in `--collision-hooks` to time these under their
original guest addresses, including native dispatch/reuse. Also times the
specialized collector's ordinary direct children. Query/solver internals remain
inclusive; this does not claim a leaf-level split inside fused code.

Five installer tests pass. A fixture exposed the old function boundary search
extending through a following static function; the installer now stops at the
next function definition. The fixture checks that a later unrelated function is
untouched and that rerunning the combined generic/specialized installer is
idempotent. Current private staged shards contain 114 generic call sites and
seven specialized call sites (14 begin/end lines), plus native collector callback.
Preparing perf283 as a diagnostic-only derivative of perf282. No claimed gain.


Perf283 compile started from a reflink copy of the full perf282 stage; only the
two instrumented shards, instrumented native collector, and version are changed.
Build command revision is 341d6873. Live tool session 50900, build driver PID
1242267; compiler PID1243126 still at ~99% CPU at three minutes. No build result
yet. Continue polling this handle; do not restart. Private package audit script
expects precisely the two shards, native collector, and four version consumers
to differ from perf282; not executed before build completion. Guarded launch
script prepared for perf283/protected a30 save, not run. Hardware remains282;
lease successfully renewed. Previous goal turn made diagnostic implementation
progress; current turn adds fused coverage, tests and the active SDK build.


### Perf283 built and deployed

Initial build50900 terminated rc2: automatic query fusion regeneration correctly
rejected diagnostic-edited callsites. No generator validation was weakened.
Normalized both edited shards against282 (remove observers/normalize split tail
return and whitespace): guest statements identical. Twelve fusion sources,
headers and configs matched byte-for-byte. Private post-generation-audit.json
records evidence. Diagnostic-only make invocation uses `-o
build/recomp/query-fusion.generated.json` to preserve those existing generated
outputs; retry27576 completed rc0. Package audit92067 passed: exactly code008,
code028, native collector and four version consumers differ from282. Contract
unchanged, only game-a.self and boot-game.txt replaced.

RuntimeSHA d8b7de85d7c8803ba8b25b37b3baca8cfde7d74f2bdb91fb92dd47fc2445c42c.
PackageSHA09fe97e4053a8cb939a67e5eecd85a6279d00570a8776dc84926a6adfb5ed00f.
Deployment50579 terminal success: slot1, hash verified, boot confirmed.
Independent status identifies283/341d6873; awake lease renewed. Slot0 retains282.
Predeployment log13302 completed, 14,063,583B preserved. Last complete report
37260: FA92063.04ms/frame;10976050.13ms/1.7167ticks;4B9D08.62ms/17.1667calls.

Guarded launch23012 completed; screenshot shows loading, not failed gameplay.
Idle collector90315 is active, waiting for loaded/active/director telemetry
before its45second measurement. No held controls. Poll this same handle; do not
restart because loading takes several minutes. Diagnostic phase2 remains on,
protected save XV_TEST_SAVE=a30-perf211; no ordinary FPS acceptance claim.


## Important correction: native settings were present, two hooks were absent

Perf283 idle collector90315 completed; screenshots confirm pod gameplay. In a
complete60frame window, 4B9D011.04ms includes4960010.74,172BF010.61;
collection171F108.72; object callback1716F08.46; object-space172F408.33,
including881105.30 and868F02.86; solver170C101.86. These are nested diagnostic
wall times, not additive independent costs. Diagnostic idle660frames mean61.317ms,
p9576.278ms; not an ordinary-build performance comparison.

Source audit found a concrete retention bug: perf282 (and its diagnostic283)
query_fusion.c has no xv_native_4b9d0_object_query hook, and solver_fusion.c has
no native feature-test hooks. Native implementations/settings existing did NOT
prove these call paths active. Previous notes claiming both active were too
strong. Native world-query hook in xk_query_reuse.c remains installed. Current
logs contain no native-object-query initialization tag, consistent with source.

Generator now inserts the exact existing compile-guarded object-query and solver
feature hooks before output hashing/publication. Make dependencies include both
hook tools. No new arithmetic or collision semantics. Solver patching extracted
as pure function with complete/partial/duplicate checks. Two solver-hook tests
pass; existing object-hook test compiles/runs all four native/world-run routes.
Full owned-image generation41611 succeeded in collision-hook-retention/build-x87:
only query_fusion.c and solver_fusion.c changed. A forced repeat and exact-hook
comparison verify retention, recorded in retention-verification.json.

Other phase tests passed when invoked as their standalone scripts (unittest
discovery is not their CLI). Pi SSH reachable, load0.01; no jobs started there.
Perf283 remains on hardware, idle collection terminal, no controls held. Prepare
ordinary perf284 from282 plus restored hooks, verify actual native invocation
and differential correctness on hardware before claiming recovered FPS.


Forced repeat generation36123 passed: every output hash stable, changed=[], both
hook receipt fields true. Exact comparison against282 equals applying the two
previously qualified hook transformations; code028 bytes unchanged. Perf284
build3568 is active under collision-hook-retention/build-x87, revisionb3b52b63,
ordinary282 timers (not283 expanded diagnostics). Expected changed objects are
query_fusion,solver_fusion,and four version consumers. Code028 is recompiling
because generation stamp changed; package audit requires its output bytes still
match282. No package yet; don't deploy until build/audit pass.

Prepared launch-verify.py for284: explicit object-query1 and native4B9D01,
phases0; removes display-callback-timing override to remain within32 overrides.
This is native-vs-original correctness verification, not an FPS comparison.
Not launched. Hardware remains283, lease valid, collector90315 terminal.


### Perf284 deployed; correctness collection active

Build3568 and package audit16157 completed rc0. Actual changed objects exactly
query_fusion.o,solver_fusion.o and four version consumers; code028 output remains
identical to282. RuntimeSHA a737fb1ed762a5a66f407dc9cffd51292938b82266eb27174a9e8339fa4de8c1;
packageSHA87fe557df36bdeb6b7f15d90bc75642968425f02990a1b07695b050df2742077.
Contract unchanged. nm confirms native object-query and all four native feature
observer/dispatch references in the actual compiled modules.

Deployment85401 terminal rc0: verified hash, slot0, boot confirmed. Independent
status284/b3b52b63 and renewed lease. Guarded launch16186 terminal rc0; correct
32 launch overrides include explicit object-query1/native4B9D01/phases0, protected
savea30-perf211. No controls held. Previous283 retained in slot1; packaged282 is
still available as ordinary rollback if needed.

Correctness collector84362 active, incremental log capture in
collision-hook-retention/verify.log. Poll same handle. It requires loaded/active/
director1, object-mode1 startup evidence, >=2000 verified query calls and >=500
verified feature calls, with zero reported mismatches/journal failures. Reports
separate query/feature counters and saves verify-summary.json upon completion.
This is a scoped correctness check, not the full campaign/goal acceptance or an
FPS measurement. No controls or restart on observation timeout. After success,
restart284 in ordinary native mode2/phases0 and perform gameplay measurements.


### Perf284 scoped hardware verification passed; normal run started

Collector84362 terminal success at frame4915: query6275/6275 verified,
features701/701 verified, zero mismatches/declines/journal failures. Query totals
combine world/object calls; explicit object mode1 startup proves that restored
entry was reached. Screenshot shows pod with transition blur; loaded/active/
director true. This qualifies exercised states only, not all campaign physics.

Restart48673 via Companion terminal success; initial endpoint refused briefly
while dashboard started, then retry independently confirmed284 at timing_frame0.
Lease renewed without another restart. Normal launch78521 completed using
object-query2/native4B9D02/phases0; snapshotGPU1/all earlier launch settings retained,
protected save unchanged. Idle collector53687 active waiting for gameplay then
45seconds. Poll existing handle; after completion run prepared collect-gameplay.py
and review images. No performance result yet.

New tools/test_collision_hook_retention.py reproduces missing generated hooks
and verifies exact restoration plus repeat-generation stability using owned
inputs outside source. Run60024 passed, evidence collision-hook-retention/
hook-regression. This test isolates hook retention with query-reuse disabled;
actual284 enabled-reuse build/repeated generation already passed separately.
Initial source-tree enabled-reuse test rejected pre-existing canonical-header
inventory differences (query_world_run.h/xv_x86rt.h versus capture pins); no pins
were weakened. Stage284 uses the retained qualified headers. Default system
Python also lacks iced-x86; use the existing private venv for generation tests.


## Perf284 normal gameplay: user reports a successful run

Idle collector53687 and gameplay30640 terminal success. Reviewed idle-before/
after: both outdoors with600reserve/3grenades, NOT pod. Thus do not compare this
idle result as a pod improvement against282. Protected namespace unchanged;
location changed between verification and ordinary captures, cause not proven.
Reviewed outside-after: outdoor trees/cliffs/waterfall and Covenant craft, no
clear enemy count. User may also be playing; input brackets do not establish a
deterministic replay. No controls held after collector completion.

Normal mode2 confirmed in logs; native feature calls now reported; root-pair
accepted counters present. No phases2 instrumentation. Current measurements:

| Segment | Samples | Mean ms / FPS | p95 ms | max ms | >50/>100/>200ms |
|---|---:|---:|---:|---:|---:|
|Initial outdoors|840|51.281 /19.500|64.337|115.766|312/8/0|
|AR firing|82|64.383 /15.532|78.020|114.731|81/1/0|
|Movement|102|52.842 /18.924|63.497|136.730|50/1/0|
|Later outdoors|1003|45.045 /22.200|58.178|344.762|189/2/1|

User: “This current run I would say is a SUCCESS!!!” Retain perf284 as promising
candidate. These results establish playable outdoor intervals and subjective
improvement, not sustained20FPS across required combat/cutscene/15min acceptance,
and not a causal FPS delta versus a different scene. Next target AR/effects and
increased active-unit work; preserve restored hooks. Next contact-feature
candidate868F0 remains guest; its verified diagnostic cost2.86ms/frame in pod,
calling86440/862A0/86170 (private callgraph/reference recorded). Do not infer it
still dominates after284 without current measurements.

Pi has helped earlier ARM correctness/profiling; latest retention bug was found
by Vita diagnostics and source audit. No claim Pi predicts hardware FPS or did
this latest hardware test. Current task-specific scripts terminal; hardware284
running with awake lease. No goal completion: remaining heavy scenes, stalls,
AI/weapon/audio/checkpoint and stability gates still require work.


## NPC slowdown retained on284; Pi contact-feature profile running

User corrected success report: “Still low frames with NPCs but it's a ton better.”
Previous reply was status-only; this continuation pulled fresh evidence. Log64880
completed (3,658,320B) in collision-hook-retention/npc-report.log. Post-controlled
sequence windows9360–10920:1620samples58.552ms/17.079FPS,p95104.194,max232.248,
98>100ms/four>200ms. Worst60frame window10080:106.575ms/9.383FPS;10020:
97.242ms/10.284FPS. Last180frames alone19.029FPS would hide those episodes.

Nearby10080 report: FA920 elapsed94.386ms/frame (inclusive/preemption/waits),
helperCPU50.78ms,helperwall55.07,ownerjoin0.30,done->noticed49.66ms. CPU2 around
84–94%. These adjacent async reports support owner/update critical-path pressure,
not proof a specific leaf or exact frame-by-frame alignment. Helperwork itself
also approaches50ms, so fixing only one side may expose the other bottleneck.
Native object mode2 confirmed; feature calls1484/60frames nearby; counters with
zero surfaces in ordinary mode are disabled detail counters, not empty geometry.
No Vita restart/controls this turn, lease renewed before log capture.

Prepared contact-feature-profile-pi from existing native-object-query ARM harness
objects, replacing only code013 with18 additional scopes in868F0/862A0/86170/
86440. Build39068 completed rc0. It uses the older supporting harness/runtime,
not284; do not compare Pi timings to Vita FPS or present it as current full build.
Transferred17845 complete. Remote session89661 currently running timeout180 on
Pi192.168.0.9, taskset0,1, PID7606 confirmed live ~104%CPU. Other cores available
for Halo2. Root /home/birchwoodgod/xita-70110; executable
harness-codex-contact-feature-20260927; isolated save/log under
runs/codex-contact-feature-20260927, no existing saves overwritten. Scripted
150:a,300:a,450:a starts newa30; profile phases1/nativeobject2/native4B9D02,
soft raster off, legacy shader/effects reductions. Result is supporting callee
attribution only, not representative NPC gameplay until its logs establish that.
After terminal timeout124 (expected observation bound), pull game.log and
exit-code, inspect actual loaded scene and timed children. Do not duplicate job.

## Pi contact-feature profile completed; current user feedback retained

The bounded Pi run ended with expected timeout status124. Retrieved full log to
private contact-feature-profile-pi/game.log. Loaded/active/director all1; camera
31.49,-102.45,59.38 stayed at the pod. This is not NPC-combat coverage.
Settled windows show86170 about0.73–0.76ms inclusive,0.44–0.46ms self,
5040calls/60frames;862A0 about0.46–0.48ms inclusive,5880calls/60frames;
86440 about0.07ms. These are older ARM harness timings, not Vita predictions.
The reporter aggregates callees and attributes untimed868F0 children under172F40;
do not mistake this for a direct source call graph. Next concrete leaf candidate
is86170, with862A0 second; inspect semantics and build differential coverage
before replacing either. Current284 NPC attribution is still needed before
claiming these leaves dominate combat. No new build deployed or gameplay
interrupted. Vita awake lease renewed3600seconds. User reports NPC frames remain
low but substantially improved; sustained20FPS goal remains unmet.

## Contact-branch register candidate prepared on Pi

Previous turn was progress: completed/retrieved the bounded profile. Inspection
confirms86170 emits polygon contacts (ring/plane/point transforms ->85020),
862A0 emits capsule contacts after plane/edge convexity checks. This agrees with
docs/claude-collision-collection-20260916.md and the earlier collection-packet
profile; do not claim discovery of a new call graph.

Both current284 bodies exactly equal the existing regen-base-final bodies after
observer normalization. Existing regen-regs-d3-final has register lowering for
both (86170:39x87/8syncs;862A0:38x87/9syncs; neither has guarded calls). Current284
uses memory lowering. Historical perf177-splice receipts say both were spliced;
when/why this was lost remains unaudited. Do not globally regenerate other code.

Private contact-feature-registers-pi candidate replaces only those two bodies
in the instrumented supporting harness. Six/five timer pairs retained via the
existing splice tool, hashes in splice-receipt.json; all generated code private.
ARM build27725 completed rc0 and transfer26745 completed. A bounded180second
run is now session18994, Pi cores0/1, separate runs/codex-contact-registers-20260927
save directory. Same legacy harness/settings as the prior contact-feature run.
The run command is retained in run-remote.sh. Poll the existing handle; do not
duplicate. This measures a prototype, not correctness qualification or Vita FPS.
Next: retrieve terminal output, verify loaded state, compare scoped costs, then
qualify complete context/memory and ARM call-boundary behavior before hardware.
No Vita build change/restart; awake lease renewed.20FPS goal unmet.

## Register candidate supporting result and polygon qualification

Pi run18994 finished with planned timeout124; retrieved game.log. Last20
polygon reports: reference inclusive0.737/self0.446ms; candidate
inclusive0.715/self0.4255ms. Same stationary pod coordinates and older
headless harness. Sequential noisy instrumentation, no confidence interval:
small supporting difference, not proof of Vita benefit; retain as candidate
for stacking, not a claimed solution to NPC stalls.

Added tools/test_contact_polygon_registers.py and synthetic fixture. Extracts
owned reference/candidate86170 privately; full context/arena/callee trace and
preemption compare strictly.256 cases cover all8 starting x87 tops, null/non-null
transforms,0–8 ring vertices, preempting loops, sentinel/ordinary argument,
surface record across a page boundary. Stub callees overwrite physical x87
slots/status but preserve known stack effects. Host ASan/UBSan76390 passed;
ARM compiled and ran on Pi46314, passed256. Deliberate multiply->add mutation
failed scenario1 with memory/trace differences.

Scope is lowering with synthetic callees, not real collision packet semantics.
No arbitrary aliases, special floating values or real-game differential capture
yet;862A0 capsule branch also still unqualified. Candidate remains private and
undeployed;284 unchanged. Next qualify capsule and broader boundaries, then
consider hardware trial alongside existing improvements. No task processes
remain active from this turn; keep-awake lease renewed. Goal remains unmet.

## Capsule qualification exposed x87 scheduler-boundary bug

Previous turn progress: polygon differential fixture and completed ARM profile.
New862A0 capsule fixture failed the old register candidate at scenario3: final
context/arena matched, but xv_preempt observed stale physical x87 slots and status
at backward jump86431. This is why final-output-only tests were insufficient.
Do not deploy the original contact-feature-registers-pi/harness candidate.

Fixed canonical Emitter.x87_preempt_inline: register lowering decrements budget
once, spills dirty slots/top/status only when yielding, calls scheduler, then
reloads locals. Memory lowering keeps X_PREEMPT. JMP/Jcc/LOOP emission all use
the helper. Static dirty-state analysis remains conservative. Scheduler must
preserve the guest logical stack depth, as required by the existing model.

Regenerated privately with current codegen, original ownedXBE/symbols, profile
halo_ce_3925, phase timing, x87-regs-only86170,862A0. Session3720 completed.
Compared with earlier candidate: only the three preemption emission sites changed.
No other instruction/arithmetic changes in these two bodies.

Corrected capsule passes512 strict cases under host ASan/UBSan34331 and Pi
ARM84865; includes early return, masked equal plane, opposite-orientation dot
test, cross-product branch, null/non-null transform, all8 x87 tops, and optional
physical-slot/status mutation at scheduler yield. All4 callee stubs exercised.
Polygon256cases still passes host ASan/UBSan1028. Actual-emitter tests cover
JMP/Jcc/LOOP/LOOPE/LOOPNE, forward branches and unchanged memory lowering;2pass.
Synthetic callee limitation remains. The originally failing candidate provides
a regression negative control; private capsule-host retains failure artifacts.

Source fix and fixtures ready; no hardware deployment or FPS claim. Next ARM
polygon recheck for corrected yield handling, broaden numeric/alias coverage,
and build only qualified bodies into a private284-derived candidate (retain
native query/solver hooks). Vita status verified284/b3b52b63, awake renewed.
All jobs terminal. Sustained NPC20FPS/other acceptance gates still unmet.

## Expanded numeric/alias checks; capsule NaN discrepancy remains

Previous goal turn was progress: fixed preemption synchronization and512 ARM
capsule cases. This turn expanded fixtures to1024cases. Polygon adds matrix
aliases to its local plane/vertex storage and a page-crossing matrix; numeric
inputs include signed zeros, subnormals, maxfinite, infinities and quietNaN.
Capsule adds plane/vertex special values and synthetic transform-pointer aliases
(the transform callees remain stubs, so not real transform alias qualification).

Polygon host ASan/UBSan45711 passes1024. Capsule host56173 fails703: final
context differs but fullarena/calltrace match. Focused O1+sanitizer64596 confirms
physicalst5 referencefff8000000000000 vs candidate7ff82468a0000000: distinct
NaN payload/sign in a popped slot. O2 plain85830 passes704cases. Strict gate
not relaxed. Tool now accepts case-start to reproduce this singlecase.
Do not claim capsule universally bit-exact; assess target compiler behavior
and any required exceptional-value fallback before including it.

Both corrected expanded fixtures compiled ARM O2. Remote test16252 running
sequential polygon1024 then capsule1024, cores0/1; PID9290 polygon confirmed
alive00:45 at99.9%CPU. Log contact-feature-registers-pi/boundary-arm.log.
Poll samehandle; no duplicate job. Target production shard usesO2,thumb,
cortex-a9,neon; these supporting tests stillARM-state GCC13 (not full VitaSDK).

Created private contact-registers-hardware/build-x87 as reflink copy of284
(1.1GB apparent); not modified/spliced/built/deployed yet. Candidate scope will
depend on qualification (polygon-only is possible while capsule held). Keep
all previously restored nativehooks and buffers. Vita284 unchanged;awake renewed.
No performance win claimed. Goal remains unmet.

## Perf285 polygon-only hardware candidate building

Previous turn progress: expanded numeric cases and live ARM test.16252 is now
terminalrc1: polygon1024passes; capsule535fails only final NaN sign in popped
st2/st3 (reference7ff8000000000000/candidatefff8000000000000), memory/trace same.
Capsule held; strict gate unchanged. Polygon qualified for an incremental trial.

Used existing splice_x87_regs --only86170 with regen-yield and baseline-final
on private contact-registers-hardware/build-x87. Report exactly1spliced shard.
source-audit.json proves only86170 body changed in code013; all other functions,
query_fusion andsolver_fusion source unchanged.285/bef4017f version metadata.
Current build9404 live; PID1286939 cc1 compilingcode013 at99.3%CPU after1:48.
Do not restart. make-plan lists full dependency recipes, but actual build log
sofar only a subset recompiles. Actual object audit after completion is required.
Private audit-package.py prepared: compare284 objects, allow onlycode013 and
version consumers, substitute onlygame-a.self/boot-game into284package, enforce
unchanged update contract. Not run yet, no285VPK/deployment claimed.

Before-update Vita log fetch66907 also live (PID1287579 observed00:24); path
contact-registers-hardware/before285.log. No controls/restart. Next poll both
existing handles, audit package/source preservation after generation, then
deploy qualified285 with announcement and protected test save. Retain284rollback.
No FPS claim; small polygon lowering stacks prior native hooks and snapshotGPU.
NPC-critical-path work and full20FPS acceptance still outstanding.

## Perf285 package qualified; deployment in progress

Build9404 completedrc0. audit-package.py passed: changed objects exactly
code013 plus dashboard/remote/main/ui version consumers. Query/solver objects
byte-identical284, so existing nativehooks retained. Package changes only
game-a.self/boot-game.txt; contract775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
RuntimeSHA0be12bd2c85d7d8c2450f887c5b89f79fc4e610cccf2673a7b0bd8426c80e2c0;
packageSHAaef5c8e36740edb4fd77addc7548cab52442eb55c8ab4d3a55f37f4b3c4c1065.
VPK contact-registers-hardware/xita-perf285c.vpk.

Announced interruption and started update--apply session3455. Still live: last
upload progress34668544/34832554; finish verification may take120seconds.
Do not restart/re-upload based solely on silence. Pollsamehandle and inspect
deploy.log. No installed285claim untilboot/hash confirmed. launch-fast.py,
collect-idle.py,collect-gameplay.py prepared in candidate directory with285
assertions; launch retains protectedXV_TEST_SAVE=a30-perf211 andnormalphases0.

Beforelog66907 completed14864289bytes. Last600samples284 mean78.360735ms/
12.761493FPS,p9590.413,max103.162,all600>50ms,five>100ms; uncontrolledscene,
notmatchedbenchmark. C2near96–97%, ownerFA920around75ms inclusive; noFPSgain
claim. Further source audit:90950 dispatches indirectper-typeupdates (including
4C980);8DDF0 is1538-line poseupdate with knownposeexperiment scope. Supporting
Pi costs remain nested, do not call90950a cheaplookup or assume GPU bound.
Next confirm285boot,renewlease,launchandcollectordinarygameplay. Goal unmet.

Deployment3455 completedrc0: hashverified, slot1bootconfirmed. Independent
status285/bef4017f timingframe0/dashboard. Awakelease renewed3600.
Protected a30 launch42023 now active; logcontact-registers-hardware/launch-fast.log.
Poll existinghandle; scriptedbuttonsequence has finallypadrelease. No285
gameplay/FPS evidence yet.284rollback remains in slot0 and packaged.

## Perf285 normal a30 pod sample; gameplay sequence running

Launch42023 completedrc0 with finallypadrelease. Screenshot atsequenceend
stillloading; noblindextra buttons/restart. Collector24008 waited for loaded1/
active1/director1 (actualmapI/Ocontinued), then completedrc0. Both idlebefore/
after images inspected: same lifepod view, AR60/120reserve, no visiblecombat.
840samples48.799888ms/20.49185FPS,p9559.076,p9966.391,max76.102;265>50ms,
none>100ms. This is notsustained20acceptance and not a controlledcausal gain
versus284's outdoor sample. Noimmediate visible regression in this view.

Started collect-gameplay.py session74189:fire7450–7536,move7794–7878 recorded;
45secondpostmovementinterval thenlogcapture stillactive. Boundaryscreens need
inspection; noresultclaimed yet. Poll existinghandle; don't overlapcontrols.

Audit before285fullsessionlog: no whole-objectworker initialization markers,
while root-paircounts active. xd3d_object_jobs_ready strictviewportlag1 and
90stableframes reset underlag0 catch-up ticks. This is a documented intentional
gate (olderperf144 tolerantwholecallbacktrial121ms vs67ms), not newlydiscovered
missingmulticore support. Core0/1 stillrunrender/capturework. Do not blindly
removegate: wholecallbacksreorder sharedgame state and are explicitlyunproven.
CurrentNPC ownerupdatepressure remains; further targetednative/purejob work
needed alongsidepolygon. Goalunmet.


## Perf285 gameplay and stationary slowdown audit

Previous user-feedback turn was no progress (status only). Revalidated current
checkout at 077e601a, clean; renewed physical Vita lease successfully. No update
or settings change in this continuation. Perf285 remains the tested candidate.

Completed gameplay74189 and encounter87158 artifacts are preserved in private
`contact-registers-hardware/`. All scripted controls released. Reviewed boundary
images: fire-after is inside the pod/reloading; outside-after is by cliffs,
waterfall and trees. Encounter before/after face a nearby rock, with no visually
confirmed enemies. Do not describe this as verified multi-enemy combat.

| Ordinary segment | Samples | Mean ms / FPS | p95 ms | Maximum ms | >100 ms |
|---|---:|---:|---:|---:|---:|
| AR firing | 86 | 63.685 / 15.70 | 77.612 | 123.852 | 1 |
| Moving out of pod | 84 | 64.513 / 15.50 | 80.031 | 265.674 | 1 |
| Outside, stationary | 911 | 49.516 / 20.20 | 59.620 | 108.303 | 1 |
| After traversal, stationary | 840 | 50.074 / 19.97 | 90.273 | 158.077 | 28 |

These are CPU Present intervals from normal gameplay, not physical scanout or
matched causal comparisons with perf284. No crash in these short sequences;
15-minute acceptance, NPC combat, checkpoints and cutscene gates remain unmet.

New `encounter-window-audit.json` uses complete timing windows wholly inside
status brackets 13057..13963. First seven windows ending 13140..13500 average
37.31–41.30 ms; subsequent windows ending 13560..13920 average 54.04–68.12 ms.
Thus the overall near-20-FPS average hides a sustained deterioration while the
camera is stationary, not merely a few isolated transition stalls.

Existing slow-frame instrumentation partitions 29 records inside those status
brackets: 3417.199 ms before Present versus 6.186 ms in all other segments
combined (99.82% before Present). The count is 29 rather than the table's 28
because frame13925 is inside the status bracket but outside the last selected
complete 60-frame report. No samples removed to improve the aggregate. This
strongly deprioritizes Present flip/drain/publication work for these stalls;
pre-Present still includes scheduling and is not exclusive simulation CPU time.

Adjacent asynchronous reports show helper CPU30.64–30.87 ms, scene wall32.72–
33.00 ms, done-to-noticed17.21–20.95 ms and FA920 inclusive46.20–49.21 ms.
Do not align these exactly to individual slow frames or add overlapping times.
They support further owner-side update investigation while helper finishes early.

Source audit avoids two misleading targets: 90770 is a type postprocess
DISPATCHER, whose dominant aim-blend child already has a native replacement;
it is not a fresh 5.4 ms arithmetic opportunity. Hierarchy bounds declines
include the deliberately retained final guest iteration and short tails, so
large decline counts alone do not prove missed useful batches. Final iteration
preserves guest registers, FP state and stack footprint. Do not relax its gate.
Next target remains remaining owner object/animation preparation with existing
native hooks retained; qualify any replacement against full guest state and
memory before deploying. Goal remains unmet.


## Hierarchy fallback attribution prepared

After the user-facing bug-list turn (no performance progress), resumed the NPC
objective. Vita awake lease renewed successfully. Pi SSH responded at192.168.0.9,
uptime2h51/load0; no harness/Halo2/compiler process matched the read-only check.
No new Pi run or Vita update was performed in this turn.

Latest encounter tail: aim-blend1275/1275 and1380/1380 calls native, no declines;
hierarchy2059/2177 batches,19356/20398 child nodes,3895/4130 bounds declines,
44/126 numeric-pose declines. Broad bounds counts cannot distinguish required
final-original iterations from recoverable work. Added five guarded subcounters
in xk_hierarchy.c: entry, model-address, count/queue, short/final tail, empty
worklist. Existing admission conditions, arithmetic and publication unchanged.
Report describes retry-inclusive attempts, not missed nodes or milliseconds.
This diagnostic is prepared only, not deployed or evidence of FPS improvement.

Existing test_model_hierarchy.py initially failed linking because its fixture
omitted the newer xv_phase_enabled global used by xk_math root-pair. Added
explicit disabled phase state to host and ARM fixtures. Then host68326 passed:
222 full comparisons in each enabled/unset/disabled/math-disabled mode;
117 admitted random probes in enabled mode;85 unchanged declines per mode.
Owned-XBE signatures and disabled-generation equivalence also passed.
Private output hierarchy-bounds-audit uses285 stage XBE/manifest. First attempted
old production-build-final manifest was empty; correct stage manifest used.
ARM instruction test did not start: default interpreter lacks pyelftools,
separate crash-parser venv lacks Unicorn. No ARM result claimed for this change.
Next integrate these counters with the next qualified diagnostic candidate or
ARM harness to distinguish retained tails before changing hierarchy admission.
Vita remains285; full NPC and cutscene20FPS target still unmet.


## Hierarchy ARM qualification and live Pi diagnostic

Previous goal turn made progress: committed fallback attribution and repaired
fixture dependency. Current ARM test44243 completedrc0:2308 comparisons,
four rounding modes, root/consumed-prefix/shuffled-index/remapped-page cases.
Private hierarchy-bounds-audit/arm/result.json and arm.log retained. Uses VitaSDK
Thumb Cortex-A9 instruction emulation, not Vita3K or hardware FPS. Isolated venv
now has Unicorn2.1.4 and pyelftools0.32; no global Python environment changed.

Built hierarchy-bounds-audit/harness using existing ARM whole-game link recipe,
replacing only xk_hierarchy with the new counters and retaining baseline
point-location-build code013 (not unqualified contact-registers capsule code).
Hierarchy source comparison against that stage is precisely the counter patch.
This supporting harness predates perf285 and cannot establish its frame times.

Pi verified idle before launch. New private job codex-hierarchy-bounds-20260927
runs180seconds pinned cores0/1, explicit XV_NATIVE_MODEL_HIERARCHY=1, separate
save directory. Session12828 still active; remote PID11096 confirmed Rl at50s,
102%CPU. First reports include count/queue declines5400 and zero tail/empty,
but other windows zero; no representative gameplay attribution yet. Preserve
and poll this SAME session; timeout observation is not job failure. Planned
180-second timeout normally returns124; inspect final log/exit-code before
interpreting counts. Copy log to hierarchy-bounds-audit after completion.
Vita285 unchanged, awake lease renewed. Goal unmet; no new hardware FPS claim.


## Pi fallback census narrowed; launch-state correction

Previous turn progress/verified live jobs. Pi12828 completed plannedtimeout124.
Copied complete log to hierarchy-bounds-audit/pi.log; pi-summary.json has204
report calls (includes repeated zero reset reports),320591 count/queue declines,
171 tail,0 entry/model/empty. Later a30 map reads and director1 establish loaded
gameplay. Final windows2160 batches/15000 child nodes/5400 count declines per60
frames;8DDF0~3.6ms inclusive/~1.95ms remainder on this supporting Pi harness.
Do not extrapolate to Vita FPS or confirm visible NPC combat from these counters.

Expanded count category into small(<3),large(>64),queue(first>=queued or queued>
count), preserving guard order and behavior. Host10867 completedrc0: same222
full comparisons in each of4modes,117admissions enabled,85unchanged declines.
ARM2308 qualification from previous turn applies before this counter-only split;
no new ARM result claimed for split. Private Pi harness rebuilt successfully.

Detailed Pi80333 used a COPY of previous save/cache to avoid I/O. This changed
menu navigation: at2m21s zeroa30.map reads, director0, only UI rendering. Explicitly
terminated confirmedPID11475; session80333terminal143. Its zero counters are
NOT gameplay evidence. No timeout-triggered blind restart. Original saves intact.
Fresh isolated job86433 now active, same known-working initial script and new
empty save directory: codex-hierarchy-fresh-20260927,180sec cores0/1. Poll SAME
handle, inspect map/director before interpreting, then retrieve log. Do not reuse
copied profile with the fresh-profile button sequence. Vita285 unchanged/awake.
Goal unmet; no hardware speedup claimed.


## Completed Pi census: dominant declines are tiny models

Previous turn made progress and verified a live corrected run. This continuation
polled86433 until terminal124 (planned180sec timeout), copied pi-fresh.log and
wrote pi-fresh-summary.json. a30.map reads and director1 confirmed.198 report
calls include startup/repeated zero resets. Totals:309836 small(count<3),172
short/final tails; zero entry/model-address/large/queue/empty. Final active
reports have5400 small declines per60frames. These are attempts, not expensive
missed batches. The batch leaves a final guest node for observable register,
FP and stack state, so count<3 has no ordinary non-root batch to recover.
This rules against relaxing MAX_NODES/queue validation or enlarging worker
queues on the basis of the previously broad bounds count. Existing large-model
batching is doing useful work. No claim about every hardware scene or enemy type.

Pi phase traces disable root-pair and run an older supporting runtime; their
~3.6ms8DDF0/~1.95ms remainder cannot be scaled into Vita savings. The remaining
actor collision path still deserves targeted hardware attribution AFTER restored
nativehooks: old283 pre-retention numbers are not a current hot-function ranking.
Current285 code028 has only2 scene-phase sites, so do not assume it contains all
114 expanded diagnostic sites from283. A next diagnostic candidate should retain
285 code, add the guarded detail sites and counters, identify its version, then
collect a single owner-phase scene sample (no repeated benchmark toggles). Keep
normal phase0/root-pair path as rollback; diagnostic elapsed is attribution only.
Vita285 unchanged, lease renewed. All Pi jobs from this census terminal; no
background task left to poll. Goal unmet; no new FPS improvement claimed.


## Perf286 actor-detail candidate compiling from perf285

Previous turn progress: completed Pi fallback attribution. Prepared private
actor-detail-286 as reflink copy of full285 stage. Added114 generic phase sites
plus specialized collision-hook scopes and native collector callback using the
existing tested patcher; copied canonical hierarchy diagnostic counters. Version
286/revision28034fec. Five patcher tests pass. source-audit.json verifies two
shards' guest statements unchanged after observer/whitespace normalization and
exact285 code013/query_fusion/solver_fusion/solver_primitives/query_world_run bytes.
This retains polygon register lowering and the restored native query/solver hooks.

Build4063 active, driverPID1312400 and code028 compiler observed at~1minute;
no completed result yet. Poll samehandle, do not restart. Build command marks
query-fusion.generated.json old to retain instrumented generated sources, as the
previous diagnostic did. Rechecked pinned source hashes while building:unchanged.
After completion run audit-package.py: expects onlycode008/code028, nativecollector,
hierarchy and four version consumers changed; query/solverobjects must remain
identical285. Package script substitutes onlygame-a.self/boot-game.txt into285,
requires identical update contract, records hashes. Not run yet/no286VPK claimed.

Prepared launch-diagnostic.py for286 with protecteda30-perf211 and phases2,
retaining nativeobjectquery2/native4B9D02/snapshotGPU1. Existing root-pair is
suppressed in diagnostic phases, so attribution only, no normal-FPS claim.
collect-idle.py adapted286. No deployment/restart yet; Vita285 stays installed,
lease renewed. Announce interruption before deploy; inspect completed package and
post-buildsource audits first. Goal remains unmet.


## Perf286 package audited; remote deployment underway

Build4063 terminalrc0. Package/source audit passed: changed objects exactly
code008/code028, xk_object_collect/xk_hierarchy and four version consumers.
Query/solver objects and retained polygon code013 object unchanged from285.
Only game-a.self/boot-game.txt changed, update contract unchanged
775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
RuntimeSHA81ba4621278ad6c76865517db66dac9a51fdb52cc8b3cfd4551035c7eb7ac0fc;
packageSHA600c738953be8dbb0e71fc7ccdffa87809bd1f6c5f35894041a911bf797efff1.
Private actor-detail-286/xita-perf286c.vpk. Post-build retained native source hashes
also checked. Collector metadata corrected to diagnostic phase2, not normalFPS.

Announced interruption and started remote update--apply53376. Last upload progress
34668544/34837442; verification/boot still pending, handle active on last poll.
Do not re-upload/restart due to silence. Poll SAME handle and deploy.log; only
claim installed286 after hash/slotboot and independentstatus confirmation. Then
renewlease, run prepared launch-diagnostic.py (protected a30 save), collect-idle.py,
inspect images, and collect-encounter.py (includes5sec pod exit before oldroute).
Prepared input sequences always release pad. Existing285 package is rollback.
No286gameplay evidence yet. Goal unmet.


## Perf286 boot confirmed; user NPC session recovered

Deployment53376 terminalrc0: runtimeSHA matched, slot0 boot confirmed.
Independentstatus286/28034fec at timingframe0; dashboard log confirms waiting for
Launch Game. User reported moving to many NPCs. Paused launch to preserve prior
session first: archived log slot1 fetched9532 terminalrc0,19048722bytes to
actor-detail-286/user-npc-previous.log, identifies285/bef4017f and ends update
handoff. Current user-npc.log is dashboard-only, NOT gameplay evidence.

Last600 complete archived intervals end56340: mean99.8194ms/10.0181FPS,
p95121.253,p99138.647,max155.787;600>50ms,248>100ms,0>200ms.
User supplies NPC-heavy scene identification; no synchronized screenshot.
Adjacent latest phase window FA9205997290/60=99.955ms inclusive; helper CPU59.95,
wall73.70,done-to-noticed32.96ms. Overlapping/nested reports, not additive self
costs or exact per-frame alignment. Nearby C0~55–62%,C1~86–92%,C2~92–96%.
Final frame aggregate9.2FPS,363draws/frame,decode0; substantial owner AND render
work. Do not repeat earlier quiet-scene helper33ms as this NPCscene's cost.
Nativefeaturecalls2293/60frames with0declines; mode2 means not active differential
verification. Evidence strengthens actorpressure but not exact internalroutine.

Explained findings to user and started prepared protected launch65006, currently
active; poll samehandle. Owner phases2/nativehooks2/snapshotGPU1, savea30-perf211.
No collector yet; do not overlapinputs. After launch completes, telemetry-readiness
collector and image qualification before traversal. Awakelease renewed; no claim
286gameplay or FPS improvement yet. Goal unmet.


## Perf286 pod and traversal capture; AI behavior dispatcher identified

Launch65006 completedrc0/padreleased. Initial screenshot loading; no extra input.
Readiness4251 waited through map load/cinematic (inspected readiness-view.png,
letterboxed pod), then terminalrc0. Idle images confirm ordinary pod view.
720 diagnostic intervals58.446ms/17.110FPS,p9573.639,max84.920,532>50,0>100.
Phase2/root-pair suppression means NOT a regression or normal-FPS acceptance.
Latest pod phases8DDF011.77ms incl,4B9D08.66,particles10E7A08.44. Collision
172F406.84 includes881103.71 and868F02.96. Costs nested; nativehooks active.
Hardware hierarchy declines also overwhelmingly small models, agreeing withPi.

Traversal31471 terminalrc0, controlsreleased. Before/after show outdoor trees and
cliffs, same view, AR60/600reserve; no visually confirmed enemy.840intervals
50.564ms/19.777FPS,p9569.997,max354.803,430>50,one>100/200. Not sustained20.
Measured bracket8299..9192. Crucially AFTER this interval, adjacent phase reports
next-frame9240/9300 show AI14A16215.51/18.59ms,14E2C015.40/18.49;
14E1A0>1640909.49/12.62ms. Earlier next-frame9180 has14E2C04.03ms.
Do not attribute these later timings to the45secaggregate. OwnerFA92077.64/82.66,
8DDF015.92/15.74,4B9D09.42/8.98. Thus AI rose as well as object preparation;
collision alone is not an established dominant explanation for the whole jump.

Source164090 is34lines: resolves actor table2FA244,datum stride724h, signedword
actor+6Ch, dispatchtable1F0570 stride38h, and tail xv_call(c,c->r[0]). It is an
INDIRECT behavior dispatcher, not12.6ms arithmetic. Must time resolved destination
before selecting a native replacement; behavior semantic names not verified.
Existing generic scene patcher covers direct calls, not this taildispatch.

Started collect-pressure.py89967 for stationary settled capture (new pressure*
paths; retains idle artifacts). Live on lastcall; no controls. Poll samehandle,
review images/log and phase windows. Next bounded instrumentation should split
164090 by actual target while preserving tail-call guest state and ordering.
No newoptimization/FPSclaim; goalunmet. Awakelease renewed.


## Indirect AI target profiling implemented and running on Pi

Previous turn progress: hardware capture and dispatcher identification. Settled
pressure89967 now terminalrc0. Images show unchanged outdoor trees/cliffs; no
visibleenemy identity established.660 diagnostic intervals65.336ms/15.306FPS,
p9580.707,max123.805,522>50ms,9>100ms. Behavior cost varies: later14E2C0 windows
8.35 then4.98ms,1640903.64 then0.75; do not extrapolate the12.62ms peak to every
frame. Protected saves untouched; controls released; Vita286 remains phase2.

Added tools/patch_indirect_phase_timers.py, opt-in diagnostic-stage installer.
Selected parent must contain exactly one register-indirect tailcall. Capture
target before invoking callee and use the same address at timer end even if callee
changes guestregister. Guest call/return order preserved; host tail elimination
intentionally prevented. Validate every selected body before writes; refuse
missing/duplicate/partial/unrecognized installations; exact-block idempotence.
Two new tests plus5existing phasepatcher tests pass. Compiled fixture proves
callee target/register/stack changes, earlyreturn, eventorder and saved endtarget.

Private ai-behavior-profile-pi built19362 terminalrc0 from supporting ARMobjects,
replacing code027 only for164090 indirect timing, retaining previous hierarchy
counterobject and baselinecode013. source-audit.json proves reversing EXACT
inserted block recreates baseline shard byte-for-byte. No generatedcode committed.

Fresh Pi job68251 active, remotePID13374 confirmed. timeout180 pinned0/1,
private codex-ai-behavior-20260927 save/log. Original successful fresh-profile
buttons plus1200:lup*300,1650:rright*20,1710:lup*210; hostparser supports these.
Movement/map/activity must be verified from resulting log; intent is not evidence.
Poll SAME handle, retrieve log after terminal, resolve164090>targetedges. This
older supporting runtime is not a Vita FPS prediction. No newhardware update.
Vita lease renewed. Goalunmet.


## AI Pi result and perf287 diagnostic preparation

Previous continuation yielded terminal Pi evidence; no optimization accepted.
Pi job68251 ended by its planned180-second timeout (exit124), not a game-crash
claim. Retrieved ai-behavior-profile-pi/game.log and result-summary.json.
The run opened a30.map and logged54 director-on gameplay reports, ending at
camera35.89,-44.44,58.40. However14E2C0 reported at most0.01ms and no164090
entry appeared. Older linked phase reporter uses inclusive-by-callee format,
not parent edges. Thus absence of164090> text alone was not a valid test.
This route/runtime did NOT reproduce hardware heavy-AI activity; do not infer
AI is cheap or fixed. Next measurement is the current Vita runtime with the
resolved-target timer. No repeated Pi run launched.

Vita286 retained online; pulled preupdate-user.log (8438966bytes). Last600
diagnostic intervals through22140:63.259ms/15.808FPS,p9578.348,p9984.779,
max90.660,451over50,noneover100. No synchronized image for scene attribution.
Last phase reports8DDF014.10..14.53ms,4B9D08.15..9.16ms,14E2C04.21..5.25ms;
these are inclusive, not additive. Protected saves not changed.

Prepared private ai-behavior-287 as reflink copy of286; installed only the
validated164090 indirect-target wrapper incode027 plus version287/16abae1b.
Exact reversal recreates baselinecode027 byte-for-byte (source-audit.json).
Two indirect-patcher tests rerun/pass. Build89982 active at this note; poll
same handle, then run audit-package.py, which requires changed objects to be
onlycode027 and4version consumers and package assets onlygame-a/boot-game.
Not deployed yet. This is diagnostic instrumentation, not an FPS optimization.
Goal unmet.


Perf287 build89982 and package audit49170 now terminalrc0. Exactlycode027
and4version objects changed; onlygame-a.self/boot-game.txt differ from286.
RuntimeSHA eaf848d5626dda8ab0e88b52370d909d1bfc4006ef47d3cdb097fff8554da2b6;
VPKSHA97152a1bb39b41d01e2a22042e4c6871da1da2226d0803de40c55892cfc1eafd.
Contract unchanged775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897.
Announced interruption; remote update41010 actively uploading. Pollsamehandle,
verify runtime hash/version, then use ai-behavior-287/launch-diagnostic.py
and collect-pressure.py (287 versions) for protected a30 save. No target
behavior timing yet. Do not claim an optimization or20FPS.


## Perf287 hardware boot confirmed; campaign readiness capture active

Previous turn progress: built/audited diagnostic and started deployment.
Update41010 is now terminalrc0; deploy.log confirms exact eaf848d5... runtime
SHA installed and booted slot1. Independent status confirmsperf287/16abae1b,
dashboard timing0. Brief connection refusals were update handoff; no redundant
restart was issued. Companion1.07 reachable, nosleep on acknowledged.
Launch79055 terminalrc0, sequence completed and inputs released. Reviewed
a30-after-sequence.png: loading screen, NOT gameplay.

collect-pressure55030 is active, bounded readiness polling, not repeated
benchmark toggles. Latest readiness file3 has loaded0/active0/director0.
Wait same handle; don't send movement while loading. It will collect45seconds
after loaded/active/director telemetry, then review pressure-before/after.png
and pressure.log. Afterwards prepared collect-encounter.py can follow286route
and expose164090 resolved targets. May need another settled capture under a
new filename after traversal. No FPS-improvement claim. Saves untouched.
