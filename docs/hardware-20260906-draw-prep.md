# September 6 draw-preparation hardware results

Collected over read-only USB into
`/home/birchwoodgod/xita-backups/2026-09-06-080746-draw-prep-hardware/`.
The 60-file backup includes the installed executable, configuration, logs,
saves and two new screenshots. No device files were changed.

Installed executable SHA-256:
`7067df1e3daff612718c04339b6e45c7ad07541f39c7a24ade9fe3e1156e20c3`.
This matches the combined candidate installed at 07:55 CDT. The settings remain
480p, textures 128, mip smoothing off, filtering 1 and rear touch off. Logged
clocks are CPU 444, bus 222, GPU 222 and crossbar 166 MHz.

The user confirms that rendering looks very good on hardware, with little
perceived performance improvement. The new screenshots show Warthog driving
and a shotgun/sky view with overlays around 7–8 FPS.

## Recorded performance

The log has 97 draw-preparation windows. The 66 windows with BSP draws belong
to Blood Gulch; the later a10 open never reaches a logged world-rendering
window and is excluded. Each window represents 60 game Presents.

| Measurement | Median across Blood Gulch windows |
| --- | ---: |
| Logged FPS | 8.1 (range 5.0–12.1) |
| Game-side frame interval | 118.35 ms |
| Logged present wait | 1.90 ms |
| Render-pump elapsed time | 79.05 ms |
| Indexed draw adapter elapsed time | 9.45 ms |
| Draws per frame | 172.5 |
| Index preparation phase | 1.13 ms |
| Draw state phase | 2.65 ms |
| Draw texture phase | 1.97 ms |

The nearby preceding CPU samples have medians C0 16%, C1 14%, C2 78%.
These are sampled observations, not time-weighted measurements of these exact
windows. Parsed rows and summary are preserved as `draw-prep-rows.json` and
`draw-prep-summary.json` in the backup.

These measurements establish a current baseline, not a controlled comparison
with earlier runs: viewpoints and gameplay differ. Game-side, draw-adapter
and pump timings overlap. Pump elapsed time includes submissions, internal
GPU completion waits and display queuing; it is not a measurement of GPU
execution time. Subtracting these medians does not produce exclusive engine
CPU time. The individual phase medians also need not sum to the median total.

## Next measurement

Index copying is a small part of the current frame interval. Further tuning it
alone cannot recover the roughly 68 ms separating the median game-side
interval from a 50 ms / 20 FPS budget. We need attribution for the remaining
guest work and a split of pump submission, completion waits and display waits.
The current normal executable has no guest-function trace instrumentation;
merely setting `XV_PROF=1` cannot name its uninstrumented guest work. An
instrumented diagnostic build must be compared separately with ordinary builds
because instrumentation itself adds overhead.

A proposed **Benchmark** action in the Xita dashboard would make subsequent
optimization comparisons useful. It is not implemented yet. The initial design:

1. Record the executable identity, map, settings and hardware clocks with results.
2. Reset the same Blood Gulch scenario for each run, separating map loading and
   shader/texture warm-up from steady gameplay measurements.
3. Use a repeatable camera path, then separate firing and vehicle sections.
   Preserve simulation behavior; repeatability must not depend on changing the
   guest clock or forcing a different gameplay tick rate.
4. Record frame-time distributions, throughput and slowest sections alongside
   per-core usage, guest-function samples, draw preparation, render submission,
   completion waits, texture work and worker joins. Count game Presents and
   completed presentations separately. Do not add overlapping thread timings.
5. Buffer benchmark measurements and write the report after the measured
   section, with a fixed diagnostic configuration across compared builds.
6. Repeat the same scenario on hardware before and after each optimization;
   retain an ordinary-build check to measure instrumentation overhead.

A standalone CPU/memory/graphics microbenchmark can answer narrower questions,
such as whether a worker or buffer strategy is faster on Vita. The Halo
benchmark supplies the evidence that such a change improves the actual game.
Stable 20 FPS means consistently meeting approximately 50 ms per displayed
frame; an average alone can conceal the driving and firing stalls.

The AI firing report remains open. This Blood Gulch session does not establish
the cause of campaign enemies failing to shoot after the pistol handoff.
