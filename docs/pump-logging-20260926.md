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
