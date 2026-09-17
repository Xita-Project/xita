# Testing optimizations together

## Current validation policy (September 17)

The comparison described below is historical. Current cumulative trials use
ordinary gameplay after an executable-hash-verified process restart, with native
resolution and standard graphics. They do not run the built-in comparison for
each change. A compiled flag, an installed executable and a helper actually
accepting gameplay work are three separate checks.

Keep compatible changes together. An inconclusive individual timing result does
not establish a regression and is eligible for a combined trial after its
correctness and ownership checks pass. A demonstrated correctness failure,
resource-lifetime violation or repeatable material slowdown needs correction
before inclusion. Unsupported or unexercised paths remain unverified, rather
than being counted as performance successes or failures.

Assess the combined build using complete frame times, pauses and worst scenes,
including effects, campaign actors and driving. Compare like scenes and settings;
do not add FPS differences from unrelated routes or count startup/loading windows.
Five additional FPS is a plausible objective, not a promised sum: 10 to 15 FPS
requires saving about 33.3 ms per frame; 15 to 20 requires about 16.7 ms. Work
removed from an overlapping, non-limiting phase may leave frame time unchanged.

The clipping-region and polygon-edge startup trials preserve the preceding
cumulative selections. Their documents distinguish host qualification, actual
hardware admissions and performance evidence. The remaining renderer ownership
and sparse vertex-residency prototypes are private until their contracts pass.

## Historical September 14 comparison

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
The Vita build and unchanged updater asset contract are verified. Vita3K
completes the combined comparison with a stable camera and no vertex-upload
failures. The on arm exercises matrix NEON and object scanning; ordinary
configuration is restored afterward.

## First physical comparison

Runtime `2ecbca0853495a5634c8789b97097b81392361125a3354ed253ffb3cb69a1a4a`
was installed through the updater and boot-confirmed in slot B. The previously
liked `7f33dee4…` build remains in slot A. Three stationary cryo-room trials
retain 640 × 360 rendering, texture maximum 128, the 30 FPS cap, indexed vertex
checks, upload worker and native object basis. Detailed phase timing stays off.
The requested 500 MHz CPU clock reads back as 444 MHz; GPU/bus are 222 MHz.

| Trial | Off before FPS | All three on FPS | Off after FPS | Saved ms/frame |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.764 | 6.750 | 6.751 | -0.167 |
| 2 | 6.781 | 6.737 | 6.778 | -0.934 |
| 3 | 6.731 | 6.792 | 6.804 | +0.540 |

Pooling exact measured elapsed times gives **6.768 FPS off and 6.760 FPS on**:
the combined arm is **0.187 ms/frame slower** on average. The small difference
and mixed directions do not establish a useful gain or a general regression.
All camera checks pass, all 28 recorded upload windows report zero failures,
and each comparison restores the configured defaults. Screenshots retain the
same room and technician; live animation continues.

The matrix helper accepts work, but the empty-object helper skips **zero**
entries in this room despite being called. That limits what this scene says
about combinations that include empty-object scanning. These counters use
60-frame windows that can straddle arm boundaries; they establish coverage,
not exact measured-arm totals.

The three options remain available and disabled in ordinary configuration.
This is one combined candidate in one workload, not evidence against stacking
optimizations generally. Further combinations should target work present in the
scene and include comparisons with individual members removed. The established
indexed-vertex, upload-worker, math and flare improvements remain combined.

Private captures and exact executable/package hashes are retained under
`2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`.
