# Comparing the object-worker mutex backend

The [private-math comparison](object-private-math-20260915.md) did not improve
physical frame rate. Its fixed Blood Gulch view still acquired the recursive
guard roughly 140,000–150,000 times per 60 displayed frames. This experiment
compares the ordinary kernel mutex with the VitaSDK lightweight mutex API.
The available calls and work-buffer type are documented in
[VitaSDK's thread manager reference](https://docs.vitasdk.org/group__SceThreadMgrUser.html).

Both implementations protect the same critical sections. The existing
per-worker recursive depth, private math policy, contention delay, owner
notifications and quiescent service acknowledgements remain in place. This is
a test of synchronization cost, not removal of synchronization or a change to
object update ordering.

The worker pool creates both mutexes before starting threads. Failure to create
the optional lightweight mutex retains the ordinary mutex and rejects the
comparison. Only a drained frame boundary can select a different implementation;
switching with an active object scope or batch terminates the experiment. The
backend stays fixed while any worker or owner can hold the guard. Shutdown
joins workers before destroying both successfully created mutexes.

`XV_OBJECT_LIGHT_LOCK=1` selects the lightweight mutex at startup. The default is
the existing kernel mutex. The remote `object-lock` comparison runs kernel /
lightweight / kernel with identical workers, math and graphics settings. It
restores the configured selection on completion, cancellation or lost control.
`[object-lock-backend]` records the selected implementation beside frame reports.

The host adapter test compiles against the installed VitaSDK declarations and
uses recursive pthread primitives beneath platform shims. It checks 16,000
protected concurrent updates, recursive and contended try-lock results, API
selection, configured restoration, creation failure, exact destruction, and
termination on unexpected errors for both backends. Normal, ASan/UBSan and
TSan runs pass. Production worker audio/I/O handoff, remote protocol, frame
acquisition and benchmark restoration checks also pass.

Host shims do not establish firmware correctness or performance. Native emulator
and physical validation are required before calling this an optimization.
