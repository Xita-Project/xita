# September 5 local rendering work

This session uses an isolated Vita3K installation at `/tmp/xita-rendering-lab`.
No physical Vita transfer or new hardware measurement was performed. User testing
and screenshots confirm visible sky, restored impact marks and working active
camo in this local candidate. The first hardware performance target remains
sustained 20 fps; the next target is 25 fps.

## Rendering changes

- Preserve raw shader IDs for captures and HUD recognition, but use canonical
  active-program keys for lookup. Inactive combiner fields and constant colors
  no longer cause equivalent programs to miss the table. The normalization comes
  from the previously reviewed fixes branch; C/Python identity and generated
  source equivalence are checked over the accumulated captures.
- Correct `PSTextureModes` from method `0x1D90` (color clear value) to `0x1E70`
  (shader stage program), including the dedicated HLE setter. Correct state
  exposed missing compiled combinations, which were added to the local package.
- Specialize cube-mode sampling for actual bound 2D textures instead of replacing
  them with a gray cube. Translate DOTPRODUCT, DOT_ST and reflection variants,
  signed dot mappings, and implicit `r0.a` initialization. Preserve blend operations
  and inverted alpha factors when linking GXM programs.
- Route Halo's backbuffer-copy quad through the captured shader/blend path.
  Offscreen copies sample the current GXM backbuffer after the preceding scene
  completes; loading-screen feedback still samples the preceding completed frame.
  Normalize linear pixel coordinates. Previously the copy read empty guest RAM,
  making camouflaged arms and weapons black even with the correct DOT shader.
- Carry all 16 persistent vertex attributes into mesh draws. Decal fade uses
  register 9, independent of diffuse and other attributes. Constant-fed attributes
  receive their correct register offsets and immutable frame-owned GPU snapshots.
  The previous default fade made marks invisible after black fallback quads were
  removed. The user subsequently confirmed decals are present.

The register and shader semantics were checked against primary implementations:
[xemu NV2A registers](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h)
and [xemu pixel shaders](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/pgraph/glsl/psh.c).
DOT_ZW remains unsupported; no claim is made that every captured texture mode or
all lighting effects are correct. Normal plasma bolts still need a clear visual
confirmation; an earlier local capture shows a charged green bolt away from the gun.

## Geometry and presentation

The user reproduced geometry spikes while looking left/right, including with
the emulator capped at 20 fps. An 80-image sequence shows world triangles
stretching or disappearing, with intact weapon geometry in those particular views.
A separate earlier image shows a black camouflaged magnum with a tall spike;
do not assume every weapon/geometry symptom has been resolved by one change.

Trace-only checks hash vertex/index data at recording and compare it after GPU
completion. Twelve camera-turn frames report changed index data in world passes;
one has 47 changed draws out of 99, with unchanged vertex buffers. Halo rebuilds
the visible triangle list while a previous recorded frame can still consume it.
The candidate retains guest indices in two 256 KB GPU-visible pools. Sequential
and rewritten quad indices already owned by the renderer keep their existing
lifetime. Exhaustion rejects the draw instead of reusing live data. Existing
frame-completion fences guard pool reuse. The user confirms the camera-turn
spikes look fixed; a new trace checked 1,011 draws over 12 frames with zero
mutations. Later Warthog-body disappearance and angle-dependent rock pop-in remain
open. The body looked correct after restart, without a specific vehicle fix.

`XV_FRAME_CAP` is optional (1–60, absent/0 disables it). It sleeps in the render
pump before frame submission, recovers from late frames without catch-up bursts,
and leaves the guest 60 Hz clock unchanged. The local configuration uses 20;
multiple gameplay windows log 20.0 fps. Initial loading/compilation can be slower.
The user finds 20 fps playable once geometry is stable. This cap tests presentation
feel on the PC, not hardware performance. An attempted external MangoHud limiter
crashed this Vita3K build at startup and was removed from the launch command.

## CPU audit and next implementation

This audit precedes the subsequent [texture worker implementation](cpu-texture-worker-20260905.md).
No game/physics worker was implemented during the rendering work.
The native render pump requests core 1; bootstrap requests core 0. Recompiled
guest fibers use a cooperative semaphore handoff, so only one executes guest code
at once. Audio/profiler workers are separate. Direct current-core IDs are now
logged even when detailed thread information is unavailable; the emulator's IDs
do not establish physical Vita affinity.

Earlier hardware windows are from the broken render-target build, so use them
only to choose investigations. Selected busy windows spend roughly 61–198 ms in
game/HLE recording, with 3.1–12.5 ms of draw HLE and 0–0.277 ms/frame of recorded
texture decoding. Decode totals in the log cover 60 frames and must be divided
by 60. Pump/wait times overlap guest work and cannot simply be added as CPU work.
Texture conversion alone is therefore unlikely to explain the whole shortfall.

Bounded candidates from the guest code audit:

| Function | Evidence and remaining constraint |
| --- | --- |
| `f_000B5B40` | Leaf matrix composition with three pointer arguments and no child calls; candidate for native math or sufficiently large batches. |
| `f_000B5F60` | Leaf four-component math/matrix conversion; exact quaternion semantics and numeric equivalence need verification. |
| `f_00088B80` | Recursive plane/node traversal with per-query context/output writes; likely spatial traversal, not a verified physics function name. Audit callees and scratch ownership before parallel queries. |
| `f_00054010` | Reads global `0x39BE58` and writes results; needs an explicit input snapshot before considering worker execution. |

The next CPU change should follow measured hot work: immutable job inputs,
private scratch/results, bounded queues, game-ordered result commits, and a serial
fallback. Compare numeric/gameplay output and frame time, including queue/wait
cost. Do not dispatch a semaphore job for every tiny matrix multiply. Moving all
physics or AI to another core without isolating shared state is not supported by
this audit. No 20 fps hardware gain is claimed.

## Validation and evidence

Native `make RECOMP=1 -j8` builds pass. Host shader/cache, frame handoff/pacing,
texture cache, immediate attributes, CPU-reporting and render-target lifecycle
checks pass. The shader identity suite most recently checked 4,368 C/Python hashes
and 2,008 source-equivalent texture variants. Current table generation has 529
fragment assets; the last incremental compile produced 12 GXPs with zero failures.
Tests exercise index source overwrites, retained frames, pool exhaustion, attribute
isolation, and the 20 fps deadline behavior. Hardware remains untested.

Durable captures and logs are stored in
`/home/birchwoodgod/xita-backups/2026-09-05-183930-rendering-local/`, including
`xita-camera-spin-20fps/`, the black-camo/magnum image and local test results.
The repository had extensive preexisting uncommitted work; this session does not
stage, commit, push, or revert that work.
