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
of 10.733 ms and a maximum of 137.126 ms. The latter can produce a visible pause;
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
