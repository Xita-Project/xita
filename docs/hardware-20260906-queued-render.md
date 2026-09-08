# Queued render-pass hardware results, September 6 at 09:46 CDT

The queued candidate runs on hardware, but sustained 20 FPS is not achieved.
This Blood Gulch run has a higher median than the previous diagnostic run;
different routes and scene contents prevent attributing that difference to the
code change. Removing intermediate Finish calls moves substantial waiting into
submission and final completion. It does not remove the underlying GPU work.

The read-only USB collection is
`/home/birchwoodgod/xita-backups/2026-09-06-094659-queued-render-hardware/`.
All 65 copied files were hash-verified. The executable matches
`8c2d56a9d96796ef3676b860586a9c7083bbb14ace30b6a889bbf5c63d4f1006`.
Configuration is byte-identical to the previous diagnostic run: 480p, textures
128, mip smoothing off, filtering 1, rear touch off. Logged clocks remain
444/222/222/166 MHz. There are no new screenshots. The card was safely unmounted;
no device files were changed during this collection.

## Comparison

The new 531,437-byte log contains 45 matched 60-frame timing windows, including
29 Blood Gulch world windows (1,740 frames), and ends at process time 310.564 s.
The previous run has 30 world windows. Both builds contain the same guest
instrumentation; the only executable change is render-target scene scheduling.
Values below are medians of world windows, except where labeled otherwise.
Medians of individual stages must not be summed to reconstruct the total.

| Measurement | Previous waits | Queued passes |
| --- | ---: | ---: |
| FPS | 6.5 | 8.0 |
| FPS range | 4.9–17.5 | 4.3–16.4 |
| Guest frame interval | 152.45 ms | 124.30 ms |
| Draws per frame | 168.5 | 162 |
| Indexed draw HLE | 9.70 ms | 9.20 ms |
| Submission, including internal API stalls | 3.742 ms | 11.134 ms |
| Between-target completion waits | 85.205 ms | 0 ms |
| Final completion wait | 2.672 ms | 60.865 ms |
| Whole measured pump | 91.340 ms | 81.993 ms |
| Nearby sampled C0 / C1 / C2 utilization | 13.5 / 21 / 73% | 17 / 23 / 72% |

The two fastest new windows average 16.4 and 16.3 FPS with the camera pointing
straight down, 76 and 73 draws per frame, and about 42 ms in the pump. The
previous ground-view window averaged 17.5 FPS with only 49 draws per frame.
Even these views are not identical workloads. Camera direction is sampled at
the end of a window, not every frame.

Grouping by draw count gives mixed results: 151–200-draw windows have median
6.7 FPS versus 5.7 before, while windows above 200 draws have median 5.9 versus
6.4 before. Draw counts do not account for triangles, screen coverage, shaders
or pass order. These comparisons are descriptive, not proof of a causal gain
or regression. None of the new world windows averages 20 FPS.

## Findings and limits

The log confirms `XV_RT_QUEUE=1`. Every measured window has zero between-target
Finish calls and exactly 60 final Finish calls and display-queue submissions.
All stage sums agree within printed rounding. No explicit RT BeginScene,
EndScene or invalid-target errors appear. This confirms the intended path ran;
it does not prove the absence of visual artifacts or geometry drops. Capacity
logging was not enabled on the device, and there are no new screenshots.

Submission rises as high as 37.384 ms in a slow window, while final completion
still waits 68.265 ms. Internal GXM resource stalls are a possibility; the
aggregate timer cannot identify which call or resource causes the elapsed time.
Target allocation still uses `scenesPerFrame=1`.

Twenty function-profile tables are bracketed by world windows. Mean printed
shares include visibility-result polling at 16.18% (peak 40.8%), indexed draw
HLE at 6.45%, guest BSP traversal `0x88B80` at 5.20%, and Present at 1.97%.
These are sampled wall-time shares, including waits and preemption. Functions
omitted from a top-40 table contribute zero to their means; these are not
exclusive CPU costs. Texture decoding has a median equivalent of 0.003 ms per
frame across the world windows, so it is not a leading measured steady-gameplay
cost in this run.

## Next performance work

1. Separate scene acquisition and uniform-buffer reservation from draw replay
   inside submission. Use this evidence to decide whether a bounded increase in
   scene capacity could reduce internal stalls at an acceptable memory cost.
2. Attribute the remaining GPU rendering work to passes and shader workloads.
   The whole pump still exceeds the 50 ms budget in most world windows; scene
   queueing alone is insufficient. Preserve alpha textures, depth and effects
   while testing changes to their actual rendering cost.
3. Investigate visibility-result polling without fabricating results or
   releasing storage early. Measure CPU busy-wait reduction separately from
   actual frame-time improvement.

The proposed repeatable benchmark route remains useful: uncontrolled play gives
bottleneck evidence but cannot settle small performance differences. Keep the
installed candidate available for controlled comparisons, then check useful
improvements in an ordinary build with instrumentation removed.

The archive contains `analysis.json`, `checks.json` and the analyzer for this
run. See [candidate implementation and validation](queued-render-passes-20260906.md)
and [the previous hardware diagnostic](performance-diagnostic-20260906.md).
