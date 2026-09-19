# Model preparation follow-up — September 19

The perf13 Normal campaign checkpoint remains around 78.4 ms / 12.8 FPS with
the cumulative optimizations active. Its six first-scene detail intervals have
last-twelve-window medians of approximately 0.804, 0.375, 1.466, 12.007, 3.301
and 1.975 ms. The model/material interval remains substantial. These inclusive
intervals include draw recording; they are not independent removable CPU costs.

## Avoid repeating the rejected collision designs

A private whole-surface collector preserved tested outputs but increased modeled
ARM instructions by 4.75–12.24% in the two principal full-parent cases. Numeric
and alias admission walks and extra context copying outweighed body savings.
A narrower deferred publication of five provably overwritten intermediate x87
slots also lost 0.67–1.42% in its first two cases: control work and register
pressure exceeded the avoided stores. Neither candidate was integrated or sent
to hardware. The narrow candidate was stopped before broader qualification.

These are instruction-model results, not hardware cycles. They support changing
the optimization boundary, rather than deploying more variants of those same
negative-cost designs. Private reports are `world-surface-prototype/report.md`
and `world-edge-liveness-prototype/report.md` in the unified-games workspace.

## Repeated UV construction is a distinct target

The actual model/material caller repeatedly executes `56F20`, including three
waveform calls and both cosine and sine for the authored default UV descriptor.
The inspected owned a10 assets have equivalent canonical descriptors in 26 of
27 distinct examined type-4 character materials across their selected meshes.
That is asset coverage, not live call counts or a runtime speed measurement.
Adjacent complete materials generally differ, so caching a whole material is
unsupported. The narrower opportunity is exact reuse of the UV row computation.

The default 360-degree rotation cannot simply become an identity matrix: the
cosine input uses an unrounded double while sine consumes a rounded float angle.
FP status, changed time/arguments, material descriptors, original scratch writes,
output aliases and callback/reentrant boundaries must remain correct. The model
packet is stack-owned and its address can be reused; it is not an immutable
frame-global cache key. Qualification must cover the actual waveform and libm
children and the retained caller's observable state.

A private prototype passed 87 full-routine and 24 retained caller/publication
comparisons, including production shader-constant state. Modeled instructions
are 1,946 original, 2,643 for a cold cache call and 836 for a hit. This is a
positive isolated cost gate, not evidence of live reuse. The tests reseed FP
entry state; real material arithmetic can change FSW/FPSCR between calls.
Explicit owner/model-scope integration and joined live eligibility/miss counters
remain required. Dead incoming comparison bits were identified but the qualified
raw key has not been relaxed. The private report is
`model-uv-reuse-prototype/report.md`. It is not part of perf14 or a claimed FPS
gain. The separate completed-vertex-result shortcut is documented in
[completed vertex results](completed-vertex-results-20260919.md).
