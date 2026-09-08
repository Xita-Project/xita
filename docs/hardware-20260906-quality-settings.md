# Lower graphics settings: hardware results

Read-only USB collection at 14:20 CDT on September 6 copied and hash-verified
65 files, including logs, settings, saves and the installed executable, into
`/home/birchwoodgod/xita-backups/2026-09-06-142027-quality-settings-hardware/`.
The installed SHA-256 is
`bd91e725ebee1b2d4c781c39bdb2bef7223aeb5381536e1a5bf07212cf591414`,
matching the quality-options build installed at 14:00. The two screenshot files
are unchanged from the previous collection. Collection made no device changes.

## Settings actually used

The initial boot reads the old 480p configuration, then the dashboard reloads
the user's changes before Launch Game. The runtime confirms a **640x360**
render target, textures 128, point filtering, mip smoothing Off, materials Low,
glow Low, particles Low, decal lifetime 5 seconds, decal budget 32, frame cap 20
and extended compression On. Blood Gulch's tag hook changes 120 materials,
22 flares, 14 cosmetic particle types and 35 decal lifetimes.

CPU 500 MHz was requested but rejected with `802B0000`; the fallback and
reported effective clock are **444 MHz**. Bus/GPU remain 222 MHz and XBAR
166 MHz. Selecting 500 did not produce an overclock on this device.

## Measurements and limits

There are 19 Blood Gulch world reporting windows (1,140 frames), all with the
active flag set. The logged FPS range is 5.3–12.9; none reaches 20 FPS.
These are 60-frame reporting-window averages, not instantaneous minima/maxima.

| Median per world reporting window | Previous 480p session | Current 360p / Low session |
| --- | ---: | ---: |
| FPS | 9.65 | 8.5 |
| Engine interval between Presents | 101.5 ms | 115.8 ms |
| Present handoff/wait | 1.4 ms | 1.2 ms |
| Draws per frame | 129.5 | 155 |
| Draw HLE | 6.6 ms | 8.1 ms |
| Render submission, including API stalls | 3.796 ms | 4.458 ms |
| Final graphics-completion wait | 58.999 ms | 43.613 ms |
| Measured render pump | 62.741 ms | 49.480 ms |

The routes, scene complexity, camera directions and settings differ. These
sessions do **not** establish that the new build is slower, nor a causal gain
from any individual option. They support the user's observation that the lower
settings have not delivered a clear overall FPS improvement. Graphics wait is
about 15.4 ms lower despite more draws, consistent with reduced rendering cost.

The engine interval includes translated game logic, HLE, draw recording and
any waits inside that work; it is not a direct measurement of CPU execution.
Render-pump work overlaps it and must not be added to it. Likewise, a Finish
wait is elapsed CPU-side waiting, not a GPU execution counter. Core medians
in world-bracketed samples are **7 / 24 / 71 percent** (system-wide).

The final slow windows reach 5.3–5.9 FPS with 166.8–186.1 ms engine intervals
and roughly 60–69 ms pump intervals. This supports investigating the engine
and draw-generation path alongside the GPU rather than reducing effects alone.
The 20 FPS limit cannot raise low frame rates; it only spaces early frames.

## Next optimization targets

Eleven profile windows are bracketed by world reports. Mean printed sample
shares include visibility-result polling (7.42%, down from 18.17% previously),
indexed-draw HLE (7.08%), guest `0xB5B40` (4.83%), `0xB71C0` (3.97%),
simple render-state HLE (3.65%), BSP traversal `0x88B80` (3.26%) and
`0xB5F60` (2.96%). These are sampled shares, include waits, and omit entries
below the log's printed cutoff; they are not exclusive stage timings.

The subsequent [draw preparation, native math and waits implementation](cpu-preparation-20260906.md)
addresses the first two targets below. Its hardware performance is not yet
measured; keep this run as the preceding observation, not a controlled baseline.

1. Measure and reduce translated engine/draw-generation work, starting with
   the matrix helpers already audited (`0xB5B40`, `0xB5F60`) and the
   indexed-draw/state paths. Verify numeric behavior before replacing math.
2. Investigate remaining visibility polling without breaking lens-flare/query
   correctness. Median pending reads remain about 21 per guest frame; all
   pending reads take the existing delayed path.
3. Use a repeatable camera/route for per-pass GPU and CPU comparisons. Keep
   resolution and quality changes separate when attributing improvements.

All 35 complete render reports have matching frame counts/timing sums, 60 final
Finish/display-queue calls and zero intermediate target Finish calls. No explicit
render-target errors or workload-table overflows appear. Geometry diagnostics
were off, so logs do not establish visual correctness. The archive contains
`manifest.json` and `analysis.json` with the parsed windows and profiles.
