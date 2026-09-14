# Object-transform hardware follow-up — September 13, 2026

The native object-basis experiment now executes on hardware. The installed
EBOOT is unchanged from the [preceding capture](hardware-20260913-gameplay.md):
`311b01a424ccf6483372189ca6bc95ff308b9de95d222c7bde0513ef63000e9e`.
Only `XV_NATIVE_OBJECT_BASIS=1` was enabled; model-palette batching remains off.
Resolution, graphics options, vertex worker, triple buffering and phase timing
are unchanged. The clock API again reports CPU 444 MHz after the 500 MHz request
fails. This is a functionality/coverage result, not a matched speedup measurement.

## Capture

The read-only USB collection contains 12 files / 36,720,081 bytes, with checked
sizes and SHA-256 hashes and no read errors. USB was safely unmounted. No new
September 13 screenshot or crash dump was found in the inspected folders.
The log ends with a complete phase report, without a shutdown record; absence
of a new crash dump does not certify stability or visual correctness.

Private evidence is in
`/home/birchwoodgod/xita-backups/2026-09-13-202654-object-basis-results`:
`collection.json`, `analysis.json`, `window-records.json` and `analyze_run.py`.
The configuration-change backup/receipt is in the adjacent
`2026-09-13-195507-object-basis-test` directory.

There are 212 complete, usable phase windows / 12,720 frames, with no dropped,
invalid or incomplete phase records. Across all windows, the object-basis helper
records **2,068,663 accepted calls**, zero mirrored cases and zero disabled,
floating-point or layout rejections. This confirms execution of the native path,
not equivalence to the original during this particular play session. Mirroring
is covered by prior differential tests but was not exercised in this capture.

## Frame times and selection limits

Exclude menu/loading and the mixed first BSP window ending at frame 1021.
The remaining 195 windows / 11,700 frames aggregate to **14.65 FPS**, with
60-frame reports ranging from 9.6 to 16.6 FPS. Most of that interval has an
unchanged camera observation and should not be described as moving-play FPS.

| Selection | Frames | Aggregate FPS |
| --- | ---: | ---: |
| First repeated camera, end frames 1081–1801 | 780 | 16.14 |
| Long repeated camera, end frames 2161–12661 | 10,560 | 14.68 |
| Remaining six windows | 360 | 11.51 |

Camera classification uses rounded position/direction samples reported every
60 frames. It cannot prove that the player remained still throughout a window.
The final repeated view is position `74.11 -126.45 1.58`, direction
`0.83 -0.53 -0.17`, which differs from the earlier benchmark view. The six-window
selection is small and includes transitions; it is not a controlled comparison
with the earlier 11.61 FPS moving-play selection. No off/on/off benchmark ran.

All post-loading acquisition reports record zero busy-slot waits. Median
system-wide C0/C1/C2 busy values are 4/11/91%. In the six changing-view/transition
windows, scene inclusive active time is 43.000 ms/frame, pose selected self time
6.827 ms/frame and object helper `0x8D760` selected self time 6.120 ms/frame.
These overlap parent scopes and include native blocking/preemption. CPU scene
and object preparation remain substantial; this run does not isolate the
object-basis helper's saved time.

## Next measurement

The [native-math comparison](native-math-benchmark-20260913.md) adds an automatic
off/on/off test for either native object-basis preparation or model-palette
batching. It changes one helper in the same session, retains the other settings,
checks camera consistency and restores the configured default. This replaces
attempts to infer a small gain from different manual routes. First test the
object-basis path, then measure model-palette batching separately.
