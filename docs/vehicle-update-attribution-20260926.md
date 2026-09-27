# Vehicle update attribution

Perf269 slow-frame evidence points before present; the follow-up Pi owner
profile identified callback 39450 beneath 90950. Previous worker faults under
39450 involved both sound-cache yielding (95680 -> 10D920 -> 26B10 -> 32B00)
and deep physics stacks (86A40). See parallel-object-experiment-20260914.md.
Those findings prohibit assuming this callback can simply move to another core.

Private `../vehicle-child-phase-pi/` adds observers at 27 direct call sites
inside 39450. Removing observers restores the exact retained perf269 function;
all other functions in shard 007 remain unchanged. The harness uses the earlier
aligned native transform/effects objects. It does not include the experimental
1731D0 predicate candidate, change worker policy, or alter simulation behavior.
Build, audit, command, terminal result, full log and summary are retained there.

Pi cores 0/1, a30 scripted campaign/firing sequence: the 180-second job completed
with intended timeout exit 124, 87 reports through frame 5220. No fatal/trap or
tick-scope omission/overflow/abandonment matches were found. This is headless,
instrumented supporting attribution, not a Vita frame-rate or visual result.
The second input burst's actual firing remains unverified, as in the preceding
capture; do not infer weapon behavior solely from scheduled inputs.

| Host report | 39450 inclusive ms | Self ms | Child 37F80 ms |
| --- | ---: | ---: | ---: |
| 1740 | 0.98 | 0.02 | 0.93 |
| 1860 | 0.65 | 0.02 | 0.60 |
| 1920 | 1.19 | 0.02 | 1.14 |

These nested timings identify 37F80 as 92–96% of this callback's measured cost
in these windows. Optimizing the outer callback alone has little scope here.
They do not establish 37F80's cost on the Vita or identify its own slow child.

## Reference-guided next boundary

The pinned bnunu reference `source/units/vehicles.c`,
`update_alien_fighter_physics`, selects old/new physics using physics radius
and then invokes `create_ghost_effect`. The retail 37F80 likewise resolves a
physics tag, compares its first float against zero, calls 37760 or 37AB0, and
then calls 36D20. This is a structural correspondence, not a build/layout match
or permission to copy its implementation. The reference's vehicle update also
already has an at-rest gate; a blanket stationary-vehicle skip is not justified.

Inspect/time the retail children separately before implementing a replacement:

- 37760 and 37AB0: alternative physics paths with math and 84D40 calls.
- 36D20: calls 8D650, B5350, collision-vector 1721B0 and effect creation 112070.
  Thus the apparent flying-vehicle physics cost could actually be its attached
  effect/collision work. Do not assume all of the measured time is physics.

No source runtime policy or installed package changed in this investigation.
Perf269 remains the hardware baseline, with its keep-awake lease renewed.
The next concrete action is to isolate these three children, then target the
one actually responsible rather than repeat the small predicate experiment.

## Physics/effect split completed

`../vehicle-physics-phase-pi/` adds 82 direct-call observers across 37F80,
37760, 37AB0 and 36D20, retaining the previous 39450 scopes. Removing the new
observers restores the original shard 006 exactly; each selected body also
matches retained perf269. No candidate predicates or runtime policy changed.
The full ARM harness built and ran on Pi cores 0/1 for the planned 180 seconds
(exit 124), producing 87 reports through frame 5220. The saved log contains no
fatal/trap or tick-scope omission/overflow/abandonment matches. The same headless,
older-runtime and input-versus-action qualifications above remain applicable.

| Host report | 37F80 ms | 37760 ms | 84D40 beneath 37760 ms | 36D20 ms |
| --- | ---: | ---: | ---: | ---: |
| 1740 | 0.96 | 0.96 | 0.91 | 0.00 |
| 1860 | 0.76 | 0.76 | 0.72 | 0.00 |
| 1920 | 1.12 | 1.12 | 1.08 | 0.00 |

These rounded measurements put 95–96% of the selected old-physics branch under
84D40. A displayed 0.00 is below report precision, not proof of zero work.
The new-physics branch 37AB0 is not established as active by these windows.
The attached effect path is not the major cost in this sample.

The reference `update_alien_fighter_physics_old` finishes with `physics_update`;
retail 37760's corresponding call is 84D40. Its maintained translation calls
824B0, 81900, the existing quaternion helper B5F60, 83970, 81B90 and 84C00.
This narrows the next implementation boundary to shared physics update and
its children rather than vehicle steering math or ghost-effect suppression.
Existing quaternion work must not be counted as a new replacement opportunity.

Artifacts include `build.log`, `audit.json`, `run-command.json`, `run-result.json`,
`run.log` and `summary.json`. Hardware still runs perf269; no FPS improvement,
new hardware stability result or physics change is claimed from this diagnostic.
