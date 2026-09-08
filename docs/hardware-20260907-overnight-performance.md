# Overnight optimization hardware results — September 7

The installed overnight candidate completes the Blood Gulch resolution test at
**7.939 / 9.897 / 8.194 FPS** for 544p / 360p / 544p. All three phases retain the
same camera and the benchmark restores 544p. All four new optimization paths
execute on hardware. **Sustained 20 FPS remains unmet.**

These readings are higher than the preceding build's 7.389 / 9.211 / 7.451 FPS,
but they are not a controlled build-to-build comparison. This session uses a
different camera and approximately 199 draws/frame, versus 229 previously.
The pooled 544p readings are 8.064 versus 7.420 FPS; the apparent 8.7% difference
cannot be attributed to the code changes.

## Collection

Read-only USB collection copied and hash-verified 63 files, including the logs,
configuration, executable and saves, into:
`/home/birchwoodgod/xita-backups/2026-09-07-075707-overnight-performance-hardware`.
The executable SHA-256 is
`1712a90f11ba1718e04ccf91ce05c7eea08dde465cf65fb6289aec162395ab91`,
matching the [validated overnight candidate](overnight-performance-20260907.md).
No device files were changed. USB storage is safely unmounted.

No September 7 screenshots were present. The logs establish performance and
execution of the new paths, rather than visual correctness of this session.

The saved graphics configuration is unchanged from the preceding benchmark:
544p, textures 256, automatic filtering, mip smoothing On, High material/glow/
particles and a 20 FPS cap. The saved CPU request is now 444 MHz instead of
500 MHz; both sessions actually run at CPU 444, bus/GPU 222 and XBAR 166 MHz.
The earlier 500 MHz request was rejected.

## Measured resolution phases

Each phase settles for 60 frames and measures the following 120 frames.

| Phase | Resolution | FPS | Mean frame time |
| --- | --- | ---: | ---: |
| Before | 960×544 | 7.939 | 125.96 ms |
| Reduced | 640×360 | 9.897 | 101.04 ms |
| After | 960×544 | 8.194 | 122.05 ms |

Reducing resolution saves 22.96 ms/frame against the pooled 544p result and
increases throughput by 22.7%. The two 544p results differ by 3.2%. Position is
`39.9709 -88.5342 0.7142`, forward `0.77305 -0.63438 0.00067` throughout.

Supporting reports emitted inside the measured phases have different boundaries
from the exact benchmark intervals. They show:

| Supporting metric | 544p before | 360p | 544p after |
| --- | ---: | ---: | ---: |
| Render submission | 4.90 ms | 4.98 ms | 4.78 ms |
| Final graphics-completion wait | 56.95 ms | 34.40 ms | 56.25 ms |
| Draw preparation | 10.83 ms | 10.76 ms | 8.52 ms |
| Query wait per guest frame | 26.02 ms | 9.65 ms | 26.23 ms |
| Query completion-to-resume | 90 µs | 99 µs | 98 µs |
| Nearby C0 / C1 / C2 median busy | 3 / 19 / 62% | 4 / 17.5 / 78.5% | 3.5 / 10 / 72% |

Engine, render and query intervals overlap; they must not be added. Graphics
completion is elapsed CPU-side waiting, not an exclusive GPU execution timer.
The resolution response supports a GPU workload contribution. The remaining
101 ms at 360p supports continued investigation of CPU preparation/translation
and synchronization as well. The target budget for 20 FPS is 50 ms.

## Which changes ran

- Native clipping executes 629 calls/frame in each phase's supporting reports;
  the full session records 695,787 calls. The clipper still appears prominently
  in elapsed sampling, including 9.6% in the 360p profile window. This does not
  measure its speedup against the previous implementation.
- Sampler setup reuses approximately 68% of lookups in the benchmark. Live
  texture validation remains enabled. No textures are decoded in the measured
  phases; reducing decode cost or enlarging its cache cannot explain a gain
  within these intervals.
- Same-thread scheduling avoids handoffs in 4,551 of 33,675 reported scheduling calls
  (13.5%). In world-bracketed reports it avoids 2,880 of 21,510 (13.4%). These
  counters measure avoided operations, not saved milliseconds.
- The depth-only replacement handles two draws/frame, approximately 1% of
  reported draws in this view. Its coverage here is narrow; draw count alone
  cannot establish the GPU-time share. The full session records 2,692 such draws.
- Existing native matrix and quaternion coverage remains 98.1% and 99.1% in
  world reports.

All 47 complete render reports have matching timing sums and frame counts,
60 final Finish/display-queue calls, and no intermediate target Finish calls.
No explicit render-target/shader failure or workload-table overflow was found.
Geometry diagnostics were off, so this does not certify geometry correctness.

World reports span 7.2–13.2 FPS, with an 8.45 FPS median, mixing the benchmark,
stationary time, loading transition and movement. After benchmark completion,
the reported range is 8.1–13.2 FPS; it is not a fixed-route gameplay comparison.

## Next priorities

1. Add a reproducible scene/camera or an in-build optimization A/B before
   claiming a build speedup. The existing test controls resolution within a
   session, but does not reproduce the scene across sessions.
2. Continue investigating the clipper and indexed-draw preparation. The 360p
   profile window places these at 9.6% and 10.1% of elapsed samples. More of the
   measured work must be removed or safely shared to approach the 50 ms budget.
3. Measure and optimize the common color-writing GPU passes. The new depth-only
   path covers few draws in this view; the highest printed index counts remain
   under shader keys `154066FD`, `A01D09CF` and `8ED40330`. Index counts are not
   GPU timings, so measure pass cost before selecting replacements.
4. Keep completion waits correct. World-bracketed reports show 1,531 waits,
   1,528 with a ready timestamp, approximately 105 µs mean completion-to-resume,
   and a 9.66 ms maximum. Three waits lack a ready timestamp in a later gameplay
   window, consistent with the bounded wait expiring. There are 264 fallback
   polling sleeps requesting 218.6 ms total across those world reports. These
   warrant tracking, but do not explain the roughly 50 ms/frame gap by themselves.

Raw logs, hashes, collection metadata, general analysis, benchmark analysis and
optimization counts are retained in the archive. No executable or settings were
changed during this follow-up.
