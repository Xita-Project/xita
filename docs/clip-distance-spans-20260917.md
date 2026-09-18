# Bounded input spans for portal clipping

The cumulative visibility work now has an optional path that resolves a point's
two coordinates and a plane's three coefficients once per straight-line distance
calculation. It removes repeated guest-page lookups and single-float copy checks
inside the native clipping region already used on hardware.

`XV_CLIP_DISTANCE_SPANS=1` requires the Halo CE profile and native clip region.
It defaults to zero. Three source intervals are pinned, covering five emitted
copies: initial-point classification, the vertex loop, and edge intersection.
Only `xk_clip_region.o` consumes the flag. Existing cumulative switches remain.

The fast branch requires each complete input range to fit in one guest page.
Unaligned loads use the runtime's alignment-safe float type. Each float-to-double
conversion, arithmetic expression and NaN operand order remains; this does not
change geometry, precision, resolution, or visibility decisions. Split-page
inputs retain the original mapped loads. Initial classification's interleaved
stack writes also remain in order, including when they overlap an input.

No pointer or value is retained across these intervals. Preemption, stack probes,
worker locks, diagnostic fallbacks, context publication and GPU submission are
unchanged. This reduces work within clipping; it does not move clipping onto a
second worker or make the original shared visibility traversal concurrent.

## Qualification before deployment

- 1,024 whole-region ARM scenarios match the original/current/fused oracles in
  complete context, guest memory, page tables, FPSCR, callbacks and counters.
- 720 focused ARM interval cases cover all rounding/flush/default-NaN modes,
  nonfinite inputs, split pages, unaligned data, physical aliases and input
  overlaps with the actual interleaved stack writes. No NaN normalization is
  used in comparison.
- 64 further whole-region cases retain state through the modeled lock/park
  callback that mutates the guest context and scratch memory.
- 256 host scenarios pass address/undefined-behavior sanitizers, including
  scheduler callbacks that change registers, scratch addresses and page mappings.
- Five real Make transitions verify default/OFF byte identity with the installed
  clipping object, reproducible ON output and no rebuild when the flag is
  unchanged. Seven invalid configuration cases are rejected. Ninety-four other
  objects remain byte-identical.

For the same 64 ordinary-mode synthetic cases, the already installed fused
region executes 1,140,945 modeled instructions; this candidate executes
1,042,199, about **8.65% fewer**. Firmware copy internals, caches, GPU work and
hardware timing are not modeled. This is not an FPS prediction or a before/after
hardware performance result.

The compiled helper grows from 17,568 to 18,098 bytes. That code-size cost is
another reason instruction counts alone cannot establish a hardware benefit.

## Installed hardware check

Source `03dd92e` built and was pushed to the private branch. All 1,588 package
members were checked: only `game-a.self` and `boot-game.txt` differ from the
preceding capture build. Ninety-four other objects are unchanged. The updater
verified and booted runtime
`175f18dae5dadeab16a970f2722d7948dd3341ef0be5cb5511a92fb387ce1225`
in slot 0; the preceding `f4fc1cd9` runtime remains in slot 1. VPK SHA-256:
`242cd85aae8a1b4c6740b63a7a6e844da4db94e459a562b1dd22b2cc094c65fd`.

Ordinary solo Blood Gulch loaded through the split-screen menus with New001.
Camera turns, forward/back movement, two AR bursts and a grenade throw completed.
Screenshots show the magazine changing 60 to 45 to 30 and grenades 4 to 3;
the sampled base interior and outdoor rock/hillside views show no obvious new
geometry defect. The 2,323,365-byte log contains 200 capture-report windows with
zero failed jobs and no matched crash markers. This is a short smoke check,
not campaign, vehicle or long-session stability qualification.

Startup retained native 960x544, triple buffering and effective clocks
444/222/222/166 MHz. Benchmark mode remained zero and remote input was released.
The final side-of-valley view at `(36.86, -66.79, .96)`, direction
`(.99, -.13, 0)`, reports 15.2 FPS with 124 draws/frame. Its visibility interval
is about 3.9 ms/frame; this is a different view and weapon from the prior
24 ms rocket-launcher view, so it does **not** establish an optimization gain.
The captured base-interior view reports roughly 13.2–13.3 FPS. Stable 20 FPS
remains unproven.

One final status request returned HTTP 408; a read-only retry succeeded without
restarting the game. The live receipt still identified the new executable.
Tests, images, logs and build receipts are in
`direct-cluster-query/clip-distance-20260917`. The tracked generators contain
transformations and fingerprints; generated game bodies remain private.
