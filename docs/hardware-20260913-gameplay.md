# Blood Gulch gameplay and worker comparison — September 13, 2026

The installed gameplay executable matches the archived [bulk memory clear
candidate](string-fill-20260913.md): EBOOT SHA-256
`311b01a424ccf6483372189ca6bc95ff308b9de95d222c7bde0513ef63000e9e`
(31,125,690 bytes). It includes the constant-preparation changes and optional
object-basis/model-palette helpers. The latter two helpers were **disabled by
runtime configuration**. This run does not establish a cross-build speedup or
stable 20 FPS.

## Collection and settings

Twelve files totaling 36,917,185 bytes were copied from a read-only USB mount,
with size and SHA-256 checks. Collection had no read errors; the Vita was then
unmounted. No device settings or executables were changed. Private evidence:

`/home/birchwoodgod/xita-backups/2026-09-13-193811-gameplay-results`

`collection.json` records file hashes and the shallow device inventory.
`analysis.json`, `phase-windows.json` and `analyze_run.py` preserve the analysis.
The current `xita.log` contains 67 complete phase windows / 4,020 frames, with
zero dropped, invalid or incomplete phase records. The preceding launch in
`xita.1.log` ends during UI-map loading. No new September 13 crash dump or
screenshot was found in the inspected folders. Neither log has a clean shutdown
record; this collection does not certify crash freedom or rendering correctness.

Startup confirms 640×360 rendering, triple buffering on, vertex worker on,
128-pixel texture limit, Low model detail and a 20 FPS cap. Saved material/effect
options are unchanged. Phase timing is enabled.

The CPU request is 500 MHz, but the API reports failure `802B0000` followed by
a 444 MHz fallback. Logged game clocks are CPU 444, bus 222, GPU 222 and crossbar
166 MHz. September 12 captures report the same fallback; this is not a new clock
regression. Requested settings must not be labeled as measured clocks.

## Matched vertex-worker result

`xita.log` lines 5036–5797 contain one complete off/on/off comparison. Each phase
settles for 60 frames and measures 120; all three report `view-ok 1` at position
`65.1590 -124.3560 1.4181`, forward `0.22562 -0.97153 0.07256`.

| Vertex worker | FPS | Mean frame time |
| --- | ---: | ---: |
| Off before | 15.898 | 62.899 ms |
| On | 16.222 | 61.644 ms |
| Off after | 15.718 | 63.622 ms |

Pooling the equal-length off phases gives 63.261 ms/frame. Worker-on saves
**1.617 ms/frame: 2.56% less time or 2.62% more FPS**. This is one bracketed
trial; animation and simulation continue with the same camera. It supports a
small benefit, not a precise universal gain. The September 12 comparison used
a different view, more draws and triple buffering off; its 11.627 FPS is not
this build's before result.

## Gameplay and bottlenecks

Moving Blood Gulch play uses ten 60-frame windows ending at frames 1981–2521,
after the first BSP window that mixes loading and play. Camera positions and
directions vary. Rounded frame-time reports span **9.4–17.0 FPS**; 600 frames
divided by summed durations gives approximately **11.61 FPS**. The separate
phase-window clock gives 11.64 FPS. Menu/loading frames are excluded.

Sixteen post-benchmark windows end at frames 3121–4021, with the camera remaining
at the benchmark view to logged precision. They span 15.7–16.6 FPS and aggregate
to approximately 16.23 FPS from frame-time reports. Do not mix this largely
stationary interval with moving play to claim typical FPS.

| Phase measurement | Moving play, mean ms/frame | After benchmark, mean ms/frame |
| --- | ---: | ---: |
| Scene `0x5D410`, inclusive active | 41.054 | 30.180 |
| Object update `0x900E0`, inclusive active | 30.513 | 22.133 |
| Main loop, inclusive active | 82.075 | 60.926 |
| Main loop, inclusive parked | 3.871 | 0.723 |
| Scene `0x5D410`, selected self | 18.804 | 14.792 |
| Render helper `0x5B760`, selected self | 8.518 | 6.123 |
| Pose helper `0x8DDF0`, selected self | 7.061 | 5.107 |
| Object helper `0x8D760`, selected self | 6.395 | 4.129 |
| Matrix multiply `0xB5B40`, selected self | 3.775 | 2.746 |
| Quaternion conversion `0xB5F60`, selected self | 1.657 | 1.216 |

Inclusive parents contain child costs; do not add all rows. Selected self still
includes uninstrumented descendants. Active is scheduled elapsed time, including
native blocking and host preemption, rather than CPU cycles. The finer scope set
prevents direct comparisons of self values with the older, coarser trace. Object
updates execute about 2.59 times per rendered frame during moving play.

Supporting draw-preparation categories total 11.045 ms/frame in moving play:
streams 2.955, state 2.101, textures 2.055 and program selection 1.649 ms are the
largest components. These overlap scene time. System-wide median C0/C1/C2 busy
values are 4/10/87% during moving play and 4/8/95% after the benchmark.

All 35 supporting gameplay/benchmark-context acquisition reports record **zero
busy-slot waits**, with asynchronous retirement enabled. Moving-play visibility
reports also record zero waits. GPU notification latency averages 45.867 ms in
moving play and 35.814 ms after the benchmark. It overlaps CPU work and includes
observation delay; it is neither additional blocking time nor direct GPU
execution/utilization. These results favor CPU scene/object preparation as the
next target over further buffer-retirement changes.

Phase-report formatting/writing averages 0.241 ms/frame in moving play, versus
2.916 ms in the earlier capture. The reduction is encouraging, but scenes and
scope sets differ. This counter excludes timer-hook overhead; final FPS
comparisons should disable phase timing in every comparison arm.

## Next step

The configuration omits `XV_NATIVE_OBJECT_BASIS=1` and
`XV_NATIVE_MODEL_PALETTE=1`. Every captured report records zero accepted work
and disabled declines for these two experiments. Existing native per-matrix and
quaternion helpers are active. The later palette-size histogram is not included
in this installed executable.

First measure the already validated experiments separately against an unchanged
baseline at the same view and settings. Their switches are read once per process,
so changes require restarting the game. Confirm accepted counters before judging
performance and keep instrumentation settings equal between comparison arms.

Then prioritize the remaining scene helper and pose/object preparation for a
substantial worker batch. Hierarchical poses consume parent transforms and the
object loops mutate shared state, so whole-loop parallel execution still needs
an ownership redesign. Snapshot independent inputs, retain ordered publication
and a serial fallback, and measure dispatch/join time before enabling offload.
This capture identifies target regions, not a completed additional worker.
