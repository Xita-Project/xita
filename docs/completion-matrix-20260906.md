# Completion waits, in-place math and resolution benchmark — 2026-09-06

This follows the [hardware draw-preparation report](hardware-20260906-cpu-preparation.md).
The installed predecessor took the native path for only 39.5% of matrix calls,
and pending visibility queries repeatedly yielded while waiting for rendering.
Its different camera views prevented a controlled resolution/FPS comparison.

## Implementation

- Matrix multiplication accepts exact in-place output, including guest physical
  and virtual addresses that refer to the same host memory. Independent copies
  of the inputs preserve the original operation order, float rounding points,
  register context and stack spills. Partial overlap, scratch overlap,
  unaligned and cross-page spans still use the original translated function.
  `[matrix-layout]` reports native layouts and individual fallback reasons.
- Pending visibility reads park on the submitted query generation's completion
  word. The render pump publishes counts after GPU completion, then signals a
  sticky scheduler event. Early or coalesced notifications cannot substitute for
  the atomic completion check. Unsubmitted queries retain bounded polling so
  the game can reach Present. A wait is bounded at 100 ms; event creation
  failure uses the existing 1 ms fallback. `XV_VISIBILITY_EVENTS=0` disables
  the new path; `XV_NATIVE_MATH=0` retains the original math.
  Scheduler idle accounting now measures elapsed sleep, rather than the
  requested timeout, because a completion can wake the scheduler early.
- `[visibility-wait]` separates elapsed query wait from the age of the completed
  result when the guest resumes. `render-us` measures pump query preparation
  through final completion. These are overlapping elapsed intervals, not
  exclusive CPU/GPU execution counters. GPU Finish and actual four-core query
  readback remain required; query results are not published ahead of the GPU.
- An opt-in benchmark runs 544p → 360p → 544p, with 60 settling frames and
  120 measured frames per phase. Input is neutral during the test. Resolution
  changes run on the idle render pump after draining outstanding frames, and
  allocation failure cancels the test. Completion/cancellation restores the
  original runtime resolution; no configuration file is written.

## Hardware test

Start Blood Gulch alone, stand still in a normal first-person view, close menus,
and press **L + R + Select** together. The overlay shows TEST, resolution, phase
and progress. Leave controls alone until it disappears. The same chord cancels.
At 6–10 FPS the complete 540-frame sequence takes roughly 55–90 seconds; allow
up to two minutes. Continue normal play afterward and reconnect USB for logs.

Effects, texture quality and the frame cap stay fixed. Camera position/direction
are compared across phases; movement marks the result as not comparable.
The two 544p samples help reveal drift, but the test does not freeze the game
simulation or eliminate all changes in scene content. A capped result cannot
measure performance above that cap.

## Validation

- Original-versus-native math: 120,000 comparisons per run, covering exact
  in-place and physical aliases, float edge cases, rounding modes, all x87 TOPs,
  full context, output/spill bytes and fallback guards. Normal and ASan/UBSan
  runs pass; disabled-native runs also pass.
- Production visibility, scheduler deadline/notification, input, shader identity
  and frame-event tests pass normally and under ASan/UBSan, including 100,000
  concurrent scheduler handoffs and 200,000 query handoffs per visibility run.
- Benchmark tests cover timing/warmup, changed views, cancellation, unavailable
  views, allocation failure and restoration. The production pump resize test
  verifies Finish-before-free and acknowledgement of the actual resolution.
- Runtime/file/crypto/profiling, shader and draw-preparation regressions pass.
  Production frame-completion tests pass with the cap disabled and at 20 FPS.
- Native staged build: `make RECOMP=1 -j6` passes. All three decoded SELF
  segments match before and after compression/padding for USB installation.

At initial validation, hardware FPS improvement remained unmeasured. Emulator
checks validate behavior, not handheld performance; see the hardware follow-up
below for the subsequent resolution comparison.

## Executable checks

The exact same-size USB executable is 32,918,474 bytes, SHA-256
`3364033427206d324264c42446bb0a66cd752e273342c53fc13db60ffa3d8fc0`.
Archive: `/home/birchwoodgod/xita-backups/2026-09-06-162454-completion-matrix`.

In the private Vita3K installation, Blood Gulch starts through the ordinary
split-screen menus. The final executable completes all three resolution phases
with an unchanged camera: 19.998 / 20.007 / 20.000 FPS at the 20 FPS cap.
Cancelling a second test during its 360p phase restores 544p, then movement,
camera turns, firing and leaving through the pause menu work. The automation's
one-second cancellation-log assertion ran before those messages became visible;
the subsequent complete log and gameplay verify cancellation/restoration.

Across 43 reports with over 1,000 native matrix calls, including transition
windows, 99.08% of matrix calls use native arithmetic. There are 648,659 left
in-place and 425,923 right in-place calls; the 17,781 remaining fallbacks are
cross-page spans. This demonstrates coverage in this emulator run, not an
equivalent Vita speedup.

An earlier private emulator process remained alive after SIGTERM and overlapped
the capped Blood Gulch check. It was explicitly stopped before the campaign
check. The two processes used separate open log handles; all final-executable
benchmark results above come from its current log. Do not use these capped,
overlapping sessions for CPU/GPU performance comparisons. The existing Vita3K
OpenGL `v_Color0` diagnostic and recoverable write-protection diagnostics also
remain; these checks do not claim a warning-free emulator.

The same final executable subsequently loads a10 on Normal, renders the opening
space scene, and skips into the cryo bay with the technician visible. Three
sampled campaign frames check 578 / 578 / 577 draws with zero data changes before
GPU completion. This is a bounded transition/geometry regression check, not a
complete campaign playthrough.

## USB deployment

Installed September 6 at 16:59 CDT. The prior executable is backed up on the
host. The existing executable allocation was overwritten at the same length;
direct device reads and a fresh read-only mount verify the final SHA-256 and
657 unchanged other files, including configuration and saves. The Vita is
safely unmounted. Hardware benchmark results were pending at deployment.

The [first hardware benchmark](hardware-20260906-completion-matrix.md) is now
collected: 7.389 / 9.211 / 7.451 FPS at 544p/360p/544p with a matching camera.
Matrix native coverage is 98.3% and mean query completion-to-resume delay is
96 microseconds. The resolution improvement is measurable; sustained 20 FPS
still requires additional work.
