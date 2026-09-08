# September 5: Keyes and combat hardware run

The user reached Captain Keyes and shot several Grunts, then reported that resume
still does not work. The Vita was connected over USB. Logs, all profile files,
the cache checkpoint and the five latest screenshots were copied to
`/home/birchwoodgod/xita-backups/2026-09-05-073237-keyes-hardware/`.
The installed executable and original saves were left intact.

## Build and visual reports

The installed executable SHA-256 is
`12d858bddf8390a63c2610b2082dc16b1fc7963e830c48b9c3b24e20e430e2aa`,
matching `xita-20260905-loading-profile.vpk`. All three loading shaders are
installed and match that package. Guest-function tracing and the 1 kHz sampler
started automatically. The user has confirmed the loading artwork and baked
lighting on hardware.

Geometry spikes remain hardware-only according to the user. Turning on the
flashlight makes walls and models disappear. The five copied screenshots show
the cryo area, bridge and armed gameplay, but do not form a controlled
flashlight-off/on comparison. This run's current `xita.log` has no draw histograms;
the `xboxvita.log` files are historical and must not be attributed to this run.
Bloom is a suspected area, not an established cause.

## Performance

`analysis.json` in the backup records the selection and raw timing/profile rows.
Selecting 60-Present intervals with at least 10 BSP draws per frame gives:

- 91 intervals, 5,460 Presents over approximately 748.3 seconds.
- Logged rates of **4.2–11.6 fps**, aggregate Present throughput **7.3 fps**.
- Texture decoding averaged **5.6 ms per Present** across these intervals.

The selection includes transitions and changing scenes. This is an instrumented
run, not a fixed-route benchmark or an independent displayed-frame count. The
sampler measures wall time, and game, draw-HLE and pump timers overlap.

For a shortlist, select sampling tables whose immediately preceding and following
timing rows both have at least 10 BSP draws. There are 62 such tables. The following
percentages average their printed top-40 entries; omitted entries contribute zero,
so these are lower bounds on the corresponding sample shares, not exact CPU costs.

| Sampling slot | Symbol / next source to inspect | Mean printed share |
| --- | --- | ---: |
| H1842D0 | D3DDevice_DrawIndexedVertices | 11.4% |
| H185670 | D3DDevice_Present, including waits | 6.0% |
| 88B80 | Guest function in `recomp/code_013.c` | 4.6% |
| 54010 | Guest function in `recomp/code_009.c` | 3.8% |
| 53E90 | Guest function in `recomp/code_009.c` | 3.4% |
| 5C300 | Guest function in `recomp/code_010.c` | 3.1% |

There is no single dominant function in this shortlist. Start with the confirmed
texture-cache waste, then measure the next hardware run before choosing further
draw or guest-code optimizations. A final performance claim requires an ordinary
build without guest-function instrumentation. The requirement remains sustained
25 fps or more on hardware; the separately reported 22 fps in Blood Gulch remains
a user observation for that map.

## Resume evidence

The active profile checkpoint is
`save/udata/UDATA/4d530004/122A17771B9E/savegame.bin`. It and the cache checkpoint
are both 3,428,352 bytes. The profile file's first 16,384 bytes are zero; the cache
file retains its header, including `levels\\a10\\a10` and `01.10.12.2276`.
All bytes after offset 16,384 match between the two files. The cache copy alone
does not establish that this checkpoint can be restored.

- Profile checkpoint SHA-256:
  `aa2105f3a04f5aa72aa07a1fd4be0566777f180df3f9c5a206b7a568a93dd0e8`.
- Cache checkpoint SHA-256:
  `dd0a6937d963f98197ae6f4fd8cc536ce20eed6d8ec3c9d541213e43fe194755`.

This matches the earlier isolated Save and Quit reproduction: a full checkpoint
write and a 332-byte header write were followed by a 16 KB zero write. Signing
alone did not prevent that failure. Trace the header invalidation path locally;
do not overwrite the user's profile with the cache or request another long
checkpoint test while performance and rendering impede play.

## Texture-cache correction

`xita-20260905-texture-cache.vpk` corrects cache validation and streamed-file
invalidation to use the mip actually decoded, rather than level 0. Unchanged
large textures previously failed each recheck and were decoded again.

- VPK: 13,206,188 bytes; SHA-256
  `926cd2be9e6fd327be4d2513a4dfede71c6e46499c519588b146c4017578f153`.
- Executable SHA-256:
  `b42abccfd1da7266751ef62deb6f0641d0b23bd8d7727402241ccfb2d2b39784`.
- Host tests exercise unchanged/changed textures, selected and skipped mip writes,
  dynamic textures, DXT1/3/5, RGBA, luminance and cube textures. The old recheck
  fails the unchanged-upload assertion; the correction passes, including ASan/UBSan.
- Native build and isolated Vita3K cryo tutorial passed. Comparable emulator
  intervals went from 44–46 decodes per 60 Presents to mostly zero, with occasional
  real updates. These were not identical input replays; Vita FPS impact is pending.
- Every packaged asset other than the executable is byte-identical to the
  hardware-verified loading/profile build. No geometry or flashlight workaround
  is enabled, and this package does not claim a resume fix.

The package is staged at the Vita storage root for installation in VitaShell.
Staging is separate from installing the app. `XV_SAVE_LOG`, `XV_ERR_LOG` and
`XV_SHOT_DUMP` are enabled in the backed-up device configuration for the next run.
Screenshot-triggered draw logging is delayed until the next screenshot-directory
poll and can cause a brief hitch; its frame is not necessarily the captured frame.
The installation remains on the loading/profile executable until the user installs
the new VPK. Device staging and post-copy integrity results are in `staging.json`
alongside the backup. The CPU-monitor package below supersedes this candidate.

## Per-core utilization requested during follow-up

The user asked to add core utilization. The next package is now
**`xita-20260905-cpu-monitor.vpk`**, combining that monitor and the texture-cache fix.

- VPK: 13,207,780 bytes; SHA-256
  `6fb4d4de0a6ad7b1893a767580c54957cbf68e56034fb902e5c4f00a9359665a`.
- Executable SHA-256:
  `794468c616699a1d868d757b0b9dd99662479bb0a362f4b03cec227915f27fc1`.
- Only the executable differs from the texture-cache package. Loading artwork,
  lighting shaders and all other assets remain byte-identical.

`xv_cpu.c` reads `sceKernelGetSystemInfo` from the existing render pump about once
a second while frames are submitted. It computes each application core's busy
fraction as one minus the change in idle time divided by actual elapsed wall
time. This includes other work on the core; it is not Halo-only time, GPU
utilization, or an inference from time spent waiting in Present. No extra worker,
thread enumeration or CPU-affinity change was introduced.

The C0/C1/C2 rows show percentages and bars in the existing overlay. Its upper
row remains FPS, game milliseconds and Present wait milliseconds. Logs include
`[cpu]` lines with process time, actual sample-window length and raw active-core
mask. The frame-time logger still uses its separate 60-Present window.
The device configuration enables `XV_CPU=1` and `XV_FPS=1`; Select + Start toggles
the overlay, and `XV_CPU=0` disables counter sampling on the next app launch.

Initial samples, inactive cores, counter resets, implausible deltas and failed
queries show dashes / `n/a`. Core 3 is system-reserved and not displayed as another
application core. The current [VitaSDK interface](https://docs.vitasdk.org/kernel_2threadmgr_2thread_8h_source.html)
defines the idle counters; Vita3K's system-info call is unimplemented, so the
emulator cannot validate real utilization. Its successful-but-empty response was
observed and is explicitly rejected instead of appearing as 100% on every core.

`make -C recomp/host test-cpu test-textures` passes. CPU tests execute the actual
monitor with controlled kernel counters and cover variable elapsed intervals,
64-bit counters, 0/50/75/100% results, sampling throttle, active masks, reset/skew,
API failure/recovery, empty emulator data and disabling. CPU tests also pass
AddressSanitizer/UndefinedBehaviorSanitizer. The native VPK builds and the emulator
renders the new overlay with unavailable counters. Real Vita readings remain
pending installation and a new run.
