# Campaign graphics and heavy-scene follow-up

Perf.23's [query attribution](query-cost-attribution-20260919.md) puts the
selected collision-query adapter at only 3.89 ms/display frame in the original
checkpoint view. Its timing option is diagnostic, not an optimization. This
follow-up uses the same executable and normal in-game graphics panel to compare
native resolution with 360p. No remote benchmark was started.

## Same-view resolution evidence

The log confirms `XV_RENDER_HEIGHT=360 applied`, then `544 applied` on restoration.
Twelve complete stationary windows in each selected arm have the checkpoint
camera near `(-28.66, 32.52, 0.62)`, forward `(0.56, 0.82, -0.15)`. Allocation,
settling and windows bordering subsequent movement are excluded. Each selected
GPU row has 60 valid retirements, no failures/invalid samples, and a prior
negative poll for every packet.

| Selected arm | Frame ms | FPS from mean | Draws/frame | Completion lower–upper |
| --- | ---: | ---: | ---: | ---: |
| Native immediately before | 78.258 | 12.78 | 148.92 | 124.76–126.08 ms |
| 360p | 69.308 | 14.43 | 147.67 | 45.92–47.37 ms |

Completion is latency from submission start, including queue dependencies and
CPU observation delay, not GPU execution time. The frame period improves by
about 8.95 ms while completion latency drops much further. That supports a
mixed limitation in this view; a resolution-only change does not reach 20 FPS.
The comparisons are ordinary live NPC simulation, not identical frame replay.
They cannot precisely partition CPU, GPU and shared-memory bandwidth costs.

The camera moved into the adjacent combat view before the final 360p screenshot.
That image therefore does not illustrate the earlier matched windows. The
native return arm is also a different workload and is explicitly excluded from
the before/after calculation. Restoration is confirmed by the settings log and
final rendered screenshot; remote inputs were released. This is a one-way
same-view observation, not a completed matched native/360p/native trial.

## Heavy combat changes the priority

The last four native return windows are near camera `(-28.95, 36.46, 0.62)`,
forward `(-0.98, 0.21, -0.05)`, with numerous visible combatants. They average
169.38 ms / 5.90 FPS and 460 draws/frame. This differs from the earlier camera;
it is not evidence that the timing build regressed the original view.

Nearby four-window inclusive elapsed averages are:

| Scope | ms/display frame |
| --- | ---: |
| Tick driver FA920 | 62.76 |
| Accepted object passes, inside tick driver | 40.56 |
| Scene dispatcher BCB30 | 103.17 |
| First scene section, inside dispatcher | 46.81 |
| Model interval containing 5B760, inside first section | 29.34 |
| Second scene section, inside dispatcher | 43.51 |

These nested rows must not be added. Observer/report boundaries can differ;
all intervals include nested drawing, scheduling and waits. In particular,
103 ms is not an established CPU self-time cost. The scene grows substantially
more than the small selected-query path and becomes the next restructuring
priority for the actual heavy-gameplay goal.

The second scene section is the retained `5D500 -> 5D7ED` region. It contains
`5B710` (which iterates model entries through `5B4A0`), `93C00`, loops through
`7BFE0`, the ordered callback dispatcher `54010`, and `93DD0`. Earlier bounded
phase evidence also identified substantial time in `54010`; it is a useful
lead, not current per-child attribution. Next distinguish model/pass preparation
from ordered callback/draw work within this section before changing its ordering
or worker ownership. Prior material-builder and collision prototype failures
remain constraints; do not assume a new wrapper is cheaper than the retained
native/lifted mix.

Private evidence and the reproducible selected-window script are under
`../ce-perf23/campaign-resolution/` and `../ce-perf23/analyze-resolution.py`.
The summary marks the return arm noncomparable and records all selected packet
tickets. Halo 2 remains parked at the user's request. Neither stable 20 FPS
heavy gameplay nor completion of the rendering work is established.
