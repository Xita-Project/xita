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

## First campaign checkpoint capture

The first collector mistakenly accepted main-menu `loaded/active` flags; that
capture is excluded from campaign results. The replacement required the known
checkpoint camera plus worker-query activity. Its screenshot shows the saved
campaign scene, marine, weapon and HUD. After a further 150 seconds without
input, the final six 60-frame windows average 78.10 ms (about 12.80 FPS), versus
perf.32's 78.30 ms. This does not establish a performance gain from diagnostics.

Across those 360 frames, 658 sampled submissions produced:

| Scope | Sampled calls | Logical bytes | Elapsed µs | µs/call |
| --- | ---: | ---: | ---: | ---: |
| Exact comparison | 337 | 1,937,848 | 10,609 | 31.48 |
| Staging copy | 424 | 3,125,464 | 12,092 | 28.52 |
| Queue publication | 371 | — | 4,759 | 12.83 |

Both comparison and copying contribute; these totals alone do not explain the
previous heavy-scene capture cost. They are periodic elapsed samples, not cycle
measurements or bandwidth counters. Explicit joins remain outside these scopes.
The ordinary corridor movement sequence subsequently completed and its settled
capture is running. Private evidence: `capture-detail-hardware/checkpoint-settled`
`.log`, `.summary.json`, and `.detail.json`; `checkpoint-first.png`.

## Crowded corridor result and next experiment

The settled final 360 frames average 171.53 ms (about 5.83 FPS), with 22.57
selected model calls/frame and camera (-27.53, 37.08, 0.62), direction
(-0.86, -0.48, -0.15). Different model workload/camera from perf.32 prevent a
causal frame-time comparison. The final six detail windows contain 1,862 sampled
submissions: comparison 978 calls / 6,758,472 logical bytes / 58,683 us;
copy 1,097 calls / 6,840,232 bytes / 29,493 us; publication 1,000 calls /
13,399 us. Compared logical volumes are similar but sampled comparison elapsed
is roughly twice copying. Preemption and memory access patterns remain possible
contributors; this does not prove a cache-miss diagnosis.

The next controlled candidate keeps immutable input capture and worker-side
uploader validation but disables recording-side capture reuse and completed
result bypass (`XV_VERTEX_CAPTURE_REUSE=0`, `XV_VERTEX_CAPTURE_READY=0`). It uses
existing qualified fallback paths, not unvalidated guest-pointer sharing. All
other cumulative options remain enabled. This deliberately trades additional
staging copies and FIFO jobs for removing exact owner comparisons. Worker time,
queue-pressure joins and overall gameplay time must determine whether the trade
helps; no speedup is assumed. Perf.33 is retained as the comparison/rollback.

## perf.34 worker-validation comparison installed

The existing no-owner-reuse configuration compiled and passed package checks;
only runtime and boot digest changed. The remote updater verified runtime SHA256
`34562e86d4cfd35f76156be9b2b89eaf4726747a844821d33c4c2f279f90baa0`
(32,193,318 bytes), restarted into slot 0 and confirmed boot. Runtime status is
`0.2.0-perf.34 / d797ca8`. Perf.33 remains in slot 1. Ordinary menu navigation,
checkpoint confirmation, settled capture and guarded corridor replay are running
serially under `capture-copy-hardware/run-gameplay.py`; any failed stage stops
the sequence. Performance and rendering qualification of this configuration
remain pending those captures. This is a comparison candidate, not a demonstrated
optimization or a change to the ordinary build defaults.

## perf.34 settled checkpoint result

The ordinary saved checkpoint loaded and rendered. Its final six 60-frame
windows average 78.30 ms versus perf.33's 78.10 ms, with the same logged camera
and slightly different selected-model counts (7.55 versus 7.80/frame). No FPS
benefit is established. Removing owner comparisons changes the measured work:

| Per frame, nested/overlapping scopes | perf.33 | perf.34 |
| --- | ---: | ---: |
| Owner capture elapsed | 5.274 ms | 4.225 ms |
| Worker preparation elapsed | 4.778 ms | 6.942 ms |
| Explicit capture join elapsed | 0.079 ms | 0.046 ms |
| FIFO jobs | 62.94 | 113.21 |
| Captured staging data | 446.96 KiB | 845.75 KiB |

The owner reduction is accompanied by increased worker work and copying. This
supports the expected tradeoff, not a net win. Upload failures are zero in both
sets of six windows. The heavy corridor capture is still running; default
selection should not change based on this checkpoint alone. Evidence lives in
`capture-copy-hardware/checkpoint-settled.log`, its summaries, and
`checkpoint-comparison.json`.

The packet completion bounds span more than one frame at this checkpoint
(roughly 118–125 ms from submission versus 78 ms between frames). They include
pipeline/queue latency and must not be interpreted as exclusive per-frame GPU
service time or added to CPU phases. This observation does not establish which
stage sets steady-state throughput.
