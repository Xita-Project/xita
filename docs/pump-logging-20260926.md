# Render pump asynchronous reports — 2026-09-26

Status: perf266 installed and boot-confirmed; gameplay effect remains unverified.

Perf265 recorded seven long slot waits on tickets congruent to 3 modulo 60.
Six mostly preceded retirement. Periodic pump reports currently issue console
and file writes synchronously; this is a plausible contributor, not yet a
measured causal explanation.

The pump now has an independent 32 KiB report builder. Retirement, display
callback summaries and queue summaries enqueue immutable chunks to the existing
writer. The guest report keeps its own builder and metadata. Critical messages
remain immediate. Queue admission pins writer lifetime; shutdown cannot discard
an admitted report. FIFO sequence numbers, rather than report IDs, determine
flush completion. Report IDs can complete out of order across producers.

This is lossless asynchronous logging, not an unconditional nonblocking path:
a full queue still applies backpressure, and persistent sink failure requires
explicit retry. Failed auxiliary admission retains existing immediate logging.
CPU-usage reporting remains unchanged.

Validation: the full native writer suite passed normal, ASan/UBSan and TSan
(including independent producers, chunk ownership, saturation, retry and
shutdown). Existing log batching and benchmark fixtures passed. The production
handoff test had a stale single-game marker; it now extracts the current update
tail and checks both CE and Halo 2 arguments and sync failure behavior under
ASan/UBSan. No Halo 2 runtime behavior was changed.

Next: compile perf266 with existing settings, verify update contract, deploy
with rollback retained, and collect ordinary a30 gameplay. Report frame-time
and long-slot-wait distributions separately. No performance gain claimed yet.

## Deployment

perf266 / 58e024d7 built successfully. The six new auxiliary concurrency cases
also passed on the Raspberry Pi (ARM Linux pthread harness, core 0 only).
Package comparison against perf265 changed only game-a.self and boot-game.txt;
update contract remained unchanged. Remote updater verified 34,811,074 bytes,
SHA-256 `995bbaea012cb7c95fa514cfc31e1c961eee15d97987bf61d7f238eee7fa699e`,
installed slot 1 and confirmed boot. Dashboard independently reported perf266.
Perf265 remains in slot 0. Awake lease renewed after restart.

Private build, package, test and deployment receipts: ../pump-log-candidate/.
No gameplay timing result for this build yet.

## First ordinary gameplay capture

The a30 save loaded successfully; screenshots confirm the lifepod and outdoor
terrain/trees/weapon were present. At 360p with the existing optional shader trial:

| Segment | Frames | Mean ms / FPS | p95 ms | Maximum ms | >100 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lifepod | 720 | 55.799 / 17.92 | 72.440 | 87.841 | 0 |
| AR firing | 72 | 73.188 / 13.66 | 97.641 | 116.841 | 3 |
| Movement | 76 | 70.048 / 14.28 | 86.209 | 123.579 | 1 |
| Outside | 844 | 53.444 / 18.71 | 64.097 | 114.453 | 2 |

These are CPU Present intervals in host-bracketed ordinary gameplay, not an
isolated GPU benchmark. No sampled frame exceeded 200 ms. Captured startup and
gameplay logs contain zero slot-stall receipts (>100 ms), versus seven in the
previous perf265 capture. This is encouraging single-run evidence for the
periodic-stall hypothesis, not proof of permanent elimination or a controlled
average-FPS gain. Pod mean is nearly unchanged; outdoor mean is slightly worse.
Firing remains well above the 50 ms target.

The final writer reports error 0, one queued chunk, 45 cumulative capacity waits
(153.959 ms total); the wait count did not increase between the idle and final
captures. Lossless queue backpressure still exists. Outside, scene helper CPU
is about 46 ms/frame, with under 1 ms scene waiting and 0.58–0.85 ms deferred
recording drain waits in the last windows. Preparation/computation remains the
next target; this run does not support prioritizing wholesale GPU wait removal.

Receipts: idle-summary.json, gameplay-summary.json, stall-summary.json, input
marks, full logs and screenshots in the private candidate directory. No crash
observed during this short sequence. AI, complete cutscene, 15-minute play and
all rendering/save requirements remain unverified. Goal remains unmet.
