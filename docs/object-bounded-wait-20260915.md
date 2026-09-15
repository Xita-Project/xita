# Bounded object-worker mutex waits

The native-resolution plasma run includes a slow window where point-transform
callers on lane 0 accumulate 800,218 microseconds of elapsed shared-guard waits
across 60 displayed frames. These counters identify waiters, include scheduling
and parked service time, and overlap other lanes. They do not establish how much
frame time can be recovered.

The contended path currently tries the mutex and sleeps for 50 microseconds
before trying again. The candidate instead waits on the selected mutex with a
fresh 50-microsecond timeout. An unlock can wake the worker directly; a timeout
returns immediately to the cooperative service-park check. An infinite wait
would deadlock when another lane holds the guard and requests a quiescent owner
service, so the timeout is required. Firmware may modify its timeout argument;
each attempt supplies a new budget. Unexpected kernel errors remain fatal.

The critical sections, lightweight-mutex selection, private math, two object
workers and audio/file owner handoffs are unchanged. The candidate defaults off
and is enabled with `XV_OBJECT_TIMED_WAIT=1` or the remote `object-wait`
comparison. The comparison switches only after presentation drains and worker
batches join. It measures sleep/poll, bounded wait, sleep/poll in one fixed view,
then restores the configured policy. Per-lane counters report bounded attempts,
acquisitions and timeouts at the existing joined reporting boundary.

## Host and package validation

The production worker suite passes with both wait policies, profiling off/on,
and two/one/zero workers, normally and with ASan/UBSan and ThreadSanitizer. It
covers contending workers, held-guard audio/file owner requests, stack/context
preservation and the existing unsupported-service rejections. The private-math
suite passes ten worker/policy combinations normally and under ThreadSanitizer.
The mutex adapter tests both SDK backends, timeout-budget reset, recursive
acquisition, wake after release and unexpected errors. Benchmark availability,
restoration, cancellation, missing hooks, frame-drain ordering and the remote
protocol checks pass.

The native package builds successfully and validates against the installed asset
contract. Of 1,588 members, only the runtime and boot marker change. Runtime:
`10ef0c0d2369eacdef8a16a0be0fcaaaa1e0a8efb6cdd031ac3508e213fcb09c`.
Evidence is kept privately under `engine-restructure-20260914T2300Z`, in
`object-bounded-wait-*` logs and the matching package/hash receipt.

## Emulator check

The exact candidate boots through the normal profile, map and Slayer menus into
Blood Gulch. The remote off/on/off comparison completes with a consistent camera;
logs confirm thousands of bounded acquisitions and actual timeouts, followed by
restoration to sleep/poll. No worker STOP is recorded. Vita3K is capped at 20 FPS,
so this validates execution and restoration only. It is not hardware performance
evidence. Captures and receipts are in `emulator-object-bounded-wait/comparison/`.

The physical comparisons below reject enabling this candidate by default.

## Physical installation

The updater confirms the exact candidate runtime in slot B1, with state 0, no
pending request and a completed dashboard frame. A0 was restored and verified
as `7f33dee4…` before upload; that rollback runtime remains preserved. The prior
current log and all three history logs were captured before the update. Receipts
are in `physical-object-bounded-wait/`. Saved graphics are not changed by the
update. Both experimental workers and lightweight mutexes remain enabled.

## Physical result: keep the existing wait policy

Three same-view Blood Gulch trials at **960×544 native** give:

| Trial | Sleep/poll before | Bounded mutex | Sleep/poll after |
| --- | ---: | ---: | ---: |
| 1 | 9.237 FPS | 7.344 FPS | 9.171 FPS |
| 2 | 9.146 FPS | 7.279 FPS | 9.129 FPS |
| 3 | 9.160 FPS | 7.153 FPS | 9.209 FPS |

Every camera check passes at position `29.6260, -76.3674, 0.9251`, direction
`0.94236, -0.33466, 0`. This is a different spawn/view from earlier mutex-backend
trials; compare only the paired arms here. Saved graphics stay unchanged. The
actual clocks are CPU 444, GPU 222 MHz; the requested 500 MHz CPU clock is rejected
by this device and falls back to 444. Both object workers, private math and the
lightweight mutex are enabled in every arm.

The bounded wait is **20.2%, 20.3% and 22.1% slower** than the mean of its two
surrounding baseline arms. Baseline drift is below 0.8% in each trial. The
existing sleep/poll policy is restored and remains the configured default.
There is no achieved performance gain from this candidate.

The first trial's reported object windows explain the direction: joined batches
rise from approximately 31 ms to 58–59 ms per displayed frame. Contended guard
acquisitions rise from about 1,000 per lane per 60 frames to about 53,000; the
bounded path successfully wakes/acquires in most attempts. Elapsed per-lane wait
totals roughly double despite shorter individual waits. These windows include
live simulation and need not align exactly with the measurement boundaries;
wait totals overlap and cannot be added as independent frame cost. The result
is consistent with frequent mutex handoffs costing more than allowing the other
worker to continue through several short operations. It is not evidence that
parallel object work in general is slower.

The completed logs contain no worker STOP, and counters verify actual bounded
acquisitions/timeouts followed by restoration. The benchmark receipt, screen
captures and derived percentages are in
`physical-object-bounded-wait/comparison/`. Final comparison log SHA-256:
`43e5094282a05ffc918ab00d355b368053fb7cf69ce67a941a30e8b80213ba9d`.

The next investigation is reducing lock frequency for wholly worker-owned math,
rather than changing wake behavior. First establish which inputs and outputs
are private and how much work that covers; shared inputs, global counters and
nested collision/cache transactions still require correct synchronization.

After restoration, a physical charged plasma shot reduces charge from 100 to 89,
and the completed-frame capture and remote status continue. The subsequent full
log records no worker STOP and confirms the wait policy is off. Its deferred
commit counter is zero, so this particular shot does not repeat the earlier
hardware audio-service reproduction. The prior successful reproduction remains
recorded in the audio-commit follow-up. Final gameplay log SHA-256:
`c3fd743b38080be160f056eba18247672e9dc718c0493e28716975622b5fe1e8`.
The Vita is left running in Blood Gulch with input released and the diagnostic
wait disabled. No sustained 20/30 FPS or representative driving/campaign result
is claimed.
