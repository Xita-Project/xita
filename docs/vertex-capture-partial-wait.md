# Vertex capture queue pressure

The perf223 outdoor capture recorded 234 queue-only pressure drains over 60
frames. Previously, filling the 32-job FIFO called the full drain, waiting for
all submitted jobs and retiring reuse results even when snapshot memory had
space. Counts alone do not establish time lost or a recoverable FPS gain.

`XV_CAPTURE_PARTIAL_WAIT=1` is an opt-in candidate. On queue-only pressure with
enough arena space for a full cache miss, it waits until at least one job
completes and collects completed callbacks in order. Submission can then reuse
an available FIFO slot without emptying the remaining work. It retains CPU
snapshots and current GPU results, and rechecks reuse identities after callbacks.
The default remains zero.

Arena pressure, frame/slot changes, fallback preparation and shutdown retain
full drains. No GPU fence, upload ticket, draw ordering or slot retirement is
removed. The existing acquire/release completion protocol and notification
handshake apply; finite wait timeouts preserve progress if notification fails.
The `[vertex-capture-partial]` report counts queue-slot waits separately from
full drains. More partial waits need not mean more elapsed waiting.

The production FIFO/uploader fixture passes 24 host ASan/UBSan configurations
covering packed layout, compact staging, reuse, persistent storage, ready-result
reuse, and notifications. Queue/arena pressure runs with the option both off
and on. A deterministic notification test parks the worker after the first of
32 jobs completes: submission 33 returns while the other jobs remain pending,
without reclaiming snapshots. All 33 outputs and callbacks are then checked.
The combined-feature fixture also passes on the Raspberry Pi's ARM cores 0/1,
using the existing GCC 13 NEON intrinsic compatibility header.

The test run exposed two pre-existing fixture/build mismatches: reuse-disabled
builds lacked a no-op capture-census macro, and the sparse test still expected
CPU snapshots not to be reused. The latter now checks reused snapshot storage
and referenced vertex contents; deliberately uncopied sparse holes are not
asserted to contain particular bytes.

Hardware frame-time and prolonged active-play qualification remain pending.
