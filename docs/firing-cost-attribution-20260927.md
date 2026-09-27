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
