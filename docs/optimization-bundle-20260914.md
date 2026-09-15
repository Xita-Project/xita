# Testing optimizations together

The September 14 hardware baseline retains indexed vertex validation, the
existing GPU upload worker, native object-basis math and deferred lens flares.
These improvements are already combined. Individual measurements from different
scenes cannot be added to predict the combined frame time.

Some other candidates had small or inconclusive individual results. The remote
`prep-bundle` benchmark now tests three of them together:

| Candidate | Work changed |
| --- | --- |
| Native matrix NEON | Guarded SIMD matrix composition; unsupported values or floating-point modes use the existing path |
| Empty-object scanning | Batch empty identifiers; retain active callbacks, update order and scheduling budget |
| Texture-binding reuse | Skip identical resolved texture bindings within an uninterrupted draw range |

The comparison uses the current resolution and 60 settling plus 120 measured
frames in each of three arms: all three off, all three on, all three off.
The established vertex, upload-worker, flare and other math settings remain
configured throughout. A single drained guest boundary changes all three
members. Completion, cancellation and loss of the first-person view restore
each member's own configured value. Saved settings are never rewritten.
Unavailable native helpers or disabled native math reject the experiment.

The first question is whether the whole bundle improves complete frame time
without rendering or gameplay regressions. Repeat matched-view hardware trials;
retain inconclusive results without claiming a win. If the bundle helps, remove
one member at a time to find unnecessary work or interactions. Do not assume
three individually small changes are additive, and do not discard a measurable
bundle just because one member was previously inconclusive.

Host validation covers the production frame-acquisition/override code, all
candidate selectors, full bundle completion, cancellation and lost-view
restoration, mixed configured member values, absent helper variants, disabled
native math, and real HTTP admission/exclusion. Sanitizer checks pass.
The Vita build and unchanged updater asset contract are verified. Emulator and
physical comparisons are pending; no performance gain is claimed yet.

Private captures and exact executable/package hashes are retained under
`2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`.
