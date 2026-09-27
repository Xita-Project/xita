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
