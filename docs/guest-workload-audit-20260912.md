# Guest workload follow-up — September 12, 2026

The [physical guest-phase capture](hardware-20260912-guest-phases.md) identifies
scene preparation and object updates as the next large regions to investigate.
The 48-scope candidate batches report I/O and measures their immediate children.
It is built, tested and installed with matching executable readback; the storage
anomaly described in that report prevents treating the card as ready for further
writes. Stable 20 FPS and a larger parallel workload remain unachieved.

## Deeper local experiment

A separate 42-scope build times children of the object-transform and scene
helpers in an isolated Vita3K instance. Removing the instrumentation produces
the same guest function bodies as the candidate. This private experiment was
not installed on the Vita.

Its dashboard/menu and normal solo Blood Gulch startup render. Camera movement
and walking respond. After stopping the owned emulator process, **419 complete
windows / 25,140 frames** have matching file and console records, with zero
dropped or invalid windows. An incomplete final file window is excluded.
This validates collection, not hardware speed or comprehensive stability.

Two different stationary views illustrate the distribution and its sensitivity
to visible objects. These are **emulator scheduled elapsed times**, including
instrumentation overhead, not predicted Vita savings:

| Region | First view, 300 frames: selected self ms/frame | Final view, 600 frames: selected self ms/frame |
| --- | ---: | ---: |
| Scene callback dispatcher `0x54010` | 1.948 | 1.162 |
| Scene parent `0x5D410` | 1.383 | 1.092 |
| Object callback `0x90950` | 0.660 | 0.663 |
| Object-position helper `0x8D760` | 0.494 | 0.506 |
| Matrix multiply `0xB5B40` | 0.345 | 0.327 |
| Object transforms `0x8DDF0` | 0.328 | 0.327 |

Selected self includes uninstrumented descendants. Matrix timing spans all
selected calls, not just model rendering. Neither view is a controlled
optimization comparison; the emulator's 20 FPS cap also creates parked time.

## Ownership audit

`0x900E0` walks object records and shared bitsets; `0x8FB70` invokes callbacks,
updates transforms and recurses through linked objects. `0x8DDF0` constructs
hierarchical poses: child results depend on their parent's completed transform.
Moving those entire loops onto several cores would introduce shared-state races
and ordering changes.

`0x54010` runs ordered before/draw/after callbacks. Scene helpers mutate shared
render state. They need a recorded immutable preparation stage before concurrent
work can preserve the existing submission order.

There is a narrower independent range in model rendering `0xA26B0`:

- The loop at `0xA2790..0xA27C2` multiplies each 52-byte pose matrix by that
  node's inverse-bind matrix through `0xB5B40`.
- The inverse-bind inputs are in 156-byte model-node records, starting at
  record offset `0x68`. Each product writes a separate 52-byte output in the
  caller's stack array. These products do not consume one another's outputs.
- The following code prepares LOD/material state before passing the array into
  rendering helpers. That interval is a potential overlap opportunity.

This identifies a batching candidate, not a safe completed offload. A prototype
must preserve input lifetime, mapped-memory aliases, guest register/x87 state,
floating-point rounding, and the loop's scheduler handoff behavior. It must join
before the first consumer or stack reuse. The current native matrix helper
already removes much translated arithmetic, and dispatching small batches can
cost more than it saves. Do not enable worker scheduling merely to increase
core-utilization readings.

## Next gate and evidence

September 13 follow-up ruled out another small target in the sampled scene.
A private indirect-dispatch census over 600 stationary Blood Gulch frames records
683,603 calls: 681,758 hit the existing guest cache (99.73%), and only 641 require
the HLE table search (1.07 per frame). Repeated HLE searches are avoidable, but
their frequency does not justify prioritizing a new cache over the larger scene
and object routines. These emulator counts are not hardware timings. Evidence
is in the phase-followup directory's `audit/dispatch/result-live.json`; the
instrumentation is private and the dispatch implementation is unchanged.

First resolve storage integrity sufficiently to collect the installed 48-scope
diagnostic. Rank actual Vita child costs, then prototype the largest independent
batch with a serial fallback and equivalence tests. Measure dispatch, worker,
join and whole-frame time at matched settings with timing disabled for the final
comparison. Preserve callback/update order throughout.

Private artifacts under
`/home/birchwoodgod/xita-backups/2026-09-12-222123-phase-followup`:

- `audit/deep-stage.json`: 42 targets and instrumentation-only verification.
- `audit/deep-first-live.json`, `audit/deep-final.json`: selected windows,
  complete-window validation and artifact hashes.
- `emulator/deep-xita.log`: SHA-256
  `770fc467fc644d57989887104001ef5cf56099161ce3be364296e92b6d4bb294`.
- `native-deep/build/eboot.bin`: SHA-256
  `dcc3fc6489eb15d945cb74e7e91f99c98aa35eef82d447a809833a0a13f2db4c`.

Owned-code extracts, generated functions, game assets and diagnostic packages
remain outside Git. Nothing here establishes an FPS improvement from new
gameplay parallelism.
