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

Hardware measurement pending. This is a diagnostic, not a speedup.
