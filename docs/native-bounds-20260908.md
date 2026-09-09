# Native bounding-box visibility experiment — September 8

The first candidate from the Halo decompilation review targets the camera's
bounding-box visibility test at `0x5C300`. It retains the original outside,
partial and fully-inside classifications, including the optional reverse test
of frustum points against the box. It does not change which models should draw.

The optimization is **off by default**. The later
[hardware comparison](hardware-20260908-native-bounds.md) measured
**9.492 / 9.351 / 9.547 FPS** for original/native/original in a matching stationary
view. It showed no benefit; retain the original routine as the default. The
earlier [12.73 FPS gameplay session](hardware-20260908-evening.md) used different
activity/settings and is not the baseline for this comparison.

## Why this function

The latest hardware log contains 65 profiler reports after mesh frame 1139.
`0x5C300` contributes approximately 2.45% when averaging the rounded, reported
top-40 entries. These are wall-clock samples, include scheduling effects, and
omit functions outside each report's top 40; they are not CPU-cycle measurements.
Clipping at `0xB71C0` remains a larger candidate but already has native lowering.

The reviewed [reference knowledge base](https://github.com/stianeklund/halo/blob/a613ab54a68366ad1053d1aa913ddf687a14df8e/kb.json)
names `render_frustum_cube_visible` at `0x1867F0`, and its
[camera reconstruction](https://github.com/stianeklund/halo/blob/a613ab54a68366ad1053d1aa913ddf687a14df8e/src/halo/render/render_cameras.c)
describes the planes, points and enclosing bounds. Those are semantic clues;
the executable fingerprints differ and the reference function is not marked
ported. Its address is not substituted into Xita.

No external implementation was copied. Xita's generator uses the supported
local executable and verifies all 733 bytes of `0x5C300..0x5C5DC`:

```text
5e463d77ea6ed255323f310d40cf3f7e847c08e7a6a71841b12545b1f937e1ab
```

The reference repository had no project-level license clarifying implementation
reuse when reviewed. Its source remains outside Xita. Generated game C remains
ignored and excluded from source-review exports.

## Implementation

`tools/gen_native_bounds.py` proves x87 stack depth at every reachable
control-flow join before assigning fixed native floating-point locals.
The longer test also keeps integer registers and flags in local state.
The initial broad-bounds rejection uses the ordinary lift, avoiding most of
the local-state setup for cheap rejects.

Operation order, double intermediates, float spill points, NaN comparisons,
guest page translation, scratch writes and all returned register/flag/FP state
remain. Each original backward-branch scheduling check remains too. Before a
yield the helper publishes its state; afterward it reloads the resumed state.
It does not retain pointers or input snapshots across a yield.

The profile requires both the supported whole image and the complete function
signature. A changed tail byte leaves the ordinary translated implementation.
`[native-bounds]` reports the number of calls that used the candidate.

## Validation

- Host and ASan/UBSan comparisons: 10,240 cases enabled and 10,240 disabled
  in each run, comparing the entire test memory and context. Cases cover all
  three outcomes, alignments, split pages, physical/input/scratch aliases,
  nonfinite values, all x87 TOPs and four rounding modes. Forced yields compare
  the published context and test changes to FP/register state and input memory.
- Actual linked Vita ARM entry points: 256 comparisons under Cortex-A9
  instruction emulation, with full context/memory agreement. The test services
  imported kernel memcpy/memmove as byte copies and does not count their internal
  instructions; copy-call and byte counts match between the two versions.
- Synthetic full-path cases execute **38.72% fewer counted ARM instructions**
  in aggregate. Quick-rejection cases average about 178 versus 144 instructions.
  These numbers exclude firmware internals, cache behavior and GPU work. They
  are neither CPU-cycle measurements nor a predicted game FPS gain.
- All eight benchmark-selector combinations pass, including precedence,
  fixed-resolution phases, cancellation, lost view and restoration. Production
  submission/slot ownership and resolution-handoff tests pass.
- The native build and VPK integrity checks pass; every VPK payload matches
  the VitaSDK packer. The complete game regeneration matches the tested target
  unit. Two other pre-existing differences are a comment and declaration ordering.

The final package also passed a Blood Gulch smoke test in an isolated Vita3K
OpenGL instance: left/right camera turns, walking and assault-rifle firing.
Inspected captures show the terrain, base, rock, weapon and firing effects
without obvious missing geometry. Campaign and hardware visibility remain untested.
The original/native/original comparison completed at **19.972 / 19.966 / 19.969
FPS**, with a comparable stationary view and successful setting restoration.
All phases reached the configured 20 FPS cap; this verifies the comparison flow,
not a performance improvement. Native-call counters confirm the helper ran.
The lab was stopped and restored; the ordinary desktop emulator was untouched.

## Hardware comparison

Install `xita-native-bounds-20260908.vpk`. Keep the existing game data, saves,
graphics settings and resolution unchanged. Add these developer settings to
`ux0:data/xita/xita.cfg`, then restart:

```ini
XV_NATIVE_BOUNDS=0
XV_BENCHMARK_NATIVE_BOUNDS=1
```

Load Blood Gulch, face a scene with buildings/rocks, and stop moving.
Press **L + R + Square**. The test runs original/native/original at the current
resolution, with 60 settling frames and 120 measured frames per phase. It logs
`[native-bounds-compare]`, rejects camera movement for comparability, and restores
the configured setting afterward. This selector takes precedence over the
vertex-copy and draw-scan experiments; neither changes during this comparison.

After collecting that log, a separate run with `XV_NATIVE_BOUNDS=1` can check
camera turns, model visibility and driving. Set it back to `0` to restore the
original routine. Do not infer improvement from the 20 FPS cap or one screenshot.
Retain the candidate only if hardware throughput and visibility checks support it.
