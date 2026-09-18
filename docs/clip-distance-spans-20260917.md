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
- Five real Make transitions verify default/OFF byte identity with the installed
  clipping object, reproducible ON output and no rebuild when the flag is
  unchanged. Seven invalid configuration cases are rejected. Ninety-four other
  objects remain byte-identical.

For the same 64 ordinary-mode synthetic cases, the already installed fused
region executes 1,140,945 modeled instructions; this candidate executes
1,042,199, about **8.65% fewer**. Firmware copy internals, caches, GPU work and
hardware timing are not modeled. This is not an FPS prediction or a before/after
hardware performance result.

Tests and private build evidence are in `direct-cluster-query/clip-distance-20260917`.
The tracked generators contain transformations and fingerprints; generated game
bodies remain private. Hardware qualification and frame-time benefit are pending.
