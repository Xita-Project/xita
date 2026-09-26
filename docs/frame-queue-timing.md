# Frame publication-to-submission timing

`XV_FRAME_QUEUE_TIMING=1` enables one additional process-time read per published
frame. It defaults off and is read on the first present, so changing it requires
a restart. The timestamp travels in the existing packet, before the release
store to `g_frame_requested`; the pump reads it after the corresponding acquire.
Packet reuse still requires the existing retirement/slot ownership protocol.

The pump uses its existing submission-start timestamp and reports total and
maximum publication-to-submission delay, plus the count above 1 ms, every 60
valid packets. All aggregates belong to the pump. No waits, priorities, fences,
resource lifetimes, draw ordering or presentation policy change.

This interval includes scheduling, display availability and frame pacing. It is
not GPU execution time. Compare it with existing submission, notification and
frame-acquire reports to identify where backlog begins. A small interval rules
against a large pre-submission queue delay for that sample; a large interval
requires separating scheduling, display pressure and intentional frame caps.
It does not by itself prove that the delay can be removed.

The extended `[frame-queue-gates]` report partitions publication-to-submission
into publication-to-first-inspection and first-inspection-to-submission. Their
sums equal the reported queue interval. It counts packets that observed an
unavailable back buffer, a full display queue or frame pacing; these counts can
overlap. Both elapsed intervals include preemption. In particular, time before
first inspection may include the pump finishing an older packet, and time after
inspection is not an exact measure of display blocking.

Perf233 lifepod: 1,200 packets averaged 32.789 ms before submission (maximum
394.645 ms, 1,109 above 1 ms). Nearby 1,200 Present intervals averaged 77.595 ms,
p95 87.556 ms, p99 116.710 ms. These overlapping intervals do not establish a
recoverable 32.789 ms saving. The extended gate report is not yet deployed.
This is a diagnostic, not a speedup.


Perf234 settled lifepod (1,200 packets): 1.537 ms before first inspection and
31.739 ms after inspection; 1,086 packets encountered both unavailable back
buffer and full display queue, none encountered frame pacing. Nearby Present
intervals averaged 76.967 ms (12.993 FPS), p95 85.078 ms, p99 118.508 ms.
These are overlapping intervals, not additive frame costs.

Next diagnostic: `XV_DISPLAY_CALLBACK_TIMING=1` (default off, restart required)
measures callback setup/capture and vblank elapsed time with three clock reads
per callback. Aggregates are callback-owned. After at least 60 tracked frames, a single-slot
release/acquire mailbox hands them to the pump, which performs the report. A full
mailbox causes the callback to retain and extend its aggregate; it never waits.
Consumers must use the reported frame count rather than assume exactly 60. It does not move the vblank wait or release.
Times include callback preemption; the periodic report itself is excluded and
may add overhead. This distinguishes slow callback execution from waiting for
a callback to become eligible/scheduled; it does not separately prove GPU time.
Vita object compilation passed; hardware deployment pending.


Perf235 with direct callback logging stalled during early startup, before any
callback report was captured. The companion remained reachable; restarting the
same binary with callback timing disabled restored remote responsiveness.
This does not prove the exact stall cause. Direct callback logging was removed
because the shared logger may block on a mutex/file output. The replacement
mailbox passed host ASan/UBSan and Pi ARM tests of empty/full behavior and one
million ordered transfers of 64-bit counters on two threads. Hardware validation
of the mailbox path is pending.
