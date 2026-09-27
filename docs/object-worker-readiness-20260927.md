# Object worker viewport readiness

Status: predicate corrected in source; not deployed or gameplay-qualified.
Do not infer an FPS gain or safe arbitrary concurrent guest updates.

Perf270's saved hardware log has hierarchy admissions but reports every
hierarchy suspension as idle (e.g. 1,305 batches per 60-frame outdoor window,
zero assists). Its object-job readiness messages retain mask 80. Separate scene,
recorder and GPU-submission threads still run; these counters concern the
experimental whole-object callback workers, not all multicore support.

Inspection found an always-failing strict predicate in xd3d_object_jobs_ready:
with tolerance off, allowed lag is zero; lag one stays one, while every other
lag is rewritten to two. Both fail `lag > allowed`. Thus even the documented
exact previous-frame condition cannot be admitted. The expression originated
in 43feb869, whose stated intent was to keep exact frame-1 admission and make
lag 0/1 tolerance opt-in after perf144's slower object-worker results.

The source now expresses that policy directly: strict lag == 1, opt-in tolerant
lag <= 1, using unsigned subtraction for counter wrap. The map/loaded/active,
camera, script-state and 90-stable-frame gates remain unchanged. Tolerant mode
is not enabled. At low FPS, alternating equal/previous-frame observations can
still reset strict stability; correcting this bug does not guarantee admission.

`tools/test_object_viewport_ready.py` compiles the production predicate and checks
strict/tolerant modes, 512 lags across seven frame values, future frames and
uint32 wrap. It reproduces the previous strict-case rejection. It also checks
the production readiness function calls the tested predicate and uses its result.
The expanded test also compiles the complete production readiness function with
mock guest memory. Both modes pass the 90-frame stabilization requirement,
repeated-tick checks, every map/loading/camera/script reset gate, both accepted
camera modes and frame-counter wrap. This is not a concurrency test.

Next: qualify the corrected gate in the Pi campaign harness, explicitly inspect
whether jobs actually engage and whether the guarded helpers remain correct.
Earlier workers could be substantially slower and have known shared-state risks;
do not silently broaden camera/lag policy or promote the fix to hardware merely
because the predicate test passes. Current hardware is perf271, which contains
the cold-shader logging consolidation but not this readiness change.

## First Pi campaign result

The corrected gate completed a 180-second campaign capture (planned timeout
124) in private `../object-readiness-pi/`. Workers actually engaged; aggregate
counts are preserved in `summary.json`, with the command and full log alongside.
This is stronger than initialization alone, but not gameplay qualification.

The run produced impossible tick phase durations and repeated scope overflows.
Inspection of `xk_scene_thread.c` exposes only two profiling stacks: scene helper
and every other thread. Object workers therefore share the tick stack when
phase instrumentation is enabled. These timings are invalid for attribution;
the profiler needs worker exclusion or independently owned stacks before
profiling this configuration again. This does not prove that gameplay itself
is correct or corrupted. No worker-gate hardware deployment is authorized by
this result. Keep perf271 installed while fixing diagnostic ownership.

## Diagnostic isolation candidate

Both phase begin/end now exclude actual object-worker threads using the existing
native-thread identity query, compiled only with experimental object jobs.
The ordinary owner/scene attribution remains, and object worker work counters
remain available separately; parent elapsed time still includes worker joins.
This changes instrumentation, not task scheduling or simulation.

`python3 tools/test_scene_phase_worker_isolation.py` extracts the production
phase implementation and runs two worker pthreads concurrently with owner and
scene scopes. It verifies exact call counts, nesting and empty stacks. Removing
the exclusion causes the regression fixture to fail. This is a host concurrency
fixture with mocked thread identities, not proof of all guest scheduling.

A fresh ARM campaign harness in `../object-readiness-isolated-pi/` carries the
same readiness change and only this instrumentation exclusion atop the retained
phase implementation. Build succeeded; the 180-second campaign capture is
running. Inspect terminal results before drawing any performance conclusions.
Hardware remains perf271; no experimental gate deployment occurred.

The isolated retest completed after 180 seconds (expected timeout 124):
108 report rows, 54 active, 3,318 passes and 720,010 jobs. No phase overflows,
worker STOP, scene ABANDON or guest-trap lines were found. The previous implausible
tick durations are gone. This establishes usable owner-phase instrumentation,
not equivalent gameplay or a hardware speedup. Existing player-vector diagnostic
oddities also occur in the pre-gate baseline and are not newly attributed here.

A separate save-write timing change uses existing XV_SAVE_LOG and timestamps
only selected save transfers, excluding logging. No write, completion, offset,
flush or durability policy changes. VitaSDK compiled xk_file.c and the profiling
fix successfully; receipts in private ../save-write-timing/.

Perf272 staging in ../worker-readiness-hardware/ retains perf271 generated code,
imports the readiness predicate, profiler guard and save-transfer timer only.
No lag tolerance or camera-policy expansion. Hardware remains perf271 until the
candidate has built and deployment is explicitly recorded.

## Perf272 package

The build completed successfully: perf272 / 36f7b0ed.
Runtime SHA-256 `f50ec06e6a9c1d4d6ade3dca7caf6bc1f33941094db28582af3ec4dcda1fc9d2`,
34,818,458 bytes. Package SHA-256
`b0a961b454fe5b9dd2dd4ea7cc92557d102503f6ac240d2906f00cf225f8dd73`.
The update contract matches perf271; package comparison confirms only
`game-a.self` and `boot-game.txt` differ. All generated C units remain unchanged;
only xd3d.c, xk_scene_thread.c and xk_file.c changed among runtime C files.
Receipts are in private ../worker-readiness-hardware/.

Pre-update perf271 log preserved (8,166,181 bytes). Update upload has started;
installation requires a verified boot receipt, not just completion of upload.

Deployment completed: updater verified the runtime hash, selected slot 1 and
confirmed boot. Live status reports `0.2.0-perf.272 / 36f7b0ed` at the dashboard
with timing_frame=0. Perf271 remains slot 0. One-hour awake lease renewed by
update/launch. The a30 launch sequence is running with unchanged 32 settings,
360p and protected `a30-perf211` test-save namespace. No perf272 gameplay result
yet; inspect actual worker admissions as well as full-frame intervals.

## Bound readiness diagnostics

Perf272 loading logs show viewport-related reason masks alternating between
simulation ticks. Source now rate-limits changed-mask diagnostics to one per
60 rendered frames (first observation immediate), without changing readiness
checks, counters or admission. Camera-mode diagnostics remain unchanged.
The readiness fixture now observes the actual logging branch, checks spacing
across frame wrap and runs 1,000 alternating lag-0/1 ticks under both policies.
All prior gate assertions and new logging assertions pass. No measured FPS
benefit is claimed. This source adjustment is not in installed perf272.

The perf272 launch script completed; the capture collector is waiting on actual
loaded/active/director telemetry. Loading-screen FPS is excluded. Keep its
existing process running rather than issuing another launch while loading.

## Perf272 lifepod result and admission limit

Hardware reached the lifepod (idle-before.png visually checked). The settled
540-frame sample averaged 72.634 ms / 13.77 FPS, p95 91.663 ms, maximum 161.595 ms;
518 frames exceeded 50 ms and four exceeded 100 ms. No object-worker report
appeared. The capture contains 2,616 readiness transition messages: alternating
lag 0/1 observations reset strict readiness before 90 consecutive stable frames.
Therefore this is NOT a successful hardware worker-performance test. It is also
a regression from perf271's pod capture, with a new synchronous logging burden;
do not attribute it to actual parallel jobs, which were not admitted.

The first 3,428,352-byte checkpoint transfer measured 199,353 us; the preceding
512-byte profile write measured 1,114 us. These are actual synchronous transfer
timings, excluding diagnostic writes; this initial-save result does not prove
which later movement frame contains a save. The gameplay capture is continuing.

Perf273 contains only the bounded-readiness-log adjustment atop perf272.
It is built and packaged, not yet deployed. Runtime SHA-256
`9b7253247521d3da37630765415e5e05e272fa744a47362c76ff8ab815232950`,
34,818,386 bytes; package SHA-256
`f4ba6c680dcd2ff3bff40a914433b609e8f4df16d6844f82f1359219342dabbc`.
Keep the previous graphics settings and strict admission policy for its initial
launch; do not silently enable lag tolerance or broaden camera support.
