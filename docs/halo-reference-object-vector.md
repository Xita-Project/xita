# Object-vector collision reference investigation

Inspected 2026-09-26. Research lead, not an enabled optimization or an FPS claim.

References pinned for reproducibility:

- bnunu/halo-1 `d890c2db285c865b352b0de4e690da8535d77e84`,
  `source/physics/collisions.c`, `object_test_vector`.
- punpckhdq/halo `b70101a8fbced388815cb46cd1a142e9a191f7be` is the upstream.
  Its corresponding collision/render files are primarily symbol listings;
  the fork contains additional reconstructed implementations.

Both projects target build 2342; Xita's guest is retail 3925. Names and
algorithm structure are useful hypotheses, not proof of identical layouts,
calling conventions, or floating-point behavior. No external implementation
was copied into the runtime by this investigation.

## Concrete correspondence to retail 171AF0

Comparison against the private maintained `object-walk-registers/reference.c`
supports identifying 171AF0 as an object-vector collision traversal:

| Observed retail behavior | Reference role |
| --- | --- |
| Ignore-object comparison, object flags at +4 | Object eligibility filters |
| Type at +0x64, mask shift by type+8 | Requested object-type mask |
| B0CB0 with center +0x50 and radius +0x5c | Segment/sphere broad-phase rejection |
| Type-mask test followed by flag 0x400000 | Vehicle-physics branch selection |
| 81900 / 81A10 versus 172DE0 / 1731D0 | Physics-instance versus collision-model path |
| Recursive 171AF0 using link +0xc8 | Child-object traversal |
| Loop using link +0xc4 | Sibling traversal |

These matches do not establish every callee signature or result-field offset.
In particular, preserve child traversal under the eligibility/bounding-sphere
gate and sibling traversal after a rejected object. Moving those gates can
change collision behavior even if most test scenes look correct.

The fork's pinned `docs/object_matching_logs/collisions_obj_large_closeout_evidence_pass_20260919.md`
reports an exact instruction/relocation match for `object_test_vector` in its
own target and an independently checked October prototype. This is a reported
result, not a locally reproduced verification and not a retail-3925 match.
It strengthens the choice of this routine for investigation without removing
the retail differential-test requirement. Its source-provenance claims also
need review before any implementation is imported; this note imports no code.

## Next implementation gate

Use the reference to annotate the retail path and construct a native candidate
from verified retail behavior. Keep vehicle and model collision callees, result
writes, recursive ordering, nearest-hit comparisons and preemption semantics.
The existing 171AF0 differential fixture covers bounded synthetic sibling and
child cases, but real callee integration and captured gameplay cases are still
required. A reconstructed C implementation alone is not equivalence evidence.

Prior profiling attributes roughly 0.65–0.66 ms Pi self time to this routine
in firing windows, with significant additional callee work. That bounds its
standalone appeal on the Pi and does not predict Vita savings. The earlier
x87-register-only candidate did not demonstrate improvement; do not enable it
just because this source match was found.

## Hardware contact

The authenticated endpoint responded with perf258 / revision 427df56+ and
reported CPU 444 MHz. A one-hour keep-awake lease was renewed. Retrieved log:
private `effect-physical-slots/vita-online258.log` (935,557 bytes), ending in
Blood Gulch map loading. This is not a settled a30 performance measurement.
No update or restart occurred during this investigation; perf260 remains a
candidate requiring hardware validation.

## First reference-guided experiment: type-mask shifts

Private `object-walk-dead-shifts/` changes only the shifts at 171B4E and
171B88 to unsigned value shifts with count masked by 31. Both are followed
by TEST before flags are read; the first has one flag-neutral register MOV
between them. There is no intervening call/preemption. No global liveness
rule or production compiler default changed. In particular this does not
treat variable-count shifts as unconditionally killing incoming flags.

The existing differential fixture now has explicit optional `--plain-candidate`
and `--inactive-flags` modes. The latter normalizes only dormant CF/OF backing
fields at comparisons/callee-observation hashing; it preserves active overrides
and ADC/SBB carry input. Actual execution state is not normalized. Memory,
all other context fields, callee observations and preemption counts remain
compared. Strict comparison remains the default and passed its prior 1,000
register-lowering cases after this test change.

Host ASan/UBSan and Pi Cortex-A9 Thumb O2 passed 4,096 candidate cases,
19,077 candidate callee observations, all eight entry TOP values and recursion
depth two. Expanded type-mask inputs include negative types, zero and wrapped
counts. This is bounded synthetic evidence, not real-callee equivalence.
Private receipts: `host.log`, `pi.log`, `host/build.json`, `arm/build.json`.

Uninstrumented reference/candidate ARM text measured 4,156/4,096 bytes with
the same staged headers and O2 Thumb flags. The initial size compile used
mixed header roots and failed; the successful compile uses staged headers
only. Code size is not runtime speed. An identically instrumented gameplay
candidate retains the existing collision child observers and aligned B6210 /
1122A0 bodies; only the two target shift expressions change. Hardware remains
perf260; this experiment has not been packaged or deployed.

## Model-collision child: 1731D0

The pinned fork's `source/physics/collision_models.c`,
`collision_model_test_vector`, has a matching structural role: iterate model
nodes, choose a region permutation/BSP, invert each eligible node matrix,
transform the query point/vector and test that BSP against the nearest hit.
Retail 1731D0 calls B6210, B5EA0, B5E40 and 88E90 in that order, consistent
with inverse, point transform, vector transform and BSP query respectively.
This adds concrete semantic anchors for the existing child timing results.

Verified retail layout/access observations from the maintained translation:

- Instance model pointer +4, permutation array +8, matrices +0x0c.
- Model node count +0x28c, node array +0x290; node stride 0x40.
- Node region is a 16-bit field +0x20; BSP count/pointer +0x34/+0x38.
- Selected BSP stride 0x60; matrix stride 0x34.
- Crucial difference: at 173232 the retail routine loads a **byte** permutation
  and zero-extends it into AX, then compares AX against 0xffff. The reconstructed
  reference instead uses a signed short permutation and tests NONE. Therefore
  interpreting byte 0xff as NONE would alter the retail control flow: the
  observed retail path clamps 255 against the BSP count instead. Preserve the
  executable's behavior until separately proving a game bug and intended fix.

This prevents a direct source substitution. A native candidate should first
target retail node filtering/setup while retaining validated transform/BSP
callees and closest-hit order. Do not assume inverse-matrix caching is free:
the measured child is only about 0.09 ms in the instrumented Pi firing windows,
and a byte-comparison cache needs to account for animated matrices, scale,
guest aliasing and per-thread ownership. No such cache is enabled.

The two-shift gameplay run completed (session 53779, planned timeout 124,
69 reports, no scope-overflow/fatal/trap lines found). Firing-window self times
were 0.64/0.69 ms versus aligned baseline 0.65/0.66 ms; settled median self
was 0.26 ms in both. Separate instrumented runs establish no measurable gain.
Retain the private candidate as groundwork, not a demonstrated optimization.
Receipt: `object-walk-dead-shifts/gameplay/comparison.json`.
