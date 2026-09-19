# Sampled vertex-capture cost attribution

`XV_VERTEX_CAPTURE_DETAIL=1` enables startup-only profiling in the capture owner.
The default is Off. One in 64 valid submissions reaching the queue samples:

- Exact source comparison, including compact-prefix comparisons.
- New staging copies, including compact packing.
- Queue publication and worker notification.

`[vertex-capture-detail]` reports actual sampled calls, logical bytes and elapsed
microseconds. It resets counters at the usual drained reporting boundary and
keeps the sampling sequence across reports. Shutdown resets startup settings and
the sampling sequence. Ready draws can contribute comparison samples without
copies or queue publications. Invalid/fallback submissions before queue admission
are not sampled. Persistent-cache internals are outside these scopes.

These are nested samples, not full-frame totals. Do not add them to the existing
capture timer or multiply by 64 and claim exact time savings: periodic sampling
can correlate with draw order, elapsed time includes preemption, and timer reads
have overhead. Compare per-call/per-byte costs and multiple gameplay windows.
Logical bytes do not measure memory bus traffic or cache misses. Key lookup,
metadata/mask writes, collection and callbacks are not separately measured.

The existing FIFO test matrix exercises the production implementation, with a
new 65-submission case checking two samples, exact staged bytes, publication
counts, reporting reset, startup disable and unchanged GPU output. Hardware
measurements remain pending; this change itself is not a performance gain.

Validation: all 24 FIFO configurations passed ASan/UBSan with the sampling case;
the Vita SDK compiler also compiled the production file with compact capture,
exact reuse, completed-result bypass and notification suppression enabled under
`-Wall -Wextra -Werror`. Private receipts are
`/tmp/xita-capture-detail-asan.log` and `/tmp/xita-capture-detail.o`.

Diagnostic builds may set `XV_VERTEX_CAPTURE_DETAIL_DEFAULT=1`. The Makefile
validates 0/1 and tracks this capture-object-only define with a config stamp.
An explicit startup environment value still overrides the build default.

## perf.33 hardware installation

The diagnostic build retains perf.32's cumulative options and enables only the
new sampling default. Package verification found only `game-a.self` and
`boot-game.txt` changed; launcher, shaders and update contract are identical.
Runtime SHA256:
`c4a5d8bb78beb5e64a3dd2975dddc56683d6efcfaf8b244d12d7cde72c1df2e3`
(32,199,670 bytes). The remote updater verified the digest, restarted into slot 1,
and confirmed boot. Status reports `0.2.0-perf.33 / 2396277`; perf.32 remains in
slot 0. A dashboard screenshot confirms Halo CE selected and Launch Game ready.
The ordinary campaign launch sequence has been started. Installation proves
neither gameplay success nor performance. Captures remain under the private
`capture-detail-hardware/` directory in the unified workspace.
