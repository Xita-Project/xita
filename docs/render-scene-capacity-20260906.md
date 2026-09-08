# Render-target scene capacity, September 6

The [latest hardware run](hardware-20260906-alpha-visibility.md) identifies
EndScene stalls of up to 45.070 ms/frame in a Battle Creek reporting window.
This candidate tests additional per-target scene capacity and attributes those
stalls to individual targets. The [first hardware result](hardware-20260906-scene-capacity.md) shows shorter EndScene stalls, but no clear whole-game FPS gain.

## Change and rationale

The display, 480p and offscreen targets previously used `scenesPerFrame=1`.
Halo repeatedly returns to targets within a frame. Extra capacity may reduce
driver reuse stalls; this is an inference to test, not a proven explanation of
the hardware timings. The public VitaSDK header describes `scenesPerFrame` as
the expected number of scenes per target. The
[vitaGL implementation](https://github.com/Rinnegatamante/vitaGL/blob/cd3791e29ff7f1c0ab349f12c7231f4871ce6a75/source/gxm.c)
also exposes configurable scene counts and bounds them at eight. It defaults
to one; it is a reference for the supported mechanism, not evidence of a Halo
performance improvement.

`xv_render_target.h` centralizes allocation policy for all three target types:

- `XV_RT_SCENES` defaults to four and clamps numeric values to 1–8. Missing,
  empty or malformed settings use four. Set one before startup to restore
  the previous allocation policy.
- Query the one-scene driver size and each proposed larger size. Allow at most
  512 KiB of extra driver memory per target. Offscreen allocations additionally
  stay inside the existing 16 MiB combined surface/driver budget.
- If a larger query/allocation fails or exceeds either budget, retry smaller
  capacities down to one. A failure at one retains the caller's failure path.
  Input parameters and output ownership stay unchanged on failure.
- Capacity is selected only when creating a target. Existing live targets are
  never resized, and all GPU completion, depth preservation, target identity,
  frame-owned buffers and visibility-generation rules remain in effect.

`[render-capacity]` logs requested/effective capacity, dimensions and queried
driver memory, including fallback attempts. The maximum additional allowance
for all eight offscreen slots is 4 MiB within their existing total budget;
display and 480p targets can each use at most another 512 KiB. Actual cost must
come from Vita logs: the emulator does not model this allocation identically.

`[render-target]` adds per-target EndScene time, total scene endings and the
maximum scene endings in one frame to each 60-frame report. Target zero is the
game backbuffer, 1–8 are offscreen slots and nine is the display upscale pass.
These times are subsets of the existing EndScene/submission timers. They must
not be added to the parent timers or described as GPU execution times.

No shaders or guest game code changed from the installed alpha/polling build.
Compare whole-frame timing as well as EndScene: moving a stall into final
completion is not a saved frame-time cost.

## Validation

The actual allocation and target replay implementation passes host tests with
one, four and eight scenes, with intermediate waits both enabled and disabled.
Tests exercise memory budgets, query/create failure fallback, configuration
bounds, total accounting, release/reuse, producer/consumer order, depth/UI
preservation and failure cleanup. Profiler tests cover per-target count/max/time,
single evaluation and return values, disabled timing, clock origin and reset.
The production frame-completion regression passes. AddressSanitizer and
UndefinedBehaviorSanitizer pass with global instrumentation enabled.

The native traced `make RECOMP=1 -j6` build passes. Exactly eight runtime/header/
test files differ from the installed source snapshot. The final native SELF is
34,682,518 bytes; compression and padding fit the existing 32,918,474-byte USB
allocation, with all three decoded segments verified equal to the native file.

Candidate archive:
`/home/birchwoodgod/xita-backups/2026-09-06-123444-render-scene-capacity/`.
Installed USB SHA-256:
`a8c34f3dff71e6ba5e8738eed55186008ce5089c6d6b9b8ff11586015b906840`.

## Isolated emulator run

The exact padded USB executable ran on virtual display `:97` with OpenGL
software rendering. Normal solo menu selection started Blood Gulch. Movement,
camera turns and plasma charge/fire rendered; pause/leave returned to the main
menu. Battle Creek then loaded through the normal menu in the same process.
Movement, plasma effects and flashlight input left the world and weapon visible
in the captured views. Existing OpenGL `v_Color0` link errors remain, so these
checks do not establish pixel equivalence or resolve previous lighting bugs.

All seven logged target allocations accepted four scenes. The emulator reports
65,536 driver bytes for both one and four scenes; this cannot establish the
actual Vita memory cost. Reports cover targets 0–5 and 9, with up to six scene
endings for one target in a frame. A capacity of four is not a scene-count limit;
the driver can still wait on reuse when that capacity is exhausted.

All 192 complete checked windows have matching per-target counts and timing
sums, retain final GPU completion/display queue calls and avoid intermediate
target Finish calls. All 194 geometry-capacity reports show zero drops. All
46 specialized shader loads retain `discard-used 0`; no explicit render-target
errors appear. The last reporting window is excluded to allow for partial log
output at intentional shutdown. Logs, screenshots, the analyzer and results
are archived under `validation/`.

Hardware frame time, actual driver allocation and effective fallback capacity
remain to be measured. Use the same settings and repeat the prior Blood Gulch
route, including comparable camera directions and firing actions. Compare
EndScene, final completion and the whole pump together.

## USB installation

Installed at 12:44 CDT; fresh read-only mount verification completed at 12:45.
Direct device readback and remount hashes match the exact emulator-tested SELF.
The update overwrote the existing allocation without creating, truncating or
renaming card files. All 657 other checked files remain unchanged, including
saves and settings. The current read-only hardware backup was verified again
before installation. The card was safely unmounted afterward.

This is the latest installed diagnostic build. No VPK reinstall is needed.
The first hardware run records median 9.65 FPS in Blood Gulch; EndScene stalls are shorter, but no overall FPS improvement is established.
