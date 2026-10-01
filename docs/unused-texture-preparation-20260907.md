# Unused texture-stage preparation — September 7

The draw recorder previously resolved every bound texture, even when the
fragment shader did not sample it. This pass skips resolution and sampler
preparation for stages unused by every possible shader for that draw.
In the isolated Blood Gulch check, it skips **30.3% of bound texture stages**.
The [hardware follow-up](hardware-20260907-unused-texture-preparation.md) confirms
**31.6% skipped** in its measured view. These are work-count reductions, not an
established hardware FPS improvement.

The [13:05 controlled CPU test](hardware-20260907-cpu-comparison.md) now measures
all three changes off/on/off at 544p: **7.079 / 7.065 / 7.204 FPS**. Switching
works, but no FPS gain is established in that view. Prioritize rendering-pass
investigation next; another user CPU benchmark is not needed immediately.

The [preceding hardware test](hardware-20260907-clip-palette.md) still takes
132 ms/frame at 360p. This change addresses part of texture preparation within
the draw path; substantial CPU and GPU work remains to reach 50 ms / 20 FPS.

## Implementation

- Read sampler presence from the exact embedded GXP program using the same
  parameter API as the fragment loader. This requires no file I/O, allocation,
  shader linking or reads of the render thread's mutable link cache.
- Cache the union of normal, alpha-disabled and every cube-to-2D variant in
  each shader family. Include the heuristic fallback's samplers and all stages
  used to select a cube-to-2D variant. A missing or invalid program retains all
  four stages. Family metadata is evaluated once, on the guest recording thread.
- Embed the four existing fallback fragment programs as well. These are the
  same compiled bytes shipped in the package; executable-only updates now carry
  them and their sampler metadata together. Embedded GXP arrays are aligned for
  metadata access. Development shader overrides conservatively keep every stage.
- Retain the original live texture, palette, render-target and address checks
  for every required stage. Keep coordinate scales for all stages because a
  dependent read can use coordinates without sampling that stage's texture.
  Preserve draw order, index snapshots, geometry, blending and shader arithmetic.

`XV_UNUSED_TEXTURES=0` restores preparation of every bound stage. The runtime
override accepts -1 (configured default), 0 (off) and 1 (on). It is owned by
the guest thread. `[texture-prep]` reports stages prepared and unused stages
skipped over each 60-frame window.

## Controlled CPU comparison

On this candidate, **L + R + Square** compares **three** changes together:
clipper integer locals, palette hash reuse, and unused texture-stage skipping.
It switches them **off → on → off** at the current resolution, then restores
configured defaults. Completion, cancellation and invalid-view handling retain
that restoration. The start log explicitly names all three changes.

The [preceding candidate](clip-palette-20260907.md) compared only the first two.
Do not mix its comparison results with this bundle. The comparison cannot
attribute savings to an individual change. **L + R + Select** remains the
separate 544p / 360p / 544p resolution test.

For hardware testing, enter Blood Gulch solo, stand still in first person with
menus closed, press **L + R + Square**, and wait until the **CPU** panel
disappears. Allow about two minutes before reconnecting USB.

## Validation

- Production recording passes 54,784 differential cases per configuration,
  with normal defaults, disabled defaults and shader overrides. The same three
  configurations pass ASan/UBSan. Cases cover every shader-table entry and
  fallback, variant-specific sampler dependencies, changing/missing textures,
  cube selection, previous-backbuffer substitutions and dependent coordinates.
- Shader-loader fixtures check 18,432 metadata masks per source preference,
  alignment, unknown/invalid programs, exact embedded bytes, override precedence
  and absence of metadata file I/O. Existing sampler/depth, texture feedback,
  benchmark and Vita-input checks pass.
- The isolated `RECOMP=1` native build succeeds. The 5,106-file source manifest
  preserves generated game code and profiling. Native, compressed and padded
  executables have matching decoded SELF segments.
- The exact padded USB executable enters Blood Gulch through the normal solo
  lobby. Camera turns, movement, firing, flashlight toggling, pause exit and both
  benchmark types run. CPU-test cancellation restores all three default paths.
  Captures retain the base, terrain, sky, weapon and HUD across the CPU phases.
- The a10 opening renders; Cross skips to the first-person cryo bay and the
  camera responds to right-stick turns. All 11 sampled geometry frames have
  zero changes before GPU completion. All 310 capacity reports show zero drops;
  309 complete render reports have consistent stage sums and 60 final Finish/
  display-queue calls, with no intermediate target Finish calls. No explicit
  render-target/shader-loading failure was found. These are bounded checks,
  not full campaign or hardware validation.
- The private emulator and Xvfb are stopped and their private configuration
  restored. The emulator required SIGKILL after a SIGTERM timeout during
  cleanup; gameplay checks completed before shutdown. Hardware resolution-test
  FPS and the completed hardware CPU comparison are recorded below.

The CPU comparison's supporting reports show:

| Work per 60-frame report | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Texture stages prepared | 77,160 | 53,759–53,760 | 77,160 |
| Texture stages skipped | 0 | 23,397–23,400 | 0 |
| Integer-local clip calls | 0 | 49,800 | 0 |
| Palette hashes reused | 0 | 1,500 | 0 |

Exact 120-frame CPU phase results are 18.643 / 18.795 / 18.581 FPS. The separate
resolution check gives 19.645 / 19.798 / 19.676 FPS. These run in software OpenGL
on the host with a 20 FPS cap; geometry sampling also adds work in each phase.
They validate behavior and path switching, not Vita performance. Supporting
60-frame work reports have different boundaries from the exact FPS intervals.

## Candidate

Archive:
`/home/birchwoodgod/xita-backups/2026-09-07-110626-unused-texture-prep`.

The padded 32,918,474-byte executable has SHA-256
`d97bffe770677b02c7436edb48f89ceabf1d8302ee239e81ab9be384d3b23b3e`.
The archive includes the source snapshot, binaries, host checks, emulator
captures/logs and a read-only backup of the previous device executable and 657
other files. Installed at 11:33 CDT into the executable's existing allocation.
Direct reads and a fresh read-only mount verify the new executable and all 657
other files unchanged, including settings and saves. USB storage is safely
unmounted. `deployment.json` records the verification.

The [12:05 hardware collection](hardware-20260907-unused-texture-preparation.md)
verifies the installed executable and unchanged standard settings. Its
resolution benchmark gives 9.604 / 13.166 / 9.711 FPS at 544p / 360p / 544p,
with all three CPU changes enabled throughout. There are no CPU-comparison
markers. The camera and draw count differ from the preceding build's test,
so these results do not establish an optimization speedup. The later
[controlled CPU test](hardware-20260907-cpu-comparison.md) confirms correct
off/on/off switching, with 7.079 / 7.065 / 7.204 FPS. Its on phase is 1.06%
below pooled off throughput, smaller than the 1.77% difference between off
phases. No measurable FPS benefit is established in that view.
