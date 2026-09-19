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
