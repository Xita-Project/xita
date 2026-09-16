# Bounded asynchronous periodic reports: opt-in prototype

This change moves already-formatted periodic report bytes to a native writer.
Formatting, report callbacks, live counter snapshots/resets, game state and GXM
work stay on their existing owner. Both console and file output move for these
explicit scopes. Ordinary and critical logging remain immediate.

It is **disabled by default** and has no hardware performance result. Build with
`make RECOMP=1 XV_PROFILE_ASYNC_REPORT=1`, then set the startup environment/config
`XV_PROFILE_ASYNC_REPORT=1` to opt in. The exact value `1` is required. The normal
build omits the queue and writer. An explicit recording-owner API can also
initialize an idle writer and select admission for a same-session comparison;
it does not require enabling the startup environment option. No benchmark enum,
remote control or graphics setting is added here. The environment-start path
starts the worker after loading configuration, before game/report producers.

## Why this target

The prior storage audit's 132 periodic reports have a median cost of 10.730 ms,
maximum 137.126 ms, and total 1.579 seconds across 7,920 reported frames. Their
amortized cost is about 0.199 ms/frame. These are report costs, including
formatting and console/file output, not frame-time percentiles or isolated SD
latency. The capture mixes loading and gameplay. This prototype targets visible
periodic stalls; it does not establish a route from roughly 11 FPS to 20 FPS.
Formatting remains on the producer, and the writer still consumes CPU and I/O.

## Ownership, ordering and memory

Four fixed 32 KiB slots accompany the existing 32 KiB builder: 160 KiB of payload,
plus metadata. Each chunk stores a report ID, frame, chunk index, final marker,
capture time and monotonic accepted sequence. The producer copies bytes before
publishing. The head stays owned by the writer through both console and file
output, and is released only after success. Caller buffers can be overwritten
as soon as the write API returns. Oversized inputs split into ordered chunks;
there is no truncation or allocation in Present.

The writer is a real `sceKernelCreateThread` with a kernel-owned 64 KiB stack,
priority `0x10000120`, and `SCE_KERNEL_CPU_MASK_USER_ALL`. It never runs a guest
fiber, game/GXM callback, object job, formatter or report snapshot. It does not
touch the guest file/descriptor layer. Its priority/affinity are experimental;
the user cores are shared with existing work, not assumed free.

A brief queue lock protects slot ownership, metadata and counters. It is never
held during formatting, console output, file I/O, waits or joins. Sink locking
is separate. Mutex initialization and file rotation are serialized exactly once;
the file is initialized before publishing the worker. Failed mutex, semaphore,
thread or thread-start initialization unwinds worker handles and retains
synchronous output. Mutex creation failure uses a serialized synchronous sink
fallback; failed file open remains console-only and makes checked sync fail.

Wake/completion semaphores are hints; sequence counters establish ownership.
Timed rechecks preserve progress even if a signal fails or saturates. The idle
writer uses a 100 ms fallback timeout rather than polling every frame. Completion
waiters recheck at up to 1 ms intervals. No queue or file handle is destroyed
while an admitted producer or barrier can still use it.

Full queues wait for space without holding queue or sink locks. They do not
overwrite, discard, or synchronously spill ahead of earlier periodic bytes.
Backpressure is deliberate: bounded memory and lossless output cannot also
promise a producer that never waits during an indefinitely stalled sink.
FIFO is guaranteed among periodic chunks. Immediate/foreign critical output
may appear between them, including inside a multi-chunk report.

## Failure and barriers

Positive `sceIoWrite` counts advance the exact saved file offset. Zero, negative,
or impossible file counts enter ERROR without completing the head. Console
output has a separate contract: every nonnegative `sceClibPrintf` return accepts
the whole supplied chunk; it is not a partial-byte count. This supports both
zero-status and positive-return implementations. A negative console result
retains the last wholly accepted chunk boundary. Retry resumes at the saved
offsets, with the accepted slot still owned. Consumer failure diagnostics use
the direct console path, never the queue or file mutex.

`xv_log_get_status()` distinguishes accepted, written and synced sequences and
bytes. `written` advances only after the full chunk succeeds; partial file bytes
are still counted. A report is complete only after its final chunk succeeds.
`synced` advances only after successful file sync. The status includes the head's
report/frame/chunk, capture time, length, failed sequence/offsets, queue watermark,
backpressure count/time and worker console/file-lock/file/sync timings. These
counters describe periodic output; they are not whole-process logging totals.
An immediate-output error is sticky and cannot be erased by retrying a periodic
chunk whose bytes happen to be intact.

`xv_log_flush_wait(timeout_us)` publishes the calling owner's buffered chunk,
captures a sequence boundary, waits through it, then syncs on the writer. A
foreign open report returns BUSY, and a writer self-call returns SELF. Later
accepted chunks cannot move an already registered sync target. Concurrent flush
requests serialize their sync registration while retaining their own captured
targets. An owner flush that times out on full capacity retains its builder.

`xv_log_shutdown(timeout_us)` closes async admission, waits for admitted scopes
and barrier users, drains and syncs, requests writer exit, joins, and then deletes
the thread/semaphores. An open scope on the calling owner returns BUSY. Timeout
or error keeps all pending slots, waiters and handles alive. Repeating shutdown
after a timeout, join timeout, or successful stop is defined. Once stopped,
subsequent ordinary logs use the synchronous sink; the FD and sink mutex remain
alive for final exit diagnostics. Restart after a successful stop is unsupported.

`xv_log_retry()` is an explicit C API for a failed periodic sink, preserving a
pending shutdown's closed admission. There is no automatic retry loop and no
new remote retry command. A full queue under persistent ERROR intentionally
keeps its producer waiting until recovery. Impossible file counts cannot establish
what a broken sink actually committed; file retries assume the normal syscall
count contract. Console output has no durability guarantee. A negative console
result cannot reveal whether part of its failing chunk was displayed, so an
explicit retry can repeat that unknown part; exact partial-file accounting does
not imply an exact console offset within a failed call.

## Same-session admission controls

The writer's lifecycle and its report admission are separate. A RUNNING writer
can stay idle throughout an OFF arm. Status `enabled` is the committed admission
mode; `transition` is set while a mode boundary closes admission and drains.
Counters remain cumulative across arms, so a controller takes snapshots/deltas.
No per-frame clocks or thread-identity checks are added for this controller.

- `xv_log_async_available()` returns compile capability only, without opening a
  file or creating a worker. A positive result does not guarantee initialization.
- `xv_log_async_init()` must run on the joined native recording owner, never a
  network callback. It creates an idle OFF writer if absent and claims control
  for that native thread. Init on an already environment-started ON writer
  claims control without changing admission or dropping queued bytes. Repeated
  init by the same owner is allowed; foreign claims are rejected. Initialization
  can perform synchronous setup I/O and belongs before measured frames.
- `xv_log_async_enabled()` is a thread-safe, read-only committed-mode getter.
  It returns zero when compiled out and during a fresh idle/OFF session. An
  in-progress or failed transition still exposes the prior mode.
- `xv_log_async_set_enabled(enabled, timeout_us)` accepts exactly zero or one
  from the claimed owner. It rejects any open report (including the caller's)
  or active barrier user, serializes with initialization/shutdown, closes report
  admission, drains accepted bytes and syncs, then commits the requested mode.
  A repeated same-mode call still establishes a new checked sync boundary.

The logger enforces native thread identity after explicit init, but it cannot
prove that guest jobs have joined or that the initial caller is the recording
owner. Those are integration preconditions. The startup caller does not claim
control because it creates the separate recording thread later. There is no
owner rebind or restart after successful shutdown. Worker self-calls and invalid
lifecycle states are rejected using the existing status codes; init failures
retain the startup error for diagnosis. Compile-OFF controls return UNAVAILABLE.

A failed setter preserves the prior admission mode, queued bytes, offsets,
errors and live handles. A timeout can leave its sync request outstanding; a
later boundary waits for it and requests a new sync. Failed boundaries invalidate
the comparison arm. The controller must capture the initial mode, restore it
explicitly, check restoration's return code and never claim successful restore
while the setter fails. Persistent errors still require explicit recovery.

Ordinary/critical output remains immediate during transitions. The barrier
covers accepted periodic chunks and file writes completed before its sync; it
cannot promise durability for future concurrent ordinary writes. OFF periodic
reports use the existing synchronous grouping, including the original cost-line
text. An idle worker is present in both OFF arms of this comparison, so these
arms measure the admission change, not a process without a created writer.

Measure actual frame intervals and total measured wall time across OFF/ON/OFF,
with sufficient report occurrences per arm. Keep init/drain/sync and settle
frames outside the reported frame sample. Producer enqueue cost is not a frame
tail or average-FPS measurement; root integration owns that instrumentation.

## Report and updater integration

The existing 60-frame UI report remains formatted on its owner. Its cost line
uses a second periodic scope in async mode, so it does not reintroduce an
immediate file write after the timed report. It labels producer formatting,
enqueue and backpressure time separately from cumulative worker timings. The
known post-Present flare/frame/director diagnostics use another explicit async
scope. Histogram/tracing/gameplay calls are not enclosed in a broad report scope.
The synchronous mode retains its previous grouping and cost-line text.

`xv_log_criticalf()` bypasses buffering even on the report owner. Existing direct
trap/critical console paths remain untouched. Console evidence precedes any
file-lock wait; an immediate file write can still wait behind an in-flight
periodic file write. In particular, pre-drain update progress logs may wait
there before the timed logger shutdown begins. The remote service remains live
and exposes the pending queue in that case.

After producer/pump quiescence and GPU/display finish, `main.c` drains the logger
before stopping the remote service. An updater drain timeout/error retains
remote diagnosis and cannot advance to network stop or launcher handoff.
`GET /update` gains a bounded, read-only `log` status object without changing
existing handoff enum values. While the logger drains, existing handoff stage
DISPLAY_DRAIN plus log state DRAINING/ERROR identifies the location.

The final handoff marker uses an explicit checked sync before setting the
LAUNCHER_HANDOFF stage or calling `sceAppMgrLoadExec`. A final sync error refuses
the launch. Normal exit and failed LoadExec paths also flush their exit marker.
After the remote service has stopped, final synchronous marker/sync failures
are console-only diagnostics. The timeout bounds async waiting; it cannot cancel
a hung synchronous OS write/sync or forcibly kill a writer inside I/O. No success
or durability is claimed on failure. Ordinary process exit can still reclaim a
timed-out logger; the updater cannot use that as a successful drain.

## Validation performed

```
python3 tools/test_async_reports.py --output-dir recomp/host/build/async-report
make -C recomp/host test-log-batch
make -C recomp/host test-frames
python3 tools/test_update_handoff.py
python3 tools/test_log_handoff_markers.py
python3 tools/test_remote.py
```

The real production queue/sink runs against deterministic blocked syscalls and
real host pthreads in 47 scenarios, each under ordinary execution, ASan/UBSan and
TSan (141 successful scenario runs). They cover:

- Owned copies/caller overwrite, empty and 32 KiB boundaries, 65,537-byte input,
  200-report randomized FIFO/wraparound and exact file/console bytes.
- Full capacity and persistent-error backpressure, owner/foreign scopes,
  owner critical bypass, concurrent cold initialization, and worker self-calls.
- Partial/zero/negative/impossible file writes, console failure after a whole
  accepted chunk, failed sync, exact file-offset retry, startup failure at six
  stages, failed wake hints, and truthful incomplete sequences/report markers.
- Console success returns of zero, one and 2,048, each with runtime async both
  enabled and disabled; complete 1,024-byte console/file output and checked
  shutdown without a false sticky error.
- Owner flush/continued formatting, a flush target racing later admission,
  two simultaneous flushers retaining different FIFO targets, blocked console
  and file output, bounded stop, join timeout/retry and repeated stop with no
  deleted handle exposed to a waiter. An injected clock crosses the shutdown
  deadline between reads; the saturated remaining budget cannot wrap unsigned.
- The extracted production GPU/display/logger shutdown function with real
  blocked logger I/O: remote remains live until the logger drains.
- OFF/ON/OFF with one retained writer, exact OFF file grouping, repeated same-mode
  syncs, startup-ON owner claim without a mode flip, wrong-owner and open-scope
  rejection, active flushers, blocked file/console output, retained write/sync
  errors, and deterministic toggle deadline crossing. Concurrent ordinary I/O
  holds the sink while another thread checks closed transition admission;
  timeout preserves OFF and all handles before a later successful boundary.

The existing synchronous log fixture passes ordinarily and under ASan/UBSan,
including the three console-success conventions and control API stubs with async
compiled out. The UI fixture checks RUNNING-but-OFF grouping with batching both
enabled and disabled.
`make -C recomp/host test-frames` passes with the new log API stubs. The six
console-status regressions and deadline regression each reject their respective
original production defect when tested against the pre-fix source.
Shutdown/lease tests and six extracted production exit-tail cases pass. Real
loopback HTTP tests preserve existing update behavior and validate maximum-width
logger telemetry as complete JSON. No external device or service is accessed.

VitaSDK compilation passes for `xv_log.c`, `xv_ui_gxm.c`, `xv_remote.c`, `main.c`
and `xd3d.c`. The build uses the checked-in shader layout header (`make -o
shaders/xv_layouts.h`) and performs no asset generation or full package build.
The default ARM logger object has no `log_queue`/`log_writer` symbols and 32,792
BSS bytes; the enabled object has 164,312 BSS bytes, in addition to its kernel
thread stack. Existing unrelated main/header warnings remain. No hardware,
Vita3K, deployment or authoritative source/stage mutation was performed.

## Review and physical acceptance

This isolated branch starts at `1ad76da027552d2611dbf1a984564af1a23d71f5`.
Independent review of the initial prototype `75ef5d6` found three blockers that
its original 28 scenarios missed: console status misinterpreted as a byte count,
deadline subtraction using two clock reads, and missing frame-test API stubs.
The follow-up corrects all three and expands the coverage described above.
The later admission-toggle change is limited to logger APIs, report-mode
selection, tests and this document; benchmark/main/remote integration is separate.
The focused `runtime/main.c` integration patch is provided separately for root
integration; runtime benchmark branches are untouched. Keep both build and
runtime opt-ins disabled until a matched physical comparison establishes value.

A useful trial measures tagged report-frame tails, frame p95/p99/p99.9/max,
queue/backpressure frequency, writer scheduling cost and average FPS separately,
with identical scene, camera, graphics and object-worker policy. Include enough
reports to encounter rare stalls. Disappearance of one 137 ms outlier is not a
tail-latency result. Formatting, unrelated directory probes/map I/O, storage
errors and background CPU contention remain outside any claimed improvement.
