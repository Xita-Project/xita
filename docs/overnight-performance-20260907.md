# Overnight performance work — September 6–7, 2026

Hardware follow-up: the [September 7 benchmark](hardware-20260907-overnight-performance.md)
now measures 7.939 / 9.897 / 8.194 FPS at 544p / 360p / 544p. All four paths run;
the changed camera prevents a controlled speedup claim against the prior build.
The report below records the implementation and validation at deployment time.

The target remains sustained 20 FPS on hardware. This candidate addresses the
three agreed items: translated CPU work/draw preparation, avoidable scheduler
handoffs, and fragment work in GPU passes. Hardware FPS has not been measured
for this candidate. The preceding fixed-camera hardware benchmark was
7.389 / 9.211 / 7.451 FPS at 544p / 360p / 544p; see
[the baseline report](hardware-20260906-completion-matrix.md).

## Memory audit

Both the installed application SFO and the native build already request
extended application memory with `ATTRIBUTE2=12`. Its newlib heap is 48 MiB, and the decoded texture pool is
32 MiB. The 64 MiB Xbox physical address arena reproduces Xbox address layout;
increasing that constant alone does not make more Vita RAM available safely.

The preceding hardware benchmark used approximately 8,453 KiB of the texture
pool for 130 textures and decoded zero textures in all three measured phases.
Its two texture purges were explicitly requested after map-tag loads. The log
contains no allocation-failure evidence. There is no demonstrated texture-pool
capacity bottleneck to justify increasing it. This does not measure total free
system RAM or exclude pressure in untested maps.

## Changes

- **Polygon clipping:** `0xB71C0` accounts for about 5.86% of the sampled world
  windows in the preceding run. The exact 874-byte Halo 3925 routine now uses
  native floating-point locals instead of dynamically indexing the emulated
  x87 stack for each operation. Integer bookkeeping, float rounding points,
  memory accesses, aliases, return context and preemption points remain.
  `tools/gen_native_clip.py` proves stack depth at every reachable control-flow
  join and generates the implementation. The entry hook requires the complete
  routine's SHA-256; a changed tail byte retains the ordinary lift.
- **Scheduler:** a runnable guest invokes the existing selection logic before
  handing control to the root scheduler. If that logic selects the same guest,
  it continues directly. If it selects a peer, the root consumes that exact
  selection, preserving auto-reset event/semaphore consumption and ordering.
  Actual waits and sleeps still switch. The sampling thread now requests a
  wait-statistics dump instead of sorting/resetting guest-owned counters itself.
- **Draw preparation:** per-stage sampler control words are reused when the
  live base texture and all four sampler settings match. Live texture/palette
  validation still runs on every lookup, including streaming, missing textures
  and render-target aliases. Filter preferences are immutable during a run.
- **GPU passes:** a linked shader that writes no color and neither discards
  pixels nor replaces depth can use an embedded constant
  shader with color writes masked off. It requires no varying, sampler or
  uniform inputs; the internal depth-program path always selects those built-in
  bytes, including when ordinary shader development overrides are enabled. Geometry, depth tests/writes and query
  boundaries remain intact. Every nonzero color mask, discard-capable shader,
  depth-replacing shader and failed replacement link retains the original path.
  This eliminates texture/combiner work only in eligible depth-only draws.

Development rollback switches are `XV_NATIVE_CLIP=0`, `XV_FAST_YIELD=0`,
`XV_SAMPLER_CACHE=0`, and `XV_DEPTH_ONLY_SHADER=0`. They default to enabled;
device configuration and the user's graphics preferences are preserved.

New log records identify actual use: `[native-clip]`, `[guest-yield]`,
`[sampler-cache]`, and `[depth-only]`. They report work counts, not GPU timings
or guaranteed FPS gains. Render and engine elapsed intervals still overlap.

## Validation

The independent clipper comparison passes 12,000 cases with optimization
enabled and another 12,000 disabled, both normally and with AddressSanitizer
and UndefinedBehaviorSanitizer. Coverage includes all eight x87 TOP values,
four rounding modes, 1–512 vertices, overlapping/physically aliased buffers,
page boundaries, duplicate/nonfinite vertices, capacity limits, all context
fields and the complete 2 MiB test memory. NaN payload differences are allowed
only in output float words, consistent with C arithmetic. Each mode also
checks 3,847 preemptions, including inspection and modification of FP context
inside the preemption hook.

The final host fixture checks profiler/watch restoration after the stack probe
and at every forced preemption. A 16-vertex crossing-polygon microbenchmark
measured approximately 603 ns for the original lift and 545 ns for native
locals in one run. Other host validation was running concurrently; these are
not controlled ARM measurements or a predicted Vita FPS increase.

Production host fixtures cover scheduler selection/real waits, 100,000 threaded
completion notifications, sampler invalidation, depth-only eligibility and
failed links. Existing shader, draw-state, render profiling, frame ownership,
visibility and resolution-benchmark checks also pass.

The native build passes. The final padded executable SHA-256 is
`1712a90f11ba1718e04ccf91ce05c7eea08dde465cf65fb6289aec162395ab91`;
all three decoded SELF segments match the native and compressed builds.

The final executable runs alone in the private Vita3K instance. Blood Gulch
starts through the normal split-screen lobby; movement, turning and firing
render correctly in the captured views. The 544p/360p/544p test completes at
20.004 / 19.998 / 20.010 FPS with the same camera and restores 544p. These are
capped emulator results, not hardware performance. Pause → Leave Game returns
to the main menu. The a10 opening cutscene runs on Normal and skips to the
player's cryo-bay view; the technician and room remain visible while turning.

The six geometry samples check 80 / 80 / 79 Blood Gulch draws and
440 / 384 / 395 campaign draws, with zero changes before GPU completion.
Across the combined menu/world/campaign run, logs record 1,068,900 native clip
calls, 71,187 same-thread bypasses from 586,823 yields, 9,846,809 sampler reuses
versus 2,571,241 preparations, and 375,213 depth-only draws. These cumulative
work counts do not establish a Vita speedup or an average over a fixed route.

The 220-byte constant depth GXP has SHA-256
`9b9fef26ecb86680340b26d0f9084d27b0344270d075c01724f8004ec63d60ed`.
The binary test verifies zero varying, sampler and uniform dependencies and
no discard/depth replacement, using the
[Vita3K GXM structures](https://github.com/Vita3K/Vita3K/blob/master/vita3k/gxm/include/gxm/types.h).
Its embedded bytes match the checked program, and an executable-internal path
prevents an ordinary shader override from replacing it.

Existing Vita3K OpenGL `v_Color0` and recoverable memory-protection diagnostics
remain. This is a bounded gameplay/geometry check, not a warning-free emulator
or a complete campaign playthrough. The emulator and private Xvfb are stopped;
the private configuration is restored.

## Deployment

The host archive is
`/home/birchwoodgod/xita-backups/2026-09-06-232716-overnight-performance`.
The previous executable and 657 other files are backed up and verified on the
host. Installed at 00:45 CDT on September 7 using the existing 32,918,474-byte
executable allocation. Direct device reads and a fresh read-only mount verify
the executable SHA-256 and all 657 other files unchanged. The Vita is safely
unmounted; settings and saves are preserved. `deployment.json` records the
checks and rollback location.
Hardware FPS for this candidate remains unmeasured.
