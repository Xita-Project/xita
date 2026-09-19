# Capture FIFO notification coalescing

`XV_VERTEX_CAPTURE_NOTIFY=1` is an optional build flag, default **0**. It changes only the capture worker's event notifications. Queue order, private snapshots, completion publication, owner callbacks, GPU-copy tickets and GPU slot retirement retain their existing paths. The Make configuration stamp rebuilds only `runtime/xv_vertex_capture.o` when this flag changes. There is no runtime setting or per-job timer added.

The perf18 settled checkpoint had a median 3773.5 queued jobs per 60 frames (about 63/frame), 324537.5 us capture, 280762 us worker and 3062 us join, 3.5 drains, one pressure drain, max-pending 32 and zero failed jobs. The old path attempts two event-set calls per job, about 126/frame. These overlapping timings do not give an exclusive notification cost or an FPS prediction.

## Wake protocol

Queue counters still own work. After release-publishing `cap_submitted`, the producer exchanges the wake state to `PENDING` with acquire-release ordering. It signals the worker only if the previous state was `SLEEPING`.

The worker exchanges to `RUNNING` before scanning the queue. Once empty, it uses an acquire-release compare-exchange from `RUNNING` to `SLEEPING`. A concurrent producer either changes the state first, making the compare-exchange fail and forcing an immediate rescan, or observes `SLEEPING` and signals the sticky event flag. The exchange at the next scan acquires the producer's pending publication. No additional submission is necessary for progress.

The existing 20 ms idle wait remains finite. A failed wake signal can therefore delay a job until that timeout but cannot strand it. Shutdown always signals unconditionally, even if the coalescing state says running; a failed shutdown signal also retains the finite timeout recovery.

## Completion protocol

Before every completion reread, the owner uses an **acquire-release exchange** to arm `cap_waiting=1`. After release-publishing each `cap_completed`, the worker acquire-release exchanges waiting to zero and signals only if the previous value was one. If the worker's exchange precedes the owner's exchange in modification order, the owner acquires the preceding completion publication and its reread observes progress. If the owner's exchange precedes the worker's, the worker sees the armed wait and signals. Subsequent RMWs preserve that ordering chain. A plain store in the owner is insufficient for this proof.

The owner rearms on every drain-loop iteration and clears waiting before collecting callbacks or returning. The existing 1 ms completion timeout and 100 us error delay remain. A completed reread can legitimately clear waiting before the worker attempts notification; no event is then needed. Empty drains never arm waiting. Startup initializes both handshake words before starting the thread; release resets them only after worker join or a startup failure with no running worker.

## Counters and boundaries

The joined `[vertex-capture-notify]` row reports wake signal/skip, done signal/skip and failed wake/done event-set attempts. Signal counts are attempts, including failures. Shutdown's control wake is excluded. These counters use atomic relaxed increments and atomic exchanges at report because worker notification bookkeeping follows the completed publication: queue equality alone does **not** join that bookkeeping. A boundary job, or a failed attempt and its associated attempt count, can land in adjacent report windows. Totals across windows remain intact. Counts accumulate through worker restarts until report, matching the other window counters.

## Qualification

Run from the source root:

```
python3 tools/test_vertex_capture.py
SANITIZE=1 python3 tools/test_vertex_capture.py
THREAD_SANITIZE=1 python3 tools/test_vertex_capture.py
python3 tools/test_vertex_capture_notify_build.py
```

All three capture runs pass 24 configurations: the existing 12 packed/compact/reuse/persistent/ready combinations, each with notifications OFF and ON. The fixture compiles the actual production capture FIFO, uploader and copy worker with pthread implementations of Vita services. It forces arrivals on both sides of the worker's empty-to-sleep transition, a sparse single job, completion before owner arming, completion after arming, completion between reread and wait, a failed done event, a failed sparse wake, failed shutdown wake, eight-job coalescing with exact FIFO callbacks, and rearming between partial completions. It also holds the worker after completed publication while the owner reports, checking late counter attribution. Existing pressure, wrap, unmapped inputs, GPU lifetime, failure and shutdown cases run in every applicable mode. Test frontiers compile to nothing in ordinary builds.

The host sanitizers pass; TSan warns that the existing GPU-device fences are not instrumented. Queue atomics and host event synchronization are instrumented. Forced host schedules supplement the acquire-release argument above; they do not simulate ARM weak memory or Vita scheduler performance.

The Make gate passes default/OFF/ON/no-op/OFF transitions and malformed values. OFF preprocessing matches the original production body after canonicalizing assertion `__FILE__`/`__LINE__` diagnostics. A standalone Vita ARM compile passes and introduces no atomic-library imports. Two private negative controls are rejected: sleeping after the pending CAS fails, and suppressing an armed completion's event. Artifacts are in `../capture-notify-qualification`; no generated game assets are tracked.

No hardware performance result is claimed here. Physical validation should compare the new signal/skip/failure counts alongside the retained frame, capture, worker and join measurements, particularly checking whether failed notifications move work onto timeout recovery.

## Physical Vita: perf.19

`0.2.0-perf.19 / 78b5c55+` was uploaded, hash-verified and boot-confirmed in
slot 1; slot 0 retains perf.18. Runtime SHA256:
`f5e286547446928ce4fac6b225840e3ae7873a1e01f570049b410588e5b66b4d`.
The complete Vita build and package checks passed. Only `game-a.self` and
`boot-game.txt` differ from perf.18. This candidate retains its optimization
stack and also sets `XV_DRAW_PROFILE_DEFAULT=0`; this combined observation
cannot isolate the FPS contribution of either change.

Normal Pillar of Autumn loaded at the same checkpoint and camera
(-28.66, 32.52, 0.62; forward 0.56, 0.82, -0.15). Native 544-line rendering
and existing settings remain unchanged. These settings include earlier reduced
effects and disabled decals; they are not the all-Original preset. No diagnostic
benchmark or Vita3K validation was used.

Across the last twelve complete, settled 60-frame windows, notifications report
33,573 signal attempts and 55,615 skips: **62.36% avoided**, or 46.63 actual
attempts/frame instead of about 124 potential attempts/frame in those windows.
There were no failed notifications or capture jobs. Detailed `[draw-prep]`
rows are absent while index/cache, capture and owner reports remain available.

| Nearby window medians | perf.18 | perf.19 |
| --- | ---: | ---: |
| Frame time / FPS | 78.50 ms / 12.7 | 78.20 ms / 12.8 |
| Draws/frame | 154 | 150.5 |
| Capture elapsed/frame | 5.409 ms | 5.219 ms |
| Capture worker elapsed/frame | 4.679 ms | 4.762 ms |
| Capture join elapsed/frame | 0.051 ms | 0.050 ms |
| Tick-owner elapsed/frame | 36.033 ms | 36.177 ms |
| Scene-owner elapsed/frame | 40.198 ms | 39.965 ms |

These are overlapping observers with separate windows and live NPC variation.
The 0.3 ms whole-frame difference does **not** establish an FPS gain. The change
removes real event traffic but has not moved this scene toward 20 FPS noticeably.
Keep it in the cumulative research build, with the project default still Off.

A camera turn and three pistol shots completed; the magazine display decreased.
The post-input screenshot showed 9 FPS around effects, so firing-related frame
drops remain. The 1,869,872-byte post-input log contains no searched STOP, FATAL,
GPU-crash or trap markers, and all notification failure counters are zero.
This brief check does not establish long-session/combat stability. Controls
were returned to neutral. Evidence: `../ce-perf19/gameplay/`.

Further tiny notification changes are unlikely to close the remaining gap.
The next structural investigation is serialized actor/collision work. Collection
writes shared visit epochs and object stamps and reads mutable object chains;
merely dropping its outer guard would invalidate the transaction. Existing
native query loops remain enabled. Any wider parallel path needs explicit
ownership/publication boundaries rather than another unsafe lock removal.
