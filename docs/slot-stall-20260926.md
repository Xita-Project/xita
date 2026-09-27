# Perf264 long-frame investigation

The 861.078 ms outdoor frame 8643 has an existing `[present-stall]` receipt:
813 ms inside `xv_present`, with all other named present stages below 1 ms.
The corresponding `[frame-acquire]` window records two busy-slot waits totaling
829084 us. Nearby CPU reporting spans 1630.9 ms and reports reduced busy time on
all three cores. A later yield-storm snapshot includes guest sleeps overdue by
99 and 403 ms, followed by a 519 ms scene-helper observation.

This narrows the largest hitch to slot acquisition, but does not tell whether
GPU retirement was late or the guest resumed late. The scheduler snapshot is
nearby evidence, not proof of the cause of frame 8643. No texture decode was
reported in that frame window; the shader change cannot be blamed or credited
from these observations.

## Bounded diagnostic added

When existing `XV_FRAME_QUEUE_TIMING` is enabled, the pump records a monotonic
timestamp immediately before publishing packet completion. The recording owner
reports slot waits above 100 ms, capped at 32 reports per process. Each receipt
splits total wait into elapsed time until the last required packet retirement
and elapsed time from that retirement to return to recording.

This does not change sleeping, wakeups, ownership, queue limits or GPU waits.
It adds one pump timestamp per retired packet when queue timing is on. No new
logging occurs for ordinary waits. GPU completion and CPU retirement remain
different events; time before retirement includes submission, scheduling and
completion callbacks, not just GPU service.

The owner reads metadata only after its existing completion acquire. It is the
sole publisher and has not published another packet while waiting. Required
next-slot owners are fewer than four tickets old, so their entries cannot have
been reused in the four-entry packet ring. Invalid ages or timestamps produce
`valid 0`, never fabricated timing or a changed storage-release decision.

An extracted production-block ASan/UBSan fixture passed retirement/resume splits,
32-bit ticket wrap, impossible ages, missing/future timestamps, and retirement
between the completion check and the initial wait timestamp. Private fixture:
`../material-nocolor-candidate/slot-stall-test.c`.

Not yet built into a Vita executable or deployed. Perf264 remains installed.
Next: build the diagnostic, retain normal gameplay settings, and capture a long
wait to choose between pump/retirement work and guest scheduling. Do not change
synchronization policy on the strength of aggregated GPU latency alone.
