# Clipping-region startup in the cumulative build

The preceding twenty-two-path build compiled the exact B7F10 clipping-region
helper but never enabled it during ordinary startup. Earlier live comparisons
had mixed timing results; they did not establish a correctness failure or a
general regression. This private trial keeps the preceding choices and enables
that existing helper after a real process restart. No arithmetic is rewritten.

## Startup contract

`XV_CLIP_REGION_TRIAL=1` is explicit and defaults OFF. It requires the Halo CE
profile, native clipping region, object workers and the compiled census owner
boundary. The census itself stays OFF. Present and Swap pass their actual guest
context to a one-time helper. It waits for the initialized native recording
owner, current guest fiber, joined workers, empty queue and drained services.
It never initializes the worker pool or binds control from bootstrap/network.

Pending comparisons, census requests, watch callbacks and function tracing
defer startup. Existing initialized region control is preserved, including
an active or disabled mode. Incompatible native/register-clip configuration
declines without changing saved settings. Successful admission initializes
and enables the existing controller once and logs the selection. Original
per-call compatibility checks, fallback, guards and yielding remain intact.

This private mode rejects comparison selector 36 before modifying its request
queue: that older comparison restores OFF on completion/cancellation. Default
builds preserve its existing behavior. Other comparison selectors retain
their existing admission and compatibility rules.

## Validation and package

Six actual Make transitions verify default/OFF byte identity, repeated ON
identity and no rebuild for unchanged selections. Only `kernel/xd3d.o` and
`runtime/xv_benchmark.o` change; 92 retained objects remain identical, including
the clipping arithmetic/controllers/hooks, visibility loop, query and solver.
Independent checks cover 22 Make admission cases and 86 sanitized production
request/poll cases. Focused owner tests use the real pthread pool, actual owner
boundary and clip controller, with explicit scheduler/fiber/diagnostic doubles.
They check complete context, guest arena, mappings and FP state for declines,
one-time enable and preservation of an already initialized/active controller.

The generated native helper and B7F10/B7F50/B8000 bodies match the earlier
qualified integration. Its approximately 944-byte wrapper/native frame total
before descendants is unchanged; complete physical stack headroom remains
unmeasured. Host owner tests are not proof of Vita scheduler behavior, a full
new arithmetic qualification or crash-free gameplay.

- Runtime: `b879579db6a4fa0410c84a40823eb17ae4d3d48f76b94fdde803be7766eab912`
- VPK: `0618e57337680f696fd8147739ea11df4e504fe7984bf4eb5ec16faadcb7c692`
- Executable: 31,975,250 bytes; unchanged 1,588-member package contract.
- Only `game-a.self` and `boot-game.txt` differ from the preceding package.

Private evidence is under `clip-region-cumulative-startup` and
`clip-region-startup-audit`. Boot confirmation, effective selection and actual
region admissions are recorded below from ordinary physical gameplay. No FPS gain,
stable 20 FPS or resolution of historic intermittent crashes is established
by this package qualification. Native resolution and standard graphics remain
the baseline; the built-in comparison is not used for this cumulative trial.

## Physical admission and short gameplay check

The updater confirmed this exact runtime in slot 0 at 20:24:49 UTC on September
17. Its log records one joined-owner startup enable and nonzero clipping work.
An initial Blood Gulch report contains 1,440 regions and 10,620 inner clips over
60 frames; a later valley report contains 4,617 regions and 33,543 clips. Both
report zero capacity failures. This establishes actual execution in gameplay.

Ordinary inputs exercised movement, camera turns, plasma charging/release,
Warthog driver entry, forward movement, steering, reverse, exit and pause.
The short drive contacted a canyon wall; it is not a representative full valley
driving comparison. The screenshots show the vehicle and environment, but cannot
exclude transient rendering defects between captures. No crash was observed or
matched by the saved fault-marker search. This does not clear historic crashes.

Native 960 by 544 resolution and standard graphics were preserved. Different
views still fall below 20 FPS and cannot establish a before/after gain. Exclude
loading, menu, pause and input-boundary timing windows. The full capture is
2,827,956 bytes with SHA-256
`2772194236e38be31508a35c523ad5fc2491ca9764d4413aa4f2ff853d6fd3b9`;
the input journal, screenshots and receipt remain in the private evidence folder.
