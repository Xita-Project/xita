# September 5: first hardware CPU-monitor run

The user reports core 2 busiest, with the other cores often around 5–10%.
The installed executable matches `xita-20260905-cpu-monitor.vpk`, SHA-256
`794468c616699a1d868d757b0b9dd99662479bb0a362f4b03cec227915f27fc1`.
Startup clocks are CPU 444, bus 222, GPU 222 and crossbar 166 MHz.

Logs, configuration, saves/cache and eight latest screenshots are backed up at
`/home/birchwoodgod/xita-backups/2026-09-05-081406-cpu-hardware/`.
`manifest.json` records file hashes; `analysis.json` retains raw parsed rows and
the summary. No save repair or graphics setting change was made.

## Measured results

The current log has 437 CPU intervals over approximately 472.5 seconds, including
loading/menu activity. Restricting CPU samples to those between two consecutive
frame-time rows that both report at least 10 BSP draws gives 133 samples and
142.4 seconds of sampled windows:

| Core | Time-weighted busy percentage | Observed range |
| --- | ---: | ---: |
| C0 | 6.49% | 4–13% |
| C1 | 22.67% | 4–59% |
| C2 | 77.68% | 44–96% |

These are system-wide kernel idle-counter deltas, not per-thread or GPU load.
The screenshot at 08:10:08 shows C0 7%, C1 21%, C2 85%, alongside 7 FPS.
The calculation agrees with the idle-delta method in
[PSVshell](https://github.com/Electry/PSVshell/blob/master/src/perf.c#L78).
The measurement is not an independent comparison with PSVshell on this device.

Selecting all 23 frame-time intervals with at least 10 BSP draws gives 1,380
Presents over 168.5 seconds: aggregate Present throughput 8.19 FPS, with logged
intervals from 5.3 to 12.5 FPS. Game-side elapsed time averages 116.9 ms/Present,
Present wait 5.2 ms and pump elapsed time 53.7 ms. These timers overlap; pump
elapsed time includes waits and does not directly measure GPU execution.

Texture decoding averages 0.76 ms/Present and 5.2 decodes per 60 Presents, versus
5.6 ms/Present in the earlier Keyes/combat selection. Several later intervals have
zero decodes. This supports the cache fix removing redundant work on hardware,
but differing scenes/routes prevent assigning a controlled FPS gain.

## Affinity discrepancy and diagnostic follow-up

`main.c` explicitly requests core 0 for `xv_recomp` and core 1 for `xv_pump`.
However, game code runs in `xk_fiber` kernel threads created with affinity zero;
the bootstrap spends time parked in the cooperative scheduler. Audio and profiler
workers explicitly allow cores 0–2. Only one guest fiber executes at a time.

The [SDK documentation](https://docs.vitasdk.org/group__SceThreadMgrUser.html)
contradicts itself: the default-mask macro describes inheriting the calling
thread, while `sceKernelCreateThread` describes inheriting the calling process.
The earlier claim that game logic is confined to core 0 was not verified on
hardware. The new readings contradict it, but cannot identify the busy thread.
Do not attribute core 2 solely to audio, or pin threads elsewhere as a claimed
performance fix without measuring their effective masks and workload.

`XV_THREADS=1` now logs thread names, IDs, actual CPU IDs, effective affinity masks,
priority, status, raw cumulative run clocks and migration counts. Bootstrap,
render pump, guest fibers and audio/profiler workers report at startup; the
currently presenting guest thread reports at most once a second. This adds no
thread, changes no affinity, and introduces no per-draw query. CPU IDs are point
samples, not a distribution of execution time. Raw run clocks are not displayed
as utilization. Vita3K supplies only partial thread information and cannot confirm
physical Vita scheduling.

## Graphics settings

The existing `XV_TEX_MAXDIM` setting defaults to 256. A trial value of 128 chooses
smaller existing mip levels for eligible textures, reducing detail and storage/
bandwidth. It does not resize single-level, linear or cube textures, reduce draw
count or change CPU game logic. Configuration changes require a restart.

Resolution is currently fixed at 960×544; a safe resolution scale needs renderer
work. There are no supported bloom, shadow-quality, particle-quality or draw-distance
sliders. `XV_SKIP_VS`, `XV_SKIP_PS` and `XV_FS_FORCE` are pass-isolation diagnostics
that can remove required geometry or lighting. `XV_BC_MIPS=1` remains an experiment
pending hardware layout validation, not a general performance preset.

Measure lower texture detail and future resolution/effect settings one at a time
in the same scene. Leave the validated lighting and loading shaders in place.

## Integrated dashboard candidate

During the follow-up, the user requested a pre-launch settings dashboard and
clarified that it should have a Launch Game button. The existing standalone UI
prototype now has an embedded mode that runs inside Xita before Halo starts.
It exposes supported texture detail, volume, controls and display settings with
readable labels, automatic saving and a direct hand-off to Halo's normal menu.
It does not implement direct map/save launch or return from the game.

`xita-20260905-dashboard.vpk` includes that dashboard and the thread diagnostics:

- VPK: 13,224,813 bytes; SHA-256
  `8b20f495ff5a522680e6e88c79f21b49a3f0e7514c107a06c610394a5c98db9c`.
- Executable SHA-256:
  `e6193165d77ea5d7a60d70166c797eade3fb82e80d123e1c07eb6745e60ffd64`.
- Every packaged asset except the executable is byte-identical to the installed
  CPU-monitor package. No graphics setting is lowered by packaging/staging.
- Native build, dashboard host tests, CPU/thread tests and ASan/UBSan checks pass.
  The dashboard tests exercise one-button launch, setting persistence, selective
  key updates, duplicate keys/comments, failed writes and missing game files.
- The emulator renders the dashboard, navigates with the controller and persists
  texture cap 128, volume 60 and sensitivity 125 in its isolated config. Launch
  reloads those values, the pad reports sensitivity 125, and Halo reaches its menu
  and the first level's intro. Restarting the final build shows the persisted
  settings, including the corrected percent glyph, and launches Halo again.
- The dashboard uses the existing display buffers and frees its state before
  guest startup. It adds no game-time worker or framebuffer allocation. Real
  Vita dashboard scanout, input and the effective thread masks remain to test.

Package, build/test logs and screenshots are in `dashboard-candidate/` alongside
the backup. `staging.json` records the verified device copy and save integrity.
The device config enables `XV_THREADS=1` and `XV_DASHBOARD=1`. Installing the VPK
in VitaShell is still a separate user action; copying it does not replace the
installed executable. The intermediate thread-only VPK was built and tested
locally but is superseded by this dashboard package.

The first dashboard installation attempt failed immediately. The subsequent
[USB inspection and recovery](hardware-20260905-install.md) found overwritten
bytes in the on-card VPK. A replacement passed direct reads and archive checks,
but checking other files after remount caught the VPK overwriting Xita's config.
The config was restored and further USB file allocation stopped. The user preferred
USB, so the dashboard executable was compressed, padded to the existing executable's
length, checked in Vita3K and written in place without extending the file. See the
installation report for verification and recovery details. The old on-card VPK is
not ready to install; hardware testing should use the existing Xita bubble.
