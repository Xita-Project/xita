# Gameplay performance retests

The user requested fresh Xita launches and ordinary gameplay instead of running
the built-in benchmark after each change. Recent candidates left disabled on
the strength of those comparisons need another look. Keep normal graphics
settings fixed and record the installed executable and effective startup modes.
Restarting means a new Xita process; it does not require rebooting the Vita.

## Current cumulative build

The current fifteen-path runtime `f037ddb3…`, from source `79b067c`, is
installed and boot-confirmed in slot 1. It keeps the twelve paths below and
adds packed layouts for three compatible vertex programs, one exact-state
query arithmetic reconstruction, and ordered publication of completed query
prefixes with retained-history protection. Standard graphics remain unchanged.
The twelve-path parent stays in slot 0. Short ordinary campaign firing, movement and pause checks complete without
a searched fault or logger error. Initial active reports remain 12.7–12.8 FPS;
no combined FPS gain is established. See the
[cumulative update](cumulative-render-update-20260917.md) for qualification,
package identity and hardware evidence.

The preceding eleven-path runtime `ece322ea…` completed short ordinary campaign and
Blood Gulch vehicle checks. It retains the earlier CPU/rendering work and adds
exact retired-slot upload omission and collision-solver fusion to the nine-path
build described below. No clear five-FPS gain or stable representative 20 FPS
has been established. All eleven compatible paths remain enabled.

The preceding twelve-path package, from source `e29a962`, adds query-only f32 helper inlining.
Its final query object exactly matches the qualified prototype; all other
runtime, caller, solver and generic objects are unchanged. The package retains
all assets and the existing updater contract. Runtime `2393a044…` was installed
and boot-confirmed in slot 0; it now serves as the retained rollback.
Ordinary campaign fire inputs, camera/strafe, movement and pause completed
without a searched fault marker or logger error. Initial settled reports are
12.7 FPS. No matched gain/regression or stable 20 FPS is established. The
[query-inline follow-up](query-f32-inline-20260917.md) records the evidence.
This is a cumulative change, not a replacement of the preceding optimizations.

The eleven-path active campaign reading has an independent cross-check: 372
completed display callbacks in 30.000–30.239 host seconds, or 12.302–12.400 FPS,
with benchmark mode off. The visible counter showed 12 FPS. This supports the
current reading without proving that earlier workload comparisons were valid.
No setting, input or screenshot request ran during that timing interval.

## Acceptance decisions

Correctness and performance are separate. Local comparisons must preserve the
replaced code's observable results, memory, scheduling and floating-point
behavior. Hardware gameplay must then check rendering, controls, crashes and
the workloads the local fixtures cannot represent. Passing local tests does
not establish complete gameplay correctness.

For performance, confirm the candidate is actually active, then compare FPS
and visible stutters across comparable scenes and repeated fresh launches.
Keep a reproducible benefit that exceeds ordinary run variation without a
material regression elsewhere. A small consistent benefit still counts; there
is no requirement for a whole-FPS increase from each change. Roll back a
reproduced correctness or performance regression. Label an inconsistent or
unresolved result **inconclusive** and retain the implementation for further
scene and combination tests. Instruction counts or core utilization alone do
not determine the gameplay verdict.

In the cumulative experimental gameplay build, an inconclusive timing result
does not by itself disable an already admitted, compatible change. Keep it
enabled while checking the combination, provided its correctness checks pass
and no regression is reproduced. Record performance as unproven rather than
calling it a speedup. This does not promote untested prototypes, known unsafe
changes or debug instrumentation into the gameplay build. Repository defaults
and broader release qualification remain separate decisions.

## What the benchmark audit found

Claude independently audited the controller, client and saved physical logs.
The saved FPS arithmetic reproduces from the recorded elapsed times and agrees
with the ordinary frame-time reports. Object collection really ran during ON
windows, and restoration records were present. No defect in those calculations
or that mode switch was established.

The main limitation is workload control. Camera position alone does not capture
changing AI, effects, texture loading or draw counts. Short, consecutive windows
also cannot reproduce startup-only settings and persistent caches for every
kind of optimization. The collection candidate's first trial drifted; its
steadier second trial showed about 0.5% improvement in that particular view.
That does not settle its value in movement, combat, other maps or a combined
build. Existing evidence does not establish a universal noise threshold.

Object collection's mode is read on each walk and has no cache of its own, so
there is no known missing restart requirement for that test. The new collision
vertex candidate uses a startup mode for this gameplay evaluation; this is not
evidence that restarting itself improves its performance.

## Combined fresh-launch builds

The collision vertex pass (`060a3c1`), object-reference collection startup mode
(`97160cb`), and segment/sphere math (`6f44bf4`) are retained together. The latest
candidate starts all three modes ON; repository defaults remain OFF while these
fresh-launch gameplay checks are in progress. Passive mode and admission counters
confirm that the selected implementations actually execute. No selector-44 or
selector-45 live control is shipped.

The vertex-only and vertex-plus-collection builds both reached about 11.5 FPS in
the first campaign room. The external displayed-frame checks measured 11.534 and
11.495 FPS respectively. These are live simulation observations, not a controlled
small-difference verdict: they establish no clear gain in that room and do not
settle movement, combat, other maps, or the complete combination.

Small improvements can accumulate when they shorten different work on the frame's
critical path. Their FPS gains cannot simply be added. For example, going from
15 to 20 FPS requires the frame to fall from 66.7 to 50 ms, a saving of 16.7 ms.
CPU work completed earlier may merely spend longer waiting on an unchanged GPU
dependency. Keep qualified changes and evaluate their combination; do not claim a
five-FPS benefit until ordinary gameplay supports it.

The preceding five-path runtime is `31af4cf390bd5f58d58429d3902f6107174de6803777d09dd1ada3d8ffc84812`
from source `1823407`. It retains those three helpers, earlier exact query
publication and conditional read-only depth-store omission together, on top
of the preceding worker/rendering changes. All five startup modes are enabled;
the newest stencil refinement admits a final continuation per campaign frame.
The latest same-checkpoint observation is 12.769 FPS versus 12.660 FPS for its
parent: inconclusive, with all five changes still enabled. See the
[stencil follow-up](depth-store-stencil-20260917.md) for evidence and limitations.

The preceding six-path runtime is
`a4e35d47a6d5db8d11e4915708ce93b9e900a8bc1893baf62a2cb61f98859eda`
from source `ca0f3b0`, installed in slot 0. It adds the existing grouped exact
vertex comparisons as a sixth startup path, retaining the preceding five and
the worker/rendering improvements. Fresh-launch logs confirm the mode and
nonzero comparisons. Normal graphics and effective clocks remain unchanged.
Its first campaign launch loaded a different profile's cryo-tutorial save;
that result is excluded from comparisons with the previous pistol checkpoint.
Explicitly selecting New001 restores the earlier checkpoint and camera. Its
ordinary displayed-frame observation is 12.7725 FPS versus 12.7689 FPS for the
five-path build: inconclusive, with all six paths retained.
See the [grouped-comparison follow-up](vertex-block-loads-20260916.md) for the
validation and admission evidence. The combination remains under evaluation.

The preceding seven-path runtime is
`fae0f1808a892ca9caf3e4a3692aaabf2fd307bda4cff72171dc17c504bf9730`
from source `c5a198a`, installed and boot-confirmed in slot 1. It adds typed
collision traversal to the preceding six paths. Fresh normal-menu launch into
the same New001 pistol checkpoint measured 12.7518 FPS versus 12.7725 FPS for
the six-path parent. This is inconclusive, and all seven remain enabled. Short
campaign firing/movement checks and Blood Gulch plasma, rocket explosion,
self-hit/death/respawn and Warthog driving checks complete without reproducing
a crash. Driving remains below target; there is no matched performance baseline
for that route. Long-session crash qualification is outstanding. See the
[typed-traversal follow-up](native-collision-traversal-20260917.md) for production
stack checks, startup evidence and the unmeasured admission distribution.

The preceding eight-path runtime is
`98393693e68be0032c5381cc172611a4db1361cf16d7dfeb7917136b9041fa99`
from source `47c7e7c`, installed and boot-confirmed in slot 0. It retains all
seven paths and adds caller-specific collision-query fusion in a separate
compiled unit. The generic query objects remain byte-identical to the parent;
the selected fused unit matches the qualified code. See the
[production integration](native-query-fusion-production-20260917.md).

Ordinary Blood Gulch gameplay has completed plasma charging/firing, Warthog
entry, reverse/steering and a valley drive, rocket pickup, explosion,
self-hit/death/respawn and pause without reproducing a crash. The initial view
reported about 13–14 FPS; the driving capture reached 6 FPS, with nearby
60-frame log windows reporting 8.1 and 9.8 FPS. These are different views and an
unmatched route, not an optimization comparison. The valley remains below
target. All eight changes stay enabled; no five-FPS gain or reproducible
performance regression has been established. The seven-path executable remains
in slot 1 for rollback. Short smoke checks do not settle long-session crashes.

Startup logs confirm native 960×544 resolution, Standard (256) texture detail,
original material/glow/particle/model quality, enabled shadows/reflections/
effects/decals, and triple buffering. Effective clocks remain CPU 444 / GPU 222
MHz. Existing mode/counter records confirm the retained selections; fusion
itself has build-time call routing and boot-hash identification, with no dynamic
invocation counter. Private evidence is in
`direct-cluster-query/query-fusion-startup/`, including `startup-modes.json`,
`gameplay-actions.md` and `bloodgulch-smoke-receipt.json`.

The same process then returned through the main menu to New001's Normal
Pillar of Autumn pistol checkpoint, confirmed by reads of save directory
`122A17771B9E` and the original camera position. Two pistol shots, camera input,
movement and pause completed. Ordinary initial reports were 12.7–12.8 FPS;
the preceding Blood Gulch load makes this a transition/gameplay check, not a
fresh-process matched comparison. The campaign log has no searched fault marker
and the logger reports no error. `campaign-smoke-receipt.json` preserves the
evidence. A load-level menu capture still has clipped/overlapping text; no new
build attribution or blanket visual-correctness claim follows from this smoke.

The subsequently qualified CPU prototype targets the complete collision-response solver
(`172CB8 → 170C10` and ten direct helpers), keeping the actor transaction held.
It handles different work from the retained query paths. Historical sampled
solver elapsed time was about 17% of its movement wrapper, including nested
work and scheduling; its current frame share and possible gain are unknown.
The prototype preserves full-call state, arithmetic and scheduling and joined
the eleven-path build. The earlier experiment that unlocked this
solver remains excluded. In parallel, the 27.86 ms/frame visibility wait in
one valley window remains a rendering dependency to investigate; it overlaps
the other measured intervals.

## Visibility dependency follow-up

A review of the combined campaign capture found approximately 11.7 ms per frame
waiting for exact flare visibility results. The original final GPU notification
was observed about 81.7 ms after submission began, versus an 87.1 ms frame period.
Those intervals overlap; they are not additive CPU and GPU execution times. The
observations suggest that this dependency can hide CPU savings in this view.
Other Blood Gulch views have shown a different limit.

The retained query-boundary change moves publication earlier in the queue:
its first hardware view had no flare waits, so it did not exercise the expected
benefit. The candidate publishes exact results after the last query writer at an
existing scene end. All geometry, textures, UI and packet storage remain owned
until the original final completion. No extra scene or stale query result is
introduced. Fresh startup selection allows this to run alongside the CPU stack
without an in-game benchmark toggle.

The combined build with all three CPU helpers and earlier query publication has
now reached physical campaign gameplay. The passive displayed-frame observation
was 12.728 FPS, compared with 11.495 FPS for the earlier two-helper build at the
same checkpoint view. This is encouraging, not a confirmed 1.2-FPS gain: live AI
can vary, the loading histories differed, and the newer build adds both the
segment/sphere helper and query publication. It does not isolate either change.

Startup and admission logs confirm all four paths execute. Query notifications
were observed before final completion, while storage remained retained until
the final notification. If no suitable scene boundary exists, the original
final notification is retained. Remaining waits and GPU throughput can still
hide CPU savings. These short gameplay checks do not resolve the older GPU
crashes or establish stable 20 FPS.

## Retest queue

These implementations remain in the source; they were not deleted.

| Order | Candidate | Earlier limitation |
| ---: | --- | --- |
| 1 | Object-reference collection | Small result in one campaign view; other workloads and combinations untested. |
| 2 | Grouped vertex comparison loads | Opposing results with varying workload at 360p/reduced settings. |
| 3 | Polygon-edge math | Small savings in one view, mixed results in another. |
| 4 | Clip-wrapper fusion | No gain in tested Blood Gulch views; startup control and native stack headroom need review. |
| 5 | Earlier query-result publication | The tested heavy view had no flare waits, so it did not exercise the expected benefit. |
| 6 | Omission of read-only depth stores | Mixed results; broader ON rendering and effects checks remain necessary. |

Use separate baseline and candidate launches with the same checkpoint/map,
resolution and a comparable gameplay route. Preserve the ordinary FPS, workload
and crash logs. For an independent timing check, record the displayed-frame
counter against host monotonic time, including request latency; no in-game mode
switch is needed. Compare repeated gameplay segments before calling a small
difference a gain or regression. Then evaluate compatible changes together.

None of these retests is complete merely because a package was built or booted.
Keep known unsafe actor/solver publication experiments out of this queue, and
distinguish never-deployed prototypes from previously tested candidates.

The preceding nine-path runtime is
`d817f077ef551ae1da2888f895497be711d03a51c60d7921048486869d68c668`,
from source `1061b17`, installed and boot-confirmed in slot 1. It retains all
eight paths and selects decoded RGBA swizzling at process startup. Exact pixel,
mip, cache and startup tests pass; only the uploader object changes. Actual
swizzled descriptors are confirmed in the bounded physical upload log, alongside
the expected unsupported-dimension linear upload. See the
[layout startup follow-up](rgba-swizzle-startup-20260917.md).

The fresh process loads New001's same Normal pistol checkpoint, confirmed by
save-directory reads and the original camera. Initial settled ordinary reports
remain around 12.8 FPS. Two fire inputs, a camera turn, strafe and pause complete;
captured checkpoint/NPC views render, no searched fault marker is found, and
the logger reports no error. This short smoke is not extended combat or crash
qualification, and no repeatable performance gain or regression is established.
All nine remain enabled at native resolution and standard graphics. The eight-path
runtime remains in slot 0 for rollback.

Normal-menu Blood Gulch startup, plasma charge/fire with vehicles visible, Warthog
driver entry, forward/reverse/steering and exit also complete. Initial reports are
14.6–14.7 FPS at a different spawn; driving captures show 6–8 FPS and a later
stationary wall view reports 10.3 FPS. No matched route comparison or speedup is
claimed. The ninth path remains enabled; short smoke does not settle longer
sessions or the historical crashes. Private receipts and exact actions are under
`rgba-swizzle-startup/`.
