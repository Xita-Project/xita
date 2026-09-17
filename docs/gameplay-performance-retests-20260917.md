# Gameplay performance retests

The user requested fresh Xita launches and ordinary gameplay instead of running
the built-in benchmark after each change. Recent candidates left disabled on
the strength of those comparisons need another look. Keep normal graphics
settings fixed and record the installed executable and effective startup modes.
Restarting means a new Xita process; it does not require rebooting the Vita.

## What the benchmark audit found

Claude independently audited the controller, client and saved physical logs.
The saved FPS arithmetic reproduces from the recorded elapsed times and agrees
with the ordinary frame-time reports. Object collection really ran during ON
windows, and restoration records were present. No defect in those calculations
or that mode switch was established.

The main limitation is workload control. Camera position alone does not capture
changing AI, effects, texture loading or draw counts. Short, consecutive windows
also cannot reproduce startup-only settings and persistent caches for every
kind of optimization. The collection candidate's first trial drifted; its
steadier second trial showed about 0.5% improvement in that particular view.
That does not settle its value in movement, combat, other maps or a combined
build. Existing evidence does not establish a universal noise threshold.

Object collection's mode is read on each walk and has no cache of its own, so
there is no known missing restart requirement for that test. The new collision
vertex candidate uses a startup mode for this gameplay evaluation; this is not
evidence that restarting itself improves its performance.

## Current candidate

Source `060a3c1` supports the native collision vertex pass enabled from process
startup. Its gameplay package starts with that mode ON, retains the existing
working optimizations, and leaves object collection OFF for this first launch.
No selector-44 live control is shipped. Startup and passive admission logs
identify the actual mode and execution without switching it during gameplay.
Local generated-path and concurrent cleanup checks passed; hardware speedup
remains unestablished until gameplay is compared.

## Retest queue

These implementations remain in the source; they were not deleted.

| Order | Candidate | Earlier limitation |
| ---: | --- | --- |
| 1 | Object-reference collection | Small result in one campaign view; other workloads and combinations untested. |
| 2 | Grouped vertex comparison loads | Opposing results with varying workload at 360p/reduced settings. |
| 3 | Polygon-edge math | Small savings in one view, mixed results in another. |
| 4 | Clip-wrapper fusion | No gain in tested Blood Gulch views; startup control and native stack headroom need review. |
| 5 | Earlier query-result publication | The tested heavy view had no flare waits, so it did not exercise the expected benefit. |
| 6 | Omission of read-only depth stores | Mixed results; broader ON rendering and effects checks remain necessary. |

Use separate baseline and candidate launches with the same checkpoint/map,
resolution and a comparable gameplay route. Preserve the ordinary FPS, workload
and crash logs. For an independent timing check, record the displayed-frame
counter against host monotonic time, including request latency; no in-game mode
switch is needed. Compare repeated gameplay segments before calling a small
difference a gain or regression. Then evaluate compatible changes together.

None of these retests is complete merely because a package was built or booted.
Keep known unsafe actor/solver publication experiments out of this queue, and
distinguish never-deployed prototypes from previously tested candidates.
