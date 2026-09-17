# Gameplay performance retests

The user requested fresh Xita launches and ordinary gameplay instead of running
the built-in benchmark after each change. Recent candidates left disabled on
the strength of those comparisons need another look. Keep normal graphics
settings fixed and record the installed executable and effective startup modes.
Restarting means a new Xita process; it does not require rebooting the Vita.

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

## Visibility dependency follow-up

A review of the combined campaign capture found approximately 11.7 ms per frame
waiting for exact flare visibility results. The original final GPU notification
was observed about 81.7 ms after submission began, versus an 87.1 ms frame period.
Those intervals overlap; they are not additive CPU and GPU execution times. The
observations suggest that this dependency can hide CPU savings in this view.
Other Blood Gulch views have shown a different limit.

The current retest moves existing query-boundary publication earlier in the queue:
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
