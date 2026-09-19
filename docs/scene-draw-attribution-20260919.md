# Draw elapsed inside ordered scene passes

The crowded perf.24 log reports 36.65 ms/frame in the existing draw-HLE
counter. This is frame-wide elapsed, not a per-model attribution or GPU service
time. The previous model traversal prototypes do not explain that cost.

With `XV_SCENE_BUCKET1_DETAIL=1`, the two already timed DrawVertices and
DrawIndexedVertices HLE entry points also accumulate their completed elapsed
into an owner-written counter that is never reset by frame reporting. The scene
observer samples it at existing boundaries, without adding any per-draw clock
reads. The feature now owns `xd3d.o` as well as the scene and observer objects;
the same configuration stamp rebuilds it on feature transitions.

`[scene-bucket1-draw]` reports twelve completed-draw elapsed totals, symbol
availability and invalid intervals. Compare each total with the corresponding
`[scene-bucket1-detail]` interval. The draw total includes capture/translation,
recording, scheduling and waits inside those two APIs. It excludes other draw
entry paths not covered by their existing timing. The difference is unclassified
elapsed outside those calls, not a CPU self-time measurement or proof of a
specific material function's cost. Neither row establishes GPU execution time.

The counter is independent of the old per-frame counter's resets. An interval
with a regressing counter, regressing clock, or draw delta exceeding its wall
interval is rejected and reported invalid. Require availability 1 and invalid 0
before interpreting a window. Sampling counts completed calls; a call spanning
an observation boundary can make that attribution unsuitable and must not be
used as an exact phase decomposition. Owner admission and scene tokens remain
unchanged; the draw path itself adds no locks, waits, state changes or new clock
calls. Nested draw timings, if introduced, would also require separate handling.

Host ASan/UBSan checks exercise all twelve intervals, skipped boundaries,
partial reports, continued cumulative counting, regression/impossible-delta
rejection and disabled operation. Existing admission/context checks remain.
Production ARM transition and generated-body qualification receipts are kept
under `../scene-draw-attribution/qualification/`. This document does not claim a
hardware result or deployment; perf.24 remains the last verified installed build.

The same selected perf.24 windows report 11.12 ms draw-HLE elapsed at the
initial checkpoint versus 36.65 ms in the crowded corridor; pump elapsed rises
from 3.43 to 10.73 ms and reported draws from 152.17 to 466.5 per frame.
Dividing gives approximately 73 versus 79 microseconds of timed draw elapsed
per reported draw, but the timing and draw-count coverage are not identical.
This suggests draw volume deserves attention alongside per-draw cost; it does
not prove either an exact per-draw cost or a removable frame-time budget.
The private `frame-counters.py` records sources and window counts. These are
different workloads, not before/after optimization measurements.

Qualification completed: the sanitizer checks and 24 retained primary-CFG cases
pass at 646 callback/preemption frontiers. Six ARM transitions pass, preserving
105 unrelated objects. Disabling the feature restores the prior complete
`xd3d.o` byte for byte as well as the selected scene/observer baseline. The first
attempt used the wrong build revision label and correctly rejected unrelated
version-object changes; the successful `qualification-2/receipt.json` uses the
matching retained-build revision. Perf.25 builds successfully; its package changes
only `game-a.self` and `boot-game.txt` from perf.24.

Perf.25 (`bfec7b2+`) is now updater-verified on hardware in slot 1, with
perf.24 retained in slot 0. Runtime SHA-256:
`37c2c5205f9790ba470123e497c2a6cd5ffd7e620c3589af0b7143c06c4dbe4c`
(32,163,638 bytes). VPK SHA-256:
`0af0422ea6c05b9d85bfe5dad8c5d141ab5419f7e109ed061c9d5a45f3a7a6ac`.
The updater confirms both verification and boot. Campaign navigation uses the
saved timed sequence; deployment alone is not gameplay/performance validation.
The earlier statement about perf.24 being the last verified installed build
applies to the pre-deployment qualification stage.
