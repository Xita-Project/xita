# Halo 2: a Vita CPU/GPU performance proxy (task brief)

Goal: estimate how the Halo 2 native build performs on a real PS Vita without a device in the loop,
so design changes can be measured in Vita terms. There is no faithful Vita simulator; this brief asks
for a measured proxy in three layers. The instrumentation it relies on already exists on the branch
`work/halo2-menu-claude-20260915`.

## Target hardware

- CPU: ARM Cortex-A9 MPCore, 4 cores at 444 MHz (3 cores available to the application), NEON.
  Treat an application core as 444 MHz at an IPC of about 1.0 for estimates; validate against a
  device run when one is available.
- GPU: PowerVR SGX543MP4+ (tile-based deferred renderer). No public simulator. Use a workload
  model calibrated against a device measurement, not a simulation.

## What exists on the branch

- `games/halo2_5849/host_channel_runtime.c`: a budgeted `[h2/perf]` log line every 60 queued
  flips (wall time, pushbuffer submit time and method count, clears, flip/present time, GXM draw
  count and render/flush/texture times, texture hash volume and cache hits/misses/unhashed, audio
  grain compute, worker lock hold, guest lock waits and lock sites, GP frame split and GP
  instruction count). This is the ground-truth format: the same line is produced on a device.
- Hot host-side modules, all plain C with host tests (`make -C games/halo2_5849 test-host
  BUILD=<dir>` runs 63 tests on x86):
  - GP DSP interpreter: `games/halo2_5849/dsp_engine.c` over `dsp/interp/dsp_cpu.c` (a DSP56300
    core). Measured on the host emulator: ~23.6k GP instructions per 32-sample frame, ~71 ns each
    under Vita3K's ARM JIT, so a 1024-frame grain costs ~56 ms against 21.3 ms of audio.
  - Audio sink and mixer: `audio_vita.c`, `audio_fx.c`, `recomp/kernel/xk_audio.c`.
  - Texture source hashing: `menu_texture.c` (`content_hash`, 8 bytes per step; a NEON variant
    was slower under the emulator, untested on hardware).
  - Pushbuffer consumer: `host_channel.c`, `command_state.c`, `kelvin_clear.c`, the pass
    consumers `*_draw.c`, and the GXM menu backend `menu_gxm.c`. Measured: ~2.27 M methods per
    60 in-match frames (38k per frame) at ~0.75 us each under the emulator.
- Compiler flags for the Vita build: `-O1 -mthumb -mcpu=cortex-a9 -mfpu=neon` (host files),
  `GUEST_OPT=-O1` for the recompiled game code; see `games/halo2_5849/Makefile`.
- Not in git and not available to an outside agent: the game image and its recompiled C
  (`private/`, `local/`), the DSP program image (`halo2-dsp.bin`), map/cache data. Any workload
  that needs them must be described as a fixture the maintainer supplies locally.

## Host-emulator baseline (Vita3K on x86, Mesa llvmpipe), in-match 60-flip window

| item | time |
|---|---|
| whole window (60 flips) | 6.9 s (8.7 fps) |
| pushbuffer submit (all consumers) | 3.6 s, of which GXM render 2.0 s (scene flush 0.64, texture acquire 0.62) |
| method parsing outside the render path | ~1.6 s for 2.27 M methods |
| flip: convert + one vblank wait | 0.85 s |
| guest (recompiled game) code | ~2.5 s |
| audio worker (own thread) | ~100% of one core: 56 ms per grain, 98% in the GP interpreter |

Back-of-envelope Vita estimates from these: the method consumer alone is ~85 ms per frame at
444 MHz (38k methods x ~1000 ARM instructions), and the GP interpreter needs ~35 M GP instructions
per second of audio (~5 G ARM instructions per second). Both need design changes; the proxy is
for measuring those changes.

## Deliverables

1. `tools/vita_proxy/`: an ARMv7-Linux benchmark harness for the hot modules.
   - Build the modules above (and their existing test fixtures where they carry representative
     data) with an ARM Linux cross toolchain (`arm-linux-gnueabihf-gcc`, same `-mcpu=cortex-a9
     -mfpu=neon` flags, both `-O1` and `-O2`), replacing the few `sce*` calls with stubs exactly as
     the host tests do (`audio_vita_test.c` shows the pattern).
   - Run each workload under `qemu-arm` with the `libinsn` plugin (instruction counts), and
     optionally under gem5 with a Cortex-A9 O3 configuration (cycles). Report per workload:
     instructions, estimated ms at 444 MHz for IPC 0.7 / 1.0 / 1.3, and the host-emulator
     measurement for the same work from the `[h2/perf]` numbers above.
   - Workloads: one 1024-frame GP grain (a synthetic GP program if the real image is absent;
     document the substitution); one texture hash of 128 KB and of 1.2 MB; one frame of
     pushbuffer consumption replayed from a recorded stream (the runtime can dump the push
     buffer: see `[h2/graphics] private push snapshot` in `host_channel_runtime.c`; the
     recording stays local); the scanout conversion (`scanout.c`); the mixer for 192 voices.
   - Output a markdown table; keep the harness runnable in CI-like fashion (`make -C
     tools/vita_proxy report`).
2. GPU workload counters in `menu_gxm.c`, added to the `[h2/perf]` line per 60 flips: draw
   count (exists), triangle count, estimated shaded pixels (screen-space bounding boxes of each
   draw, clipped), texture bytes sampled (source bytes of textures bound per draw), and the
   compiled shader lengths (instruction counts from the `.gxp` headers, cached per program).
   Then a small script `tools/vita_proxy/gpu_budget.py` that turns a perf line into an estimated
   SGX543MP4+ frame time using published fill and vertex rates, with the calibration constants in
   one place for a later device run.
3. `docs/halo2-vita-perf-proxy-report.md`: the first run's numbers and the calibration procedure
   (how to compare against a device `[h2/perf]` line).

## Constraints

- No proprietary bytes in git: no game data, no generated C, no DSP image, no pushbuffer
  recordings, no shader binaries from the game. Fixtures that need them are loaded from paths
  outside the repo and skipped when absent.
- Do not change emulation behaviour or weaken any strict check in the runtime; counters and
  logging only, budgeted the same way the existing `[h2/perf]` line is.
- Host tests must keep passing; add tests for any new pure function (see `tools/test_*.py` and
  the C `*_test.c` files for the style).
- Work on a branch off `work/halo2-menu-claude-20260915`; commits end with the usual trailers.

## Validation

- The harness's host-emulator column must reproduce the numbers in the table above within
  noise; the Vita column is an estimate until a device `[h2/perf]` line exists, after which the
  IPC and GPU calibration constants are fitted once and recorded in the report.
