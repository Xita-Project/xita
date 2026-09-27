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
This is a predicate test, not a full readiness state-machine/concurrency test.

Next: qualify the corrected gate in the Pi campaign harness, explicitly inspect
whether jobs actually engage and whether the guarded helpers remain correct.
Earlier workers could be substantially slower and have known shared-state risks;
do not silently broaden camera/lag policy or promote the fix to hardware merely
because the predicate test passes. Current hardware is perf271, which contains
the cold-shader logging consolidation but not this readiness change.
