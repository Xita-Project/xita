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

## Perf265 package

Private full Vita build completed successfully for `0.2.0-perf.265 / 4c701e91`.
The candidate preserves the current perf264 configuration and optional shader
trial, isolating the new observation code. Package member comparison confirms
only `game-a.self` and `boot-game.txt` changed; the shared update contract remains
`775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897`.

- Runtime: 34,809,974 bytes, SHA256
  `6536a0ec5a68f060194023d5f8d4f42168d855e9b1701815b1739ca30581eb18`.
- VPK: `../slot-stall-candidate/xita-perf265c.vpk`, SHA256
  `0f407d716d8a1caa4cfe2a99a53e01c7eecbaf2996e63db8513c0b1817792b01`.

Build and package receipts are preserved in that private directory. Existing
compiler warnings remain; no new compilation error occurred. No performance
improvement is claimed for this diagnostic.

Updater subsequently verified the exact runtime hash and confirmed boot in slot
0. Perf264 remains in slot 1. The ordinary a30 launch sequence was started with
the preserved `a30-perf211` namespace and queue timing enabled; launch/gameplay
qualification is still pending. Keep-awake was renewed by the launch client.

## Perf265 capture results

The existing launch and both collectors completed normally. `slot-stall-analysis.json`
contains seven valid records from loading/cutscene/early gameplay through the
ordinary outdoor run. Every ticket has `ticket % 60 == 3`:

| Ticket | Total wait ms | Until retirement ms | After retirement ms |
| --- | ---: | ---: | ---: |
| 3783 | 424.068 | 190.552 | 233.516 |
| 4383 | 300.601 | 293.217 | 7.384 |
| 4443 | 305.028 | 300.904 | 4.124 |
| 4623 | 225.949 | 223.060 | 2.889 |
| 4683 | 292.335 | 291.941 | 0.394 |
| 4983 | 323.992 | 323.608 | 0.384 |
| 5043 | 167.953 | 167.894 | 0.059 |

The six later records point toward delayed retirement rather than delayed return
from the guest sleep. The first also contains substantial post-retirement delay.
Nearby queue-gate reports show substantial time before first pump inspection,
including a window with 964495 us there and zero in inspected-head gates.
This is not evidence that GPU execution itself consumed the whole wait.

Code inspection supplies a concrete suspect: the pump's every-60-retire report
in `xv_pump_retire` calls ordinary `XV_LOG` outside a report scope. `xv_log_write`
therefore calls `log_write_immediate`, including file writes and a sink mutex,
on the GXM pump thread. The main gameplay report already uses the asynchronous
report writer, but that does not automatically cover other producer threads.
The periodic alignment is strong evidence to investigate this path, not direct
measurement of time inside the logger. Preserve this distinction.

Ordinary CPU Present interval samples (360p, same active shader trial):

| Segment | Samples | Mean ms / FPS | p95 ms | Max ms |
| --- | ---: | ---: | ---: | ---: |
| Lifepod | 720 | 56.278 / 17.77 | 73.407 | 173.632 |
| Firing | 68 | 78.614 / 12.72 | 105.154 | 133.465 |
| Moving outside | 74 | 72.489 / 13.80 | 101.435 | 255.216 |
| Outdoor | 875 | 51.614 / 19.37 | 60.504 | 104.614 |

Outdoor endpoint screenshot confirms the expected scene. No 200+ ms outdoor
hitch occurred in this short sample; this does not establish a fix or sustained
20 FPS. No controls remain held. Collector sessions are terminal.

Next implementation: independent bounded asynchronous periodic pump reports,
without stealing the existing guest report builder or changing GPU ownership.
Retain error/critical reporting and explicit queue/full/failure behavior; test
concurrent producers and flush/shutdown guarantees before deploying. Do not
simply wrap the pump with the existing single-owner report scope: a collision
could push the much larger guest report back onto synchronous file I/O.
