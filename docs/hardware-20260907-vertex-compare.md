# Vertex comparator hardware result — September 7, 2026

The new byte comparator reduces the measured stream-preparation cost, but the
whole-game improvement is small: **7.241 FPS on versus 7.073 FPS pooled off**,
about **0.17 FPS / 2.36%** in this comparison. That is consistent with the user
not noticing a practical improvement. It is not progress to playable 20 FPS yet,
and drift prevents attributing the full frame-time change to the comparator.

## Verified run

Read-only USB archive:
`/home/birchwoodgod/xita-backups/2026-09-07-214226-vertex-compare-hardware/`.
The collection copied and hash-verified 26 files / 45,327,535 bytes. The installed
30,891,526-byte executable is the expected vertex-comparison build, SHA-256
`4663b0253e116f697fda40df3437d5b05c6510915fd6d958933de1c4a39808d1`.
The new log is 548,612 bytes, last modified 21:39:40 CDT, SHA-256
`0d13a5f883ef34604ff73735eee9a0046e7ba883e2d3ae3543d1adaaa178a940`.
Both the scalar/NEON comparison and resolution comparison completed.

The saved configuration remains byte-identical: 544p, 256 textures, automatic
filtering, mip smoothing on, high effects, CPU 444 MHz and 20 FPS cap. Vertex
residency stays off; normal rendering uses single-flight recovery and contains
no ordinary Finish calls. The CPU comparator returns to enabled after its test.

## Same-view scalar/NEON/scalar comparison

Each phase settles for 60 frames, then measures 120 frames. Position
`41.7798 -83.3009 2.3235` and forward `0.52593 -0.85056 -0.00026` match;
all phases report `view-ok 1`.

| Metric | Scalar before | NEON | Scalar after |
| --- | ---: | ---: | ---: |
| FPS | 7.003 | 7.241 | 7.145 |
| Stream preparation | 6.755 ms | 5.599 ms | 6.704 ms |
| Texture preparation | 4.271 ms | 3.004 ms | 1.973 ms |
| Render submission | 5.663 ms | 5.641 ms | 5.615 ms |
| Display queue call | 0.033 ms | 0.032 ms | 0.033 ms |
| Visibility-query waiting / guest frame | 22.746 ms | 22.823 ms | 22.468 ms |
| Completion latency | 65.008 ms | 63.883 ms | 64.592 ms |

Exact phase durations are 17,134,599 / 16,573,425 / 16,795,031 microseconds.
Pooled off time is 141.373 ms/frame, versus 138.112 ms on: 3.262 ms difference.
The two scalar phases differ by about 2%. Supporting metrics average the two
complete 60-frame reports inside each measured phase. Their report boundaries
are not identical to the exact FPS interval.

Stream preparation drops by **1.131 ms / 16.8%** against the scalar mean and
returns when the comparator switches off. Copied bytes stay at 28,052 KiB per
60 frames. Compared bytes are approximately 31,177 / 31,177 / 31,206 KiB, with
about 235 GXM calls/frame and small animated-work differences. This supports a
local CPU benefit. Texture preparation improves throughout the run, including
the final scalar phase; that drift cannot be credited to NEON comparisons.
Query waiting and render submission are essentially unchanged.

## Resolution comparison and next bottleneck

The subsequent 544/360/544 comparison remains at the same validated camera.

| Metric | 544p before | 360p | 544p after |
| --- | ---: | ---: | ---: |
| FPS | 7.255 | 8.660 | 7.219 |
| Stream preparation | 5.567 ms | 5.572 ms | 5.568 ms |
| Texture preparation | 1.970 ms | 1.974 ms | 1.977 ms |
| Render submission | 5.629 ms | 5.717 ms | 5.624 ms |
| Visibility-query waiting / guest frame | 22.977 ms | 6.134 ms | 23.768 ms |
| Completion latency | 64.227 ms | 40.715 ms | 64.453 ms |

360p increases throughput by 19.66% relative to pooled 544p, while the CPU
preparation costs stay almost unchanged. This is evidence of GPU pressure and
its query dependency affecting the critical path. **It is not a pure GPU
bottleneck:** even 360p takes about 115.48 ms/frame, far above the 50 ms needed
for 20 FPS. Query waits and completion latency overlap other work and each
other; do not add them as exclusive costs or subtract them to claim CPU time.
The test restores 544p and leaves the saved settings intact.

The next hardware candidate should target the existing visibility-query wait:
retain exact flare-result inputs and perform independent work before consuming
the result, while guarding every actual consumer/reuse point. That implementation
already passed native/host/private checks. Keep the NEON comparator enabled,
retain current shaders and single-flight GPU submission, and compare only the
additional scheduling change. A substantial translated-CPU workload remains even
if this scheduling change helps; it is not promised to reach 20 FPS by itself.

## Stability limits

No new or changed crash dump appears in this collection. The complete render
windows log zero ordinary Finish calls, vertex-upload failures, draw-storage
drops or fence errors. Maximum pending depth stays one, upload high water is
534/8,192 KiB and the maximum observed GXM draw count is 400. No requested
geometry ownership capture was recorded in this run. These log checks do not
prove the previous driving crash resolved, nor certify every rendered scene.

Raw logs, the file manifest, differences from the previous collection and phase
metrics are archived. No additional repeat of the completed vertex comparison
is needed. The separate cutout comparison has passed native and host checks,
but its private game validation was paused to collect this new hardware result
and prioritize the query-wait candidate.
