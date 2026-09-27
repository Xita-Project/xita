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

## Shared physics child capture

`../physics-update-phase-pi/` adds 76 direct-call observers inside 84D40,
824B0, 83970, 81B90, 84C00 and 846E0. Each selected pre-observer body matches
retained perf269; stripping new observers restores the prior shard 013 exactly.
The already-aligned effects/native code in other functions is retained.

The ARM build succeeded. The Pi core-0/1 run completed its planned 180-second
timeout (exit 124), with 86 reports through frame 5160 and no fatal/trap or
tick-scope omission/overflow/abandonment matches in the log. Private command,
result, log and summary artifacts preserve the evidence and its limits.

| Host report | 824B0 inclusive ms | Self ms | 1721B0 ms | 82380 ms |
| --- | ---: | ---: | ---: | ---: |
| 1740 | 0.97 | 0.17 | 0.37 | 0.33 |
| 1860 | 0.80 | 0.15 | 0.33 | 0.23 |
| 1920 | 1.08 | 0.16 | 0.22 | 0.61 |

84D40 delegates essentially its whole measured interval to 824B0 in these
windows. Despite the latter's substantial translated floating-point body,
segment collision and ground-plane work outweigh its local math. A register
lowering of the entire body is therefore not the first priority on this data.

Retail 82380 initializes a plane/result record, calls 171F10 with flags C0A0,
then conditionally calls 86720, 80F00 and 940C0. The reference
`source/physics/physics.c` function `compute_ground_plane` provides a structural
anchor for this sequence. It allocates large scratch storage and may depend on
moving objects, so caching the last answer without lifetime/geometry validation
would not preserve behavior. The remaining immediate question is collection
171F10 versus plane extraction 86720 versus the other result updates.

The 171F10 collection and 1721B0 segment paths already contain native work.
Consult native-object-query.md, claude-collision-collection-20260916.md and
collision-solver-boundary-20260916.md before proposing another replacement.
Keep the physical perf269 baseline and existing physics equations unchanged
until a concrete remaining cost is selected and its replacement qualified.
All numbers are instrumented Pi observations, not hardware FPS savings.

## Ground-plane split completed

`../ground-plane-phase-pi/` adds nine direct-call observers across 82380,
86720 and its sphere/cylinder/prism test helpers (84F50, 85FE0, 85EA0).
Removing observers restores the prior shard exactly; all five selected bodies
match perf269. The ARM build and 180-second Pi core-0/1 capture completed
(exit 124), with 86 reports through frame 5160. The bounded fatal/signal/scope
error scan found no matches. This remains an older, headless diagnostic harness,
not hardware validation or proof that each scripted weapon input fired.

The report at frame 1860 exposes 82380: 0.66 ms inclusive, 0.02 self,
0.61 in collection 171F10 and 0.03 in point testing 86720. Other selected
windows omit this parent from the bounded top-cost report; absence is not zero.
Within this observed window, collection is about 92% of ground-plane cost.
Rewriting the point-testing loop first therefore has little measured scope.

The reference's collision_features_test_point explains the sphere/cylinder/prism
selection and deepest-contact output, but the capture redirects the next action
back to collection. Inspect the existing 171F10 -> 1716F0 -> 172F40 native query
coverage and its remaining traversal/memory work. Do not treat the aggregate
171F10 children across all callers as ground-plane-only timing, and do not add
another replacement for work already native. Private audit, build, command,
result, log and summary files retain this evidence. No Vita update or FPS gain
is claimed from this capture.

## Object collection versus packet construction (September 27)

The next diagnostic, `../collection-packet-phase-pi/`, times primary 172F40's
existing `nq_query_at_17301b` adapter as 88110, its transform helpers, and 868F0
packet construction. It also wraps direct calls in the three packet branches
86440, 862A0 and 86170. Twenty-four observer pairs were added; all five selected
bodies match perf269 after stripping observers, and both full source shards
restore to their prior versions. No query route, output or scheduling changed.

The ARM build and planned 180-second Pi core-0/1 run completed (exit 124),
85 reports through frame 5100; the bounded fatal/signal/scope error scan found
no matches. Older-runtime/headless and scripted-input limitations still apply.

| Report | 172F40 inclusive | Query 88110 | Packet 868F0 | Polygon branch 86170 | Capsule branch 862A0 | Sphere branch 86440 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1740 | 1.00 | 0.41 | 0.56 | 0.28 | 0.21 | 0.03 |
| 1860 | 1.11 | 0.46 | 0.59 | 0.29 | 0.21 | 0.03 |
| 1920 | 1.02 | 0.43 | 0.56 | 0.27 | 0.21 | 0.03 |

All values are instrumented Pi ms/frame, nested rather than additive.
172F40 self is 0.02/0.04/0.02; matrix inversion/point transformation is small.
Packet construction, not just the native query, is a substantial remaining
branch. Additional high-frequency observers can inflate it; do not compare
these totals with the previous less-instrumented run as a regression, or claim
these milliseconds are recoverable Vita time. The bounded report omits the
inner polygon/capsule branch rows, so individual inner leaf costs are unproven.

Next implementation target: qualify the packet construction subtree, starting
with polygon emission 86170 -> 86A40/11690/B5EA0/85020, then capsules
862A0 -> 116F0/B5E40/B5EA0/851E0. Existing native matrix helpers must remain
reused. A replacement needs owned-retail differential checks for packet caps,
count/order, transforms, plane orientation, page crossings/aliases, complete
context and preemption state, and unchanged publication timing. A frame cache
or stale collision-result reuse is not part of this proposal. No new runtime
optimization or hardware deployment resulted from the diagnostic itself.
