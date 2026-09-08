# Completion/matrix hardware benchmark — September 6

The first controlled resolution test on this build completed on the Vita:
**7.389 FPS at 544p → 9.211 FPS at 360p → 7.451 FPS at 544p**.
The camera signature matched throughout, and the benchmark restored 544p.
Resolution alone improves throughput by **24.1%**, about **1.79 FPS**, compared
with the pooled 544p baseline. The two 544p measurements differ by only 0.84%.
This verifies a pixel-workload contribution, while leaving substantial work
before the 20 FPS target.

## Collection and configuration

Read-only USB collection at 17:10–17:11 CDT copied and hash-verified 65 files to
`/home/birchwoodgod/xita-backups/2026-09-06-171009-completion-matrix-hardware/`.
The executable is 32,918,474 bytes, SHA-256
`3364033427206d324264c42446bb0a66cd752e273342c53fc13db60ffa3d8fc0`,
matching the [installed completion/matrix build](completion-matrix-20260906.md).
The two collected screenshots are unchanged from before deployment and do not
document this play session. No device files were changed; the Vita is safely
unmounted.

The saved configuration remains 544p, textures 256, automatic filtering, mip
smoothing On, materials/glow/particles High, original decal lifetime/budget,
20 FPS cap and extended compression Off. CPU 500 MHz is rejected again with
`802B0000`; the effective clocks are CPU 444, bus/GPU 222 and XBAR 166 MHz.
The benchmark changes only runtime resolution.

## Resolution results

Each phase excludes 60 settling frames and measures the following 120 frames.
FPS and frame time below come from those exact measurement intervals.

| Phase | Resolution | Elapsed for 120 frames | FPS | Mean frame time |
| --- | --- | ---: | ---: | ---: |
| Before | 960×544 | 16.240706 s | 7.389 | 135.34 ms |
| Reduced | 640×360 | 13.028429 s | 9.211 | 108.57 ms |
| After | 960×544 | 16.104666 s | 7.451 | 134.21 ms |

The pooled 544p baseline is 7.4199 FPS / 134.77 ms. Reducing pixel count by
55.88% saves 26.20 ms per frame in this view. All phases report the same camera:
position `105.0051 -162.1989 0.6408`, forward `-0.89769 0.44068 0.00000`.
The supporting reports all show 229 draws/frame and no texture decodes.

Supporting figures below are means of the two 60-frame reports emitted within
each measured phase, except CPU utilization, which uses nearby sample medians.
Their boundaries differ from the exact benchmark intervals. Settling frames,
unchanged camera and draw counts make them useful supporting evidence, but they
are not exact per-phase CPU/GPU attribution.

| Supporting metric | 544p before | 360p | 544p after |
| --- | ---: | ---: | ---: |
| Engine interval | 134.45 ms | 107.00 ms | 132.85 ms |
| Present handoff/wait | 1.15 ms | 1.20 ms | 1.50 ms |
| Render submission | 6.18 ms | 6.29 ms | 6.11 ms |
| Final graphics-completion wait | 66.36 ms | 38.59 ms | 66.96 ms |
| Render pump total | 72.66 ms | 45.00 ms | 73.19 ms |
| Draw preparation | 12.31 ms | 12.15 ms | 9.98 ms |
| Elapsed query wait per guest frame | 28.74 ms | 8.96 ms | 30.03 ms |
| Query completion-to-resume delay | 95 µs | 97 µs | 92 µs |
| Nearby C0 / C1 / C2 busy | 3 / 8 / 76% | 4 / 12.5 / 83.5% | 4 / 8 / 75% |

Render submission hardly changes, while graphics completion and query waits
fall substantially at 360p. This is evidence that GPU workload contributes to
the frame rate. The remaining 108.57 ms frame interval and higher core-2 busy
percentage at 360p also support continued work on CPU preparation/translation.
These measurements do not establish a purely CPU-bound or GPU-bound frame.

Engine, render and query intervals overlap: do not add them or interpret their
differences as exact CPU execution time. Finish is elapsed CPU-side waiting,
not an exclusive GPU execution counter. Simulation/effects are not frozen;
draw-preparation texture handling changes during the test despite zero decodes.
The returning 544p result helps bound drift, but this is still one camera view.

## Native math and completion notifications

Across 29 Blood Gulch world reports:

- Matrix: **3,215,075 native / 55,424 fallback (98.305% native)**.
- Native layouts: 1,309,330 disjoint, 1,152,310 output-over-left and 753,435
  output-over-right. Every matrix fallback is a cross-page span; alignment,
  scratch overlap and partial overlap counters are zero.
- Quaternion: **1,855,387 native / 17,263 fallback (99.078% native)**.

The [preceding hardware run](hardware-20260906-cpu-preparation.md) used native
arithmetic for only 39.5% of matrix calls. The new in-place support is exercised
on hardware. Different views and draw counts prevent claiming an overall FPS
improvement from the math change alone.

Across 28 query reports bracketed by world reports, all **1,634 completion
waits** have a ready result on return. Mean completion-to-resume delay is
**95.93 µs**, maximum **636 µs**. Median pending reads are 0.983 per guest frame;
there are no fallback polling sleeps in the reports emitted within the three
measured phases. Remaining elapsed query wait therefore mostly precedes result
publication, rather than being a delayed wake after completion. The game still
must wait for the real GPU result.

Whole-session median FPS is 7.4, with 29 world reporting windows ranging from
5.9 to 9.3. This mixes the resolution test, loading/transition work and play,
and is less useful than the controlled phase measurements.

## Next performance work

1. Optimize the remaining CPU draw path and audit the largest translated
   routines. `D3DDevice_DrawIndexedVertices` accounts for 7.79% of printed
   world-bracketed samples; guest routine `0xB71C0` accounts for 5.86% and
   reaches 7.3% in the reduced-resolution phase. Classify its input/output and
   alias behavior before implementing a replacement.
2. Trace the callers behind `NtWaitForSingleObjectEx` and `NtYieldExecution`
   (10.33% and 7.78% of printed world sample shares) to separate required waits
   from avoidable handoffs. These are elapsed-time samples, not pure CPU work.
3. Continue reducing measured GPU pass/fragment work. The resolution test
   establishes a real benefit, but even 360p remains far short of 20 FPS.
   Preserve GPU completion and draw-data ownership while optimizing.

All 45 complete render reports have matching timing sums and frame counts,
60 final Finish/display-queue calls and zero intermediate target Finish calls.
No explicit render-target errors or workload-table overflows were recorded.
Geometry diagnostics were off; the old screenshots cannot establish current
visual correctness. Raw logs, manifest, collection metadata, general analysis
and `benchmark-analysis.json` are preserved in the archive.
