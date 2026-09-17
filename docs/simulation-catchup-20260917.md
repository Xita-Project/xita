# Simulation cost during slow rendering

The cumulative build's short Blood Gulch driving test reaches about 6 FPS while
still processing approximately 30 simulation ticks per second. Object work per
displayed frame therefore grows as rendering slows. Treat per-tick cost and
per-frame cost separately when deciding which optimizations can work together.

The object-job `passes` counter records accepted `900E0` updates; `109760`
invokes that update once per simulation tick. `FA920` repeats the tick through
`FAB11`, using elapsed time and the original fractional accumulator, command
frontier and mode-specific limits. The `batches` counter instead counts dispatch
and join groups: capacity 128 can split a pass, so a batch is not universally a
tick. The UI's game-time field includes waits between Presents and is not pure
CPU execution time. Lane work and lock-wait counters overlap batch elapsed time.

In the September 17 physical driving log, the 60-frame window reporting 6.1 FPS
contains 292 accepted passes and 4,344,715 microseconds of joined batch time:
**14.88 ms/pass**, or **72.41 ms/displayed frame**. A later stationary window
contains 224 passes and about **11.08 ms/pass**, or **41.35 ms/displayed frame**.
Both correspond to about 30 accepted passes per second using the rounded frame
intervals. These are different workloads, not a baseline/candidate comparison.

Reducing rendering and preparation time can reduce simulation ticks required
per displayed frame while preserving simulation speed. At 20 rendered FPS and
30 ticks/sec, the same 14.88 ms/pass would average about 22.32 ms of joined object
time per frame. This is a conditional budget calculation, not a performance
prediction: the rest of simulation, scene preparation, scheduling and GPU work
still matter. The observed 72.41 ms/frame is not an immutable serial floor.

The guest already renders through `BCB30` after the simulation loop. Whole
intermediate poses cannot simply be skipped: `8DDF0` uses the current animation
tick, attached children consume parent node matrices, and `8FB70` passes the
updated matrices through `8C570`/`90710` to object callbacks before rendering.
No exact redundant whole-pose pass was established. Keep the original tick
sequence and reduce collision/object costs within it, alongside rendering work.

The independent audit is read-only. Private evidence is under
`direct-cluster-query/tick-render-audit/`: `report.md`, `windows.json`,
`source-evidence.json` and owned-code extracts. The physical source is
`depth-store-stencil/bloodgulch-smoke.log`; loading/transition reports must not be
averaged as gameplay. No new diagnostic run or emulator was used for this audit.
