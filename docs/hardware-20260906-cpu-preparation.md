# CPU preparation: hardware follow-up

Read-only USB collection on September 6 at 15:46 CDT copied and hash-verified
65 files into
`/home/birchwoodgod/xita-backups/2026-09-06-154631-cpu-preparation-hardware/`.
The installed executable's SHA-256 is
`a762b09c502660697900bd9ca38a32a7d26bab7fa1266282614ccbb756cdbb84`,
matching the [CPU preparation build](cpu-preparation-20260906.md). Native math,
shader identity caching, frame events and bounded visibility backoff are active.
The two September 6 screenshots are unchanged from the preceding collection.
Collection made no device writes; the Vita is safely unmounted.

## Comparable settings, different scenes

The current run uses **960x544**, textures 256, automatic filtering, mip
smoothing On, materials/glow/particles High, original decal lifetime/budget,
20 FPS cap and extended compression Off. The runtime confirms these settings
after Launch Game. CPU 500 MHz is again rejected (`802B0000`); effective clocks
are CPU 444, bus/GPU 222 and XBAR 166 MHz.

The immediately preceding run, now `xita.1.log`, uses those same settings. Its
SHA-256 matches the log backed up before the CPU build was installed, when the
graphics-menu executable (`51d1cc86…`) was present. This is a more relevant
settings comparison than the earlier 360p/Low session.

It is still **not a controlled route comparison**. The current run has 84
Blood Gulch world reporting windows (5,040 frames), versus 30 previously.
Seventy-seven current windows have identical logged camera position/direction
snapshots, so a fixed view dominates this recording. Excluding that repeated
snapshot leaves seven windows with median 7.0 FPS and range 5.6–11.2. The
previous run has a different camera snapshot in every world window. Current
median draws per frame are also higher, 155 versus 101.

## Measured behavior

Values below are medians of 60-frame world windows, not instantaneous extrema
or an average of a standardized route.

| Metric | Previous 544p run | CPU preparation 544p run |
| --- | ---: | ---: |
| FPS | 8.85 | 6.9 |
| Reporting-window FPS range | 4.6–18.6 | 5.6–11.2 |
| Windows at least 20 FPS | 0 | 0 |
| Engine interval between Presents | 112.0 ms | 143.0 ms |
| Present handoff/wait | 1.2 ms | 1.6 ms |
| Draws per frame | 101 | 155 |
| Draw HLE | 8.15 ms | 6.9 ms |
| Draw preparation, sum of measured stages | 8.724 ms | 6.982 ms |
| Shader-state preparation per draw | 19.156 µs | 8.194 µs |
| Render submission, including API stalls | 3.583 ms | 4.687 ms |
| EndScene, subset of submission | 0.544 ms | 0.807 ms |
| Final graphics-completion wait | 62.523 ms | 98.475 ms |
| Measured render pump | 65.298 ms | 103.252 ms |
| Nearby system-wide C0 / C1 / C2 busy | 8 / 21.5 / 59.5% | 5 / 17 / 49% |

Shader-state preparation is about 57% cheaper per draw in these samples,
consistent with the new state reuse. Different shader/draw mixes prevent
isolating the optimization's causal saving. The runs establish neither an
overall speedup nor an overall regression. Sustained 20 FPS remains unmet.

Engine intervals include translated execution, HLE, draw recording and waits.
Render work overlaps the engine; do not add their times. The 98.475 ms Finish
value measures CPU-side waiting for graphics completion, not exclusive GPU
execution. Lower core utilization does not by itself establish faster gameplay.

## Math, cache and remaining waits

Across the 84 current world windows:

- Matrix: **4,050,195 native / 6,196,311 fallback**, or **39.5% native**.
- Quaternion: **5,851,534 native / 80,017 fallback**, or **98.7% native**.
- Shader identities: **356,831 lookups**, **238,874 reuses** and **117,957
  computed identities**, or **66.9% reuse**.

Matrix fallbacks remain common. The current counter does not distinguish
overlap, alignment or page-boundary guards, so it cannot establish which guard
dominates. Measure the reasons before extending native support; retaining the
original function protects numerical and alias behavior.

There are 83 visibility windows bracketed by world reports. Median pending
reads are 48.77 per guest frame, versus 54.27 previously. The new post-wait
check completes about 0.98 queries per frame within the same HLE call.
Requested cooperative delay is 46.29 ms/frame, accumulated across retries;
this is not measured blocked time and must not be added to graphics wait.

The sampled visibility HLE share rises from 20.37% to 36.74%; quaternion's
printed share falls from 2.80% to 1.07%, and matrix is 4.82% versus 4.45%.
These are wall-time attribution samples, include waiting and omit entries below
the printed cutoff. A large visibility share must not be described as CPU math
or spin time alone. Delayed query publication and graphics completion remain
important to investigate together.

## Next work and limits

1. Distinguish visibility's actually blocked time from runnable work and GPU
   completion latency. Preserve current-generation results and frame ownership.
2. Classify matrix fallbacks, then implement only the common layouts whose
   original operation and alias behavior can be verified.
3. Use the same build and view for a resolution-only 544p/360p comparison,
   keeping effects and other settings fixed. This can test how much of the
   remaining wait responds to pixel workload without conflating a new CPU build
   or a different route.

All 127 complete render reports have matching frame counts and timing sums,
60 final Finish/display-queue calls, and zero intermediate target Finish calls.
All 50 loaded alpha-disabled specializations report no discard instruction.
No explicit render-target errors or workload-table overflows appear. Geometry
diagnostics were off, and the old screenshots do not validate current rendering.
The archive contains raw logs, `manifest.json`, `collection.json`, per-window
`analysis.json`, the preceding run and `comparison.json`.
