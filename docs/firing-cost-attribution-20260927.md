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
