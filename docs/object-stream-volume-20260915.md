# Rocket firing: stream-volume owner handoff

The recovered physical log identifies an intentional experimental-worker STOP,
not a diagnosed GPU fault. Worker lane 0 called `CDirectSoundStream_SetVolume`
at `193D4F`, returning to `2982F`, for object `E3F40035`. The indirect chain is
`C3A00 -> 2A180`; the private stack canary remains `584A4F42`. The user missed
the console error code, but the complete saved log contains this explicit STOP.

The failing runtime was `a8d89663…`, with both object workers and lightweight
mutexes enabled. Its last completed rendering window reports 12.7 FPS at the
rocket pickup location; this is not a controlled performance comparison.
Private evidence: `physical-return-after-rocket/run-1.log`, SHA-256
`c54ef134c26e38d6a8bac0666c0e8b080ba5c47dcb00c4cc5ff4eaba21c02682`, under the
September 14 engine-restructure validation directory. All four available run
logs were preserved before another update.

## Change

The original caller computes and clamps a volume, pushes that value and its
stream object, and calls the existing two-argument DirectSound handler. That
handler looks up the stream, updates the existing audio voice's volume, and
returns normally. It has no guest callback or scheduler entry.

The experimental worker pool now admits this exact target and return site
through its existing owner-service queue. All other object lanes park before
the owner invokes the unchanged handler. The worker retains its private stack,
job identity, arguments, result, and any enclosing shared transaction. A new
`quiescent owner stream volume updates` counter makes physical execution
observable. The owner-only test configuration executes the same handler.

Unknown callers and nested services inside an active completion callback remain
rejected. This does not disable multicore, mute sounds, replace the audio mixer,
or change the original volume calculation. It fixes this specific stop; further
unsupported services or shared-state dependencies may still surface in play.

## Validation

`tools/test_object_jobs.py` compiles the actual worker pool and stream-volume
handler. Each run services 600 volume updates, divided between calls inside and
outside a held shared transaction. Checks cover the servicing owner thread,
publication of parked workers' writes, original arguments, known/unknown
streams, signed volume values, stack cleanup, and counter reset. The unrelated
return site `292FB` is rejected before invoking the handler.

The full worker suite passes with two, one, and zero worker threads, with wait
profiling both off and on, under ordinary compilation, ASan/UBSan, and
ThreadSanitizer. Existing cache, resource, vertex-lock, completion/refill and
nested-service rejection checks also pass.

The native package builds and verifies all 1,588 members against the updater
contract; only the runtime and boot marker change. It includes the separate
[fragment-constant failure guard](rocket-submission-audit-20260915.md).

Runtime SHA-256:
`e6d9b04dfd53cb6cc194ee2d17f1740a8fad14f67ee2e80bb086631b5f4d288c`

Physical firing verification and representative campaign/driving stability
remain required. No new FPS gain is claimed for this handoff fix.

## Installation

The exact candidate reaches Blood Gulch through the normal split-screen menus
in the private emulator, with no worker STOP or fragment-constant failure in
its captured log. The new volume counter is present but remains zero in that
sample, so it is not a reproduction of the hardware-only call path.

Wi-Fi installation is complete and the physical dashboard confirms runtime
`e6d9b04d…` in slot B1, updater state 0, with no pending request. Before uploading,
slot A0 was restored and verified as `7f33dee4…`; that rollback build remains
preserved. The user is taking over controls for the rocket pickup/firing test.
The installed build retains both experimental object workers and the measured
lightweight-mutex default. A successful installation does not close the physical
firing retest or the sustained-frame-rate goal.
