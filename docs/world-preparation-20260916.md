# Physical world-preparation follow-up

The stationary native-resolution Blood Gulch capture on runtime
`69be4d260c111f67f7aa5e2780eb2d1c0b8c04d93f3862fe433bff7f59cb66b9`
contains three complete 60-frame windows with no invalid or dropped scopes.
All three benchmark arms use serial object callbacks. The middle arm enables
timing; 9.889 / 9.007 / 9.963 FPS measures diagnostic overhead, not an optimization.
The camera checks pass. These costs are elapsed time, including native calls;
selected self still includes descendants that have no selected scope.

| Selected routine | Calls/frame | Selected self, ms/frame |
| --- | ---: | ---: |
| Model setup `A26B0` | 35 | 13.450 |
| Recursive visibility `532E0` | 50 | 11.730 |
| Ordered scene callbacks `54010` | 10 | 11.228 |
| Light updates `92330` | 126.0 | 10.076 |
| Later render work `66510` | 28 | 4.649 |

Do not sum inclusive recursive rows or add these figures to native draw
preparation: native work is already included in the guest scopes. The flare
wrapper `60560` costs only 0.248 ms/frame inclusive in this view. Its expense in
the earlier campaign capture does not transfer to this valley scene.

## Next measurement

The 48-entry profile now separates model matrix products, material preparation,
render-state helpers, light transforms and spatial-list maintenance. It retains
the expensive parents and removes inexpensive leaf scopes. Object update, pose
and collision scopes also expose more of the tick driver's unassigned work.
No instruction body, scheduling policy, graphics setting or game state changes.
Ordinary generation without phase timing remains unchanged; tracing defaults off.

The model wrapper calls both matrix and material/submission helpers. Its entire
13.450 ms is not matrix arithmetic. The light routine calls `56670`, which changes
shared spatial lists; running the entire routine concurrently would not establish
independent ownership. Choose a native batch after separating these costs.

Host generation, runtime accounting, disabled-timer, serial-capture/restoration
and ASan/UBSan checks pass. Private stage verification proves all 32 generated
instruction bodies unchanged after removing only scope entries; the function
table changes only the 48-entry timing metadata. Hardware results for this new
selection are recorded below. Vita3K validation is omitted at
the owner's request; Claude owns that emulator work.

## Finer physical capture

The Wi-Fi updater installed and boot-confirmed runtime
`a360eda30593ef422c03096cb1ad0f2b95e7da13d8e8a74146cef24e5b22462f`.
The package preserves all 1,588 members and the existing asset contract; only
the game executable and boot record differ. Normal menus load Blood Gulch.
The new stationary view has three valid 60-frame windows, matching camera
checks and no dropped/invalid scopes. Off/on/off is 10.939 / 8.815 / 11.126 FPS:
the detailed instrumentation adds substantial overhead. This is a diagnostic,
not an improvement, and its view differs from the preceding capture.

| Selected routine | Calls/frame | Selected self, ms/frame |
| --- | ---: | ---: |
| Object pose `8DDF0` | 343.4 | 12.133 |
| Material/draw helper `70110` | 62 | 11.279 |
| Ordered scene callbacks `54010` | 10 | 10.058 |
| Polygon clipping wrapper `B7F10` | 55 | 7.495 |
| Spatial polygon test `51E90` | 1,530 | 7.213 |
| Shared matrix helper `B5B40` | 1,875.4 | 4.909 |

The recursive visibility wrapper `532E0` now retains only 0.993 ms/frame selected
self, while light update `92330` retains 1.260 ms. Their descendants account for
most of the earlier attributed cost. The matrix row spans all callers, not just
model drawing. `B7F10` repeatedly invokes the existing native clip routine;
`51E90` invokes the still-translated polygon-edge test `B77C0`. The latter has
no external callees and is a concrete next native-math target. Preserve its
float spill points, exceptional comparisons, complete guest state, mapped-memory
aliases and scheduler handoffs; compare the larger caller path on hardware.
Material helper `70110` also submits drawing, so it is not independent math
that can simply be assigned to another worker.

Before tracing, 60 ordinary frames dispatch 15,640 jobs as 8,217 / 7,423 across
the workers. Joined batch time is 33.307 ms/frame; each worker's measured lock
waiting is about 13.7 / 14.4 ms/frame, with overlapping waits. Following the
capture, multiple reports again show both worker lanes processing jobs, with no
rejections. Configured multicore scheduling is restored. These measurements
explain the next dependency investigation; core utilization is not a speedup.

## Storage and periodic hitches

The preceding runtime's 132 periodic reports have a median formatting/write cost
of 10.730 ms and a maximum of 137.126 ms. The latter can produce a visible pause;
these are report costs, not isolated SD write latency or proof that logging
explains all gameplay hitches. Screenshot-directory polling is enabled, and
`hist.now` is checked every 16 presented frames. Their costs remain unmeasured.

Map I/O currently reports accumulated counters after 32 MiB and at close, so
absence of a report cannot establish absence of gameplay reads. Two captured
32 MiB cache writes take approximately 1.8 seconds during loading. Investigate
bounded asynchronous diagnostic reports and per-frame I/O attribution separately
from the larger world-preparation work. Preserve immediate crash evidence and
avoid speculative read-ahead until file lifetime and access patterns are known.

Private evidence: `engine-restructure-20260914T2300Z/world-cost-20260916T005601Z`,
`world-children-20260916` and `physical-world-children`. Game bytes, generated code, logs and packages remain
outside Git. No new FPS improvement is established by this diagnostic.

## Follow-up: steady draw work and notification timing

The second long physical logger trial on runtime `7ba41688…` provides 29
complete 60-frame counter windows per arm after excluding each straddling
window. Native draw-HLE time is 13.421 / 13.428 / 13.424 ms/frame. Within it,
stream preparation is about 4.58 ms, indices 2.65 ms, textures 2.07 ms,
state 1.40 ms, programs 1.04 ms and constants 0.49 ms. These costs are included
in draw HLE, not additional frame costs. The earlier serial, instrumented
`70110` selected-self figure includes its draw calls and comes from a different
workload; it is not another independent 11 ms opportunity.

The enabled arm copies about 581 KiB and compares about 405 KiB of vertex data
per frame, with 112 copies and 91 exact-byte reuses. Pointer identity cannot
replace those comparisons: current-frame versions can change and generated
draw loops retain scheduler handoffs between chunks. Likewise, live texture
resolution must retain streaming, palette and render-target validation.
Native state/program preparation has a 2.43 ms total budget in this capture,
so even perfect reuse there cannot provide the roughly 50 ms reduction needed.

Observed fragment-notification retirement averages 60.936 / 61.042 / 61.127 ms
per packet in these same windows. Its start is the pump's timestamp before
CPU accounting, upload joins and GXM submission; completion is the timestamp
when the pump first sees the expected fragment-notification value. Display-slot
and frame-cap waits precede the start. The pump checks independently of guest
Present and requests 100 µs sleeps while work remains pending. Actual polling
gaps are not measured.

Thus this is **submission-to-observed-completion latency**, including CPU
submission, GPU/driver dependencies and observation delay, rather than pure
GPU execution time. Pump submission averages about 4.2 ms and overlaps guest
work. Every window has maximum pending one, no busy-slot waits and no submission
fence failures. Substantial GPU residence is plausible; the measurement is
not an artifact of waiting until the next guest Present, but neither is it
a proven 61 ms throughput floor. Cross-frame pipeline capacity at a 50 ms feed
interval remains untested.

Ordinary frame medians remain near 99.5 ms. Larger translated geometry and
object-update ownership remain the immediate CPU targets. If preparation gets
close to the 50 ms target, capture submission-end, last unsuccessful and first
successful notification observations plus maximum polling gap per packet.
Completion cadence and queue growth then distinguish a GPU throughput limit
from latency hidden behind the current slower CPU feed. Do not sum overlapping
CPU/GPU scopes or infer stable 20 FPS from either number alone.

## Later red-base valley capture

Runtime `c999c0120bf47608a2cadcacb6a9872bfece819e3eed3bba691f1c24ab670095`
provides another bounded trace after the [depth-store and resolution comparisons](backbuffer-depth-store-20260916.md#final-tail-hardware-comparison).
In this particular valley view, native/360p/native is 13.108/13.224/13.068 FPS:
GPU completion is observed substantially earlier at 360p without a material
frame-rate change. The view/settings differ from the earlier native captures;
this does not invalidate their resolution-sensitive results.

The following 360p phase capture uses serial object callbacks in all three arms,
then restores the configured worker policy. Timing Off/On/Off is
**13.621 / 10.613 / 13.690 FPS**, with matching camera checks. Three complete
60-frame windows contain no dropped or invalid scopes. The traced arm adds
about **21 ms/frame** relative to the surrounding serial arms; observer overhead
and the simulation's tick count per rendered frame limit absolute attribution.
These measurements rank investigation targets, not removable costs in ordinary
parallel gameplay. Long-lived already-open parents are still absent.

| Selected routine | Calls/frame | Selected self, ms/frame |
| --- | ---: | ---: |
| Object update `900E0` | 2.83 | 12.635 |
| Object pose `8DDF0` | 280.33 | 10.320 |
| Ordered scene callbacks `54010` | 10 | 7.119 |
| Material/draw helper `70110` | 38 | 6.979 |
| Polygon clipping wrapper `B7F10` | 80 | 6.578 |
| Spatial polygon test `51E90` | 1,292.79 | 6.015 |

The trace supports the existing object-pose, native spatial-query and world
preparation priorities. It does not make the previous slow guarded query adapter
or flat clip-region experiment worth enabling. A direct implementation must
reduce the actual traversal/math work and shared-guard occupancy, then pass the
existing original-code oracle before another hardware comparison. Material/draw
cost still includes native submission and cannot be added to draw-HLE time.
The final Off arm emits no phase windows. Subsequent ordinary reports again
show jobs on both worker lanes, with zero rejected jobs and 360p retained.
Private receipts are in
`depth-store-tail-validation/valley-serial-phases/` under the existing validation
directory; `analysis.json` retains all ranked rows and diagnostic limits.

## Native valley follow-up on the query diagnostic build

Installed runtime `b3ace3af9da6b78654d2bc0fda026b6549bbf61338b6764f6fef11bf07a86fa5`
retains the original query implementation and adds workload counters only for
the explicitly requested census. Native 544p and standard visual settings are
restored. The new phase capture again serializes object callbacks in every arm,
then restores the configured workers. Timing Off/On/Off is **9.405 / 7.453 /
9.452 FPS** with matching camera checks, three complete 60-frame timing windows
and no invalid or dropped scopes. The observer adds about 28 ms/frame; this is
not a normal parallel-gameplay cost or an optimization gain.

| Selected routine | Calls/frame | Selected self, ms/frame |
| --- | ---: | ---: |
| Object update `900E0` | 4.01 | 19.757 |
| Object pose `8DDF0` | 397.47 | 14.374 |
| Polygon clipping wrapper `B7F10` | 77 | 11.631 |
| Material/draw helper `70110` | 64 | 11.221 |
| Ordered scene callbacks `54010` | 10 | 10.198 |
| Spatial polygon test `51E90` | 1,708.02 | 8.093 |
| Matrix multiply `B5B40` | 2,161.94 | 5.680 |

Selected self time still includes uninstrumented descendants. In particular,
`54010` runs before/draw/after callbacks, so its whole value cannot be assigned
to its surface-index scan. The [bounded scan prototype](surface-scan-prototype-20260916.md)
preserves exact state in host/ARM comparisons, but short-input regressions and
unmeasured run distribution keep it uninstalled.

A later live campaign collection confirms the same installed runtime and zero
logger errors. One ordinary 60-frame window records 342 draws/frame, 35 BSP
draws/frame, 7.9 FPS and 26.7 ms/frame in draw HLE. Its draw-preparation stream
category is 15.203 ms/frame, covering source setup, inline upload work and any
remaining preparation join. It does not isolate pure memcpy time. The whole
window is live gameplay, not a controlled benchmark or a comparison to the
stationary valley. Continue the vertex preparation/submission investigation
alongside native scene work; avoid adding overlapping phase, worker and GPU
completion measurements.

Private receipts: `direct-cluster-query/world-prep-native-valley/` and
`direct-cluster-query/surface-scan-current-tail.log` in the existing validation
directory. The latter screenshot shows campaign gameplay; no update or input
was sent during that collection.
