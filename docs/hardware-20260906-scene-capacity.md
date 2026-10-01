# Render-target scene capacity: hardware results

The September 6 Blood Gulch follow-up records 38 active world reporting windows
(2,280 frames), with a median 9.65 FPS and a 5.3–16.1 FPS range. No window reaches
20 FPS. Settings remain 848x480, textures 128, point filtering and mip smoothing
off; clocks remain CPU 444, bus/GPU 222 and XBAR 166 MHz.

| Median per world window | Previous build | Four-scene build |
| --- | ---: | ---: |
| FPS | 9.75 | 9.65 |
| Draws per frame | 125 | 129.5 |
| Submission, including API stalls | 5.176 ms | 3.796 ms |
| EndScene (subset of submission) | 2.381 ms | 0.606 ms |
| Final graphics-completion wait | 58.337 ms | 58.999 ms |
| Whole measured render pump | 66.357 ms | 62.741 ms |

EndScene's largest 60-frame-window average is 1.123 ms/frame, versus 25.930 ms
in the previous Blood Gulch session. This is consistent with the scene-capacity
change reducing submission stalls. The routes differ, so it is not a controlled
performance comparison. The whole-game FPS result does not show a clear gain.
These are CPU-side elapsed API timings, not direct GPU execution measurements;
CPU work, draw preparation and render-pump time overlap.

All eight allocations accepted four scenes, with no fallback. Display driver
memory is 516,096 bytes (baseline 184,320), scaled target 352,256 (143,360),
small offscreen targets 221,184 (86,016), and the 640x480 offscreen target
352,256 (143,360). These are per-allocation measurements, not simultaneous
resident-memory totals. Backbuffer scene endings reach ten in one frame;
capacity four is not a hard scene-count limit.

All 58 complete frame/submission/target reports have matching counts and rounded
timing sums, retain 60 final Finish and display-queue calls, and have zero
intermediate target Finish calls. Seventy specialized shader loads report no
discard instruction; no explicit target errors or workload-table overflows
appear. Geometry-capacity logging was off; logs alone do not establish visual
correctness. Nearby core medians are 8/22/58 percent. Visibility-result polling
remains the largest printed guest-profile share (18.17 percent mean across 21
world-bracketed samples); those shares include waiting.

Read-only USB collection was made at 12:58 CDT into:
`/home/birchwoodgod/xita-backups/2026-09-06-125841-scene-capacity-hardware/`.
All 65 copied files were hash-verified, including logs, saves, settings and the
installed executable. Its SHA-256 matches the tested four-scene candidate:
`a8c34f3dff71e6ba5e8738eed55186008ce5089c6d6b9b8ff11586015b906840`.
The two screenshots are unchanged from the prior collection. No device files
were changed, and the card was safely unmounted. The archive includes parsed
windows, per-target attribution, allocation sizes and the copy manifest.

The next changes should reduce actual rendering work and memory bandwidth while
preserving gameplay timing. Extra resolution and effect-quality options provide
controlled comparisons. The user authorized implementing these settings and the
proposed CPU/GPU optimizations after this collection.
