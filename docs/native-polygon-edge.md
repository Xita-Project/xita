# Optional exact-state polygon-edge helper

This is an **off-by-default candidate for physical comparison**, not a measured
frame-rate improvement. `XV_NATIVE_POLYGON_EDGE=1` includes the helper and guarded
entry hook. Without that build flag the ordinary translated function is unchanged.
The enabled build also starts with the runtime mode disabled. The remote
`polygon-edge` comparison measures original/native/original at the same camera
and resolution, then restores the disabled default on completion or cancellation.
The first valid gameplay request initializes the controller on the joined guest
recording owner. Network admission never initializes it. Periodic native-call
counts establish coverage after initialization; unavailable builds reject the
request without changing settings.

The generator accepts only the owned Halo CE 3925 image with SHA256
`4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae` and the
195-byte function at `B77C0` with SHA256
`5ceeee6fc591265ef9a0e51d3cd0a0cac96b0a100d27b3e29a1e9a10be4aba73`.
Both guards also apply to hook emission. Altering the span removes only the
hook; generation compares the remaining ordinary instruction lift exactly.
The generated helper and independent instruction reference are ignored and
excluded from source review exports. They contain owned-game translations and
must not be distributed as source or embedded in a public diagnostic package.

## Full-state contract

The original routine takes its polygon pointer in ECX, signed 16-bit count at
entry ESP+4, point pointer at ESP+8, and float radius at ESP+12. The known direct
caller tests AL, but the static reference audit is not a closed-world proof
against computed calls. No conventional calling convention or dead-output
assumption is used here.

The helper preserves all GP registers, lazy flags including unused payloads,
x87 slots/status/control, SSE/MMX/control fields, guest stack writes, aliases,
float store rounding, original memory order, and the original scheduler boundary.
There is no new early count read: the nonpositive path still performs the exact
radius operation, stack writes, and original epilogue. This preserves access and
fault ordering, but that short path remains more expensive in the model.

The generator proves all 79 instructions have balanced x87 depth and no incoming
x87 values are read. Relative slots 0 and 1 remain untouched; the nonpositive
path writes only slot 7; positive paths write slots 2 through 7. The same proof
starts with no defined slots at the loop's scheduler reentry. Thus the helper
can omit initial FP loads and write only dirty slots, without discarding any
observable output. At a handoff it publishes complete exact state, calls the
original scheduler, then reloads GP/flag/FSP state. Each next iteration defines
its FP values before use, including after arbitrary scheduler changes.

## Integration interface

`recomp/kernel/xk_polygon_edge.h` declares:

- `xv_native_polygon_edge_init()` binds the current native thread as control
  owner at a joined/idle worker boundary. Existing idle workers are allowed.
  Choose the eventual guest recording/report thread, not a different bootstrap
  thread. Mode remains off. Repeated initialization must use the same idle owner.
- `xv_native_polygon_edge_available()` reports whether initialization completed;
  it is safe to query before initialization and from other threads.
- `xv_native_polygon_edge_override(mode)` enables positive values, disables zero,
  and restores the disabled default for `-1` (and other negative values).
- `xv_math_polygon_edge_calls()` atomically drains the interval count, modulo
  unsigned. It requires the initialized idle owner, like the override.

Owner controls call the existing weak `xv_object_math_report_check()` when linked.
They additionally reject active helpers, including helpers suspended in a guest
scheduler callback. The integrating runtime must stop/join all submitting workers
before invoking controls; this interface does not itself join them. The mode and
active count share one atomic word, so concurrent admission cannot race a mode
transition silently. Counter increments/drains are atomic. Hot admission/end do
not query native thread identity, allocate, read environment variables, or take
an OS mutex. Thread identity is checked only by the rare owner controls.

The public helper checks admission before entering a separate native FP frame.
Returning zero touches no guest context or memory and runs the existing original
fallback. `Makefile` tracks the build flag on every generated shard containing
the hook and on the helper/control objects. Both mode transitions also invalidate
the game archive so disabled native members cannot survive incremental builds.

## Reproduction and results

Use a Python environment with the repository's recompiler dependencies:

```sh
python tools/gen_native_polygon_edge.py --xbe /owned/haloce/default.xbe --manifest /owned/game_manifest.json
python tools/gen_native_polygon_edge.py --check --xbe /owned/haloce/default.xbe --manifest /owned/game_manifest.json
python tools/test_polygon_edge_guards.py --xbe /owned/haloce/default.xbe --manifest /owned/game_manifest.json
python tools/test_native_polygon_edge.py --output-dir /private/edge/host
python tools/test_native_polygon_edge.py --sanitize --output-dir /private/edge/host
python tools/test_polygon_edge_build.py
python tools/test_release_audit.py
```

With VitaSDK, Unicorn and pyelftools available, run:

```sh
python tools/test_arm_polygon_edge.py --output-dir /private/edge/arm
```

The integrated candidate passes 4,096 original full-context/memory fixtures
(4,111 original preemption handoffs), and another 4,096 mutation fixtures
(3,781 handoffs), both normally and under ASan/UBSan. Mutation cases change all
x87 slots, FSP, GP/flags, MMX/SSE/control fields, mapped pointers and the live guest
stack location. Four rounding modes, exceptional floats, page remapping and
stack/input aliases are included. Lifecycle tests cover four concurrent callers
(200,000 exact admissions), wrong-owner controls, active helper rejection, pool
busy rejection, default-off mode, and `-1` restoration. Synthetic off/on/off/off
builds restore identical disabled objects, remove archive members, and avoid
rebuilding when mode is unchanged. Image/span rejection and the ordinary profile
isolation tests also pass.

The 768 linked Cortex-A9 fixtures preserve full context, memory and FPSCR across
16 FP-control combinations. Their scheduler model resets the original budget;
the stronger arbitrary-state mutation oracle is the host suite. Default-off and
`-1` restoration also retain complete context/memory/FPSCR in the ARM model.

| ARM model sample | Original instructions | Candidate instructions |
| --- | ---: | ---: |
| Selected mode-0 cases 1–28 | 122,153 | 101,787 |
| All 768 fixtures | 3,081,200 | 2,579,776 |
| Zero-count case, mode 0 | 168 | 238 |

The selected reduction is 16.67%; the zero-count sample regresses. Counts include
the actual compiled atomic controls but exclude modeled memory-copy internals,
contention and real scheduling/OS costs. They are not CPU cycles or an FPS claim.
VitaSDK reports 360 bytes for the native kernel plus an 8-byte public wrapper
(368 bytes combined, excluding called runtime/scheduler frames). The earlier
full-slot private prototype used 440 bytes; the dirty-slot prototype used 360.
The disabled public path uses its small wrapper and admission function only.
These differential checks do not establish a hardware performance improvement.

## Physical comparison

Runtime `bd24033a500577df43cbd5393d190734fcf565d89b3be4abe1ba57aabd346b6a`
was installed and boot-confirmed through the Wi-Fi updater. All 1,588 package
members and the existing asset contract are preserved; only the executable and
boot record change. The preserved A slot remains available. No emulator was run.

Six same-session Blood Gulch comparisons use native 960×544 with unchanged saved
graphics, existing workers/optimizations, 60 settling and 120 measured frames
per arm. Each trial keeps an identical camera across its three arms. Live game
simulation and remote status polling continue. The second group rotates the
camera without moving the player.

| View / trial | Off before FPS | On FPS | Off after FPS | Frame time saved vs bracket mean |
| --- | ---: | ---: | ---: | ---: |
| First / 1 | 9.115 | 9.355 | 9.260 | 1.953 ms |
| First / 2 | 9.319 | 9.349 | 9.294 | 0.494 ms |
| First / 3 | 9.284 | 9.354 | 9.264 | 0.928 ms |
| Second / 1 | 10.598 | 10.641 | 10.598 | 0.379 ms |
| Second / 2 | 10.671 | 10.380 | 10.724 | -2.853 ms |
| Second / 3 | 10.771 | 10.765 | 10.733 | 0.114 ms |

Frame deltas use actual elapsed microseconds, not rounded FPS. Complete counter
windows establish 276.16 and 253.87 native calls/frame in the enabled arms of
the two views, and zero in their disabled arms. Each counter sample excludes
the first partial report after measurement starts and covers 180 frames per
arm across three trials; FPS covers 360. All camera/restoration checks pass.
The regression is retained in the result; its two measured report writes take
52.79 ms total, versus 31.56/23.36 ms in the surrounding arms, which cannot alone
explain its roughly 342 ms total-frame penalty.

The first view is slightly positive, but the second does not demonstrate a
repeatable gain. **The runtime override is restored OFF; default promotion is
not justified.** The candidate remains available for cumulative or broader
experiments. These views do not establish driving, campaign, combat stability
or resolution of reported crashes, and they do not meet the 20 FPS goal.

The disabled arm still includes the experimental admission call. A separate
linked ARM check of the exact hook shows 122,153 instructions without the hook
versus 123,925 with it disabled across the selected 28 fixtures (about 1.45%
extra for this routine); enabled remains 101,787. All 768 full-state checks pass.
This bounds the modeled hook tax, not its physical latency. The comparisons do
not prove a gain over an otherwise identical executable with the hook omitted.

Private receipts, images, logs and analysis are under
`engine-restructure-20260914T2300Z/physical-polygon-edge`; package/build evidence
is in `edge-pipeline*`. Generated code, original bytes and game assets remain
outside Git. The separately inspected [MCC reference tags](mcc-mod-tools-reference.md)
were not used to replace the Xbox math or map data in this build.
