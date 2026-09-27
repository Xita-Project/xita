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
