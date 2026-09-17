# Typed collision traversal experiment

This default-OFF component replaces the scalar decisions in both primary BSP
traversals, `87EA0` and `87E10`, plus projection/axis preparation
`87FE7–880DF`. It retains the existing object transaction, `88110` wrapper,
`86F50` visitor, vertex-ring helper, segment/sphere helper, child/recursive
calls, output order, capacity checks, ancestor scans and independent interior
entries. No map snapshot, retained pointer, worker, live selector or ownership
change is introduced. The original emitted implementation remains available.

The earlier register-localization experiment is rejected for cost and preserved
in commit `c779af7` and private `E/native-collision-traversal/receipt-localization.json`.
The current implementation replaces that transform; it is not an endorsement of
the earlier code. Instruction savings below do not establish Vita frame savings.

## Exact boundaries and fallback

* `87EE6–87F0D`: consume the existing qualified plane-distance result and derive
  the two 3D decisions directly. Reconstruct partial EAX/DL, x87 stack/status
  and lazy flags, then resume the original `87F94`, `87F0F` or `87F9B` label.
  The unconditional `87F96` preempt still occurs before child selection from AL.
* `87E20–87E67`: capture finite 2D operands, calculate in original multiply/add/
  subtract order, reconstruct both comparisons, stale x87 slots, TOP/status,
  partial GPRs and final flag fields. A generated callback-free native block
  contains this path and its exact original emitted fallback. It returns before
  `87E68`; no child pointer is gathered early.
* `87FE7–880DF`: prove physical separation of stack output `ESP+14..+1f` and
  query output `query+21c..+227` from each other and every later-read plane,
  center, reference, lookup and zero-constant span. Captured/global mapping roots
  must match, all relevant spans must fit one page, operands must be finite,
  normals must be normal/zero, the zero constant must be unchanged, and selected
  lookup indices must be in `[0,2]`. Otherwise the original sequence executes
  before any changed effect. Accepted stores keep their original order.

All five original traversal backedges remain at `87E7F`, `87F18`, `87F8D`,
`87F96` and `880F6`, including their taken targets and budget effects. The helper
regions contain no yield, guest call or recursive call. Full state is already
materialized before every such boundary. Mapping roots are never retained
across a new boundary; original captured integer mappings versus global x87
loads and stack pops remain distinct.

Two emitter details are intentionally preserved: `x87_compare` ORs existing TOP
bits rather than clearing them, and emitted NEG/SBB flag handling makes this
projection's orientation depend on the selected normal, despite the reference
sign load. This component does not repair either behavior. ARM double arithmetic
uses explicit VMUL/VADD/VSUB order. Exception-enabled FPSCR modes decline before
native computation. NaN, infinity, subnormal-normal, mapped-alias and unusual
lookup cases retain original fallback, with a measurable admission cost.

## Native stack contract

The rejected all-inline typed version enlarged recursive frames from 72 to
256 bytes (`87EA0`) and 40 to 168 (`87E10`). A follow-up corrected the original
stack-capacity premise: the 32 KiB stacks belong to the mock scheduler.
Recompiled CE guest threads request 512 KiB in `xk_thread.c`, passed unchanged
to native SCE thread creation by `xk_os_vita.c`; object workers also request
512 KiB. The larger frames are not a demonstrated capacity failure on that
basis. Keeping recursive frames small remains useful, and requested capacity
alone does not bound the complete live caller/recursive chain.

Current temporary storage is in nonrecursive blocks, released before every
recursive child or preempt. The 2D recursive shell contains no FP operation
when the feature is compiled. ARM `general-regs-only` and local `Os` prevent GCC
from hoisting integer flag-store constants into saved VFP registers. The exact
ARM fixture reports 72 bytes for `87EA0` (original 72), 32 for `87E10` (original
40), 80 for the 2D decision/fallback block, and 208 for projection. The wrapper
is unchanged at 40 bytes. These are compiler stack-use records, not a global
bound on all game call chains. Recheck the actual retained code unit's `.su`
after integration: different flags, instrumentation or compilation units may
change allocation. No shallow-map assumption is used to justify recursive
frame growth.

## Qualification and cost evidence

Private evidence lives at `E/native-collision-traversal`. The owned image SHA
and both instruction-span hashes are verified before generation. Raw original
bodies remain private. `stage-oracle-identity.json` records equality with the
previous staged originals for all five functions after removing only the known
plane-distance hook. The differential reference executes the cumulative
original traversal plus current vertex/segment/plane helpers; raw bodies are
emitted for identity checking, not executed as a third lane. Collection lies
outside `88110` and is unchanged, not exercised here.

The oracle compares complete xctx, all 8 MiB of synthetic guest memory, both
mapping tables/root choice, actual child-entry observations, all taken-preempt
observations/budgets, host exception status and complete ARM FPSCR. Fixtures
include full `88110` calls, direct entries, recursion and repeated surfaces,
empty/full outputs, all axis/tie/sign cases, mixed reference signs/high bits,
CF/OF override fields, x87 TOPs, four rounding modes, FZ/DN/status bits,
QNaN/SNaN/infinity/subnormal fallback, invalid lookups, physical aliases and
page-splitting inputs. Callback mutations include flags, x87 stack, geometry,
page mappings, mapping-root replacement and already-selected continuation data.

Final receipt names and results are in `receipt-typed.json`. Host ASan/UBSan
passes 256 comparisons with 211,078 matched yield observations across all five
sites. Strict ARM suites cover 252 standard and 113 axis/alias/FP cases, plus
separate complete-call cost cases. Negative controls show that dropping the
physical overlap proof corrupts full context, dropping TOP reconstruction
changes entry observations, and restoring pre-yield AL changes the selected
continuation. Feature-OFF preprocessing is byte-identical to the original
baseline, wrong spans decline, duplicate transforms reject, and interior
entries remain untouched. Real Makefile fixtures verify feature/default
transitions, archive membership, target-only defaults, header dependencies,
no-op builds and invalid defaults.

Cost runs use exact production-shaped bodies without entry/site probes. They
count ARM instructions including complete wrappers, real visitors and current
helpers, not CPU cycles, guard hold time or FPS. Final ON examples:

| Complete workload | Original | Typed ON | Change |
|---|---:|---:|---:|
| Full `88110`, 128 3D nodes, empty result | 39,924 | 36,598 | -8.33% |
| Direct 2D query, 128 nodes and final surface | 40,950 | 40,594 | -0.87% |
| Full `88110`, 128 one-way 2D nodes | 43,364 | 42,925 | -1.01% |
| Full `88110`, one leaf/surface | 14,735 | 14,706 | -0.20% |
| Full `88110`, 16 levels, one-surface work | 249,920 | 249,021 | -0.36% |
| Full `88110`, 16 levels, four-surface work | 689,785 | 691,558 | +0.26% |
| One direct 2D node, surface work | 12,307 | 12,362 | +0.45% |

The receipt also includes full-wrapper node-heavy 2D cases and compiled-ON /
startup-OFF costs. A disabled compiled feature has branch/codegen overhead;
only feature-compiled-OFF is the identical baseline. Tiny queries and fallback-
heavy exceptional inputs can lose; do not promote just the best node-heavy
case as an aggregate speedup. Real proportions of node, surface and fallback
work are still unknown. No hardware, live benchmark, deployment, full package
build or multiprocessor scheduling test was performed in this worktree.
In particular, compiled-ON/startup-OFF costs 46,994 versus 40,950 instructions
for the direct 128-node 2D query (+14.76%), and 49,420 versus 43,364 for its
full-wrapper counterpart (+13.97%). Keep the feature compiled OFF when it is
not the selected experiment; runtime-OFF is not a free baseline. Native trap
delivery with exceptions enabled is not modeled by these tests.

## Reproduction and integration

Both repository defaults remain OFF. Candidate startup selection is immutable:
`XV_NATIVE_COLLISION_TRAVERSAL=1 XV_NATIVE_COLLISION_TRAVERSAL_DEFAULT=1`.
The control exposes only `const unsigned xv_collision_traversal_mode` and the
passive `xv_collision_traversal_enabled()` getter. There is no owner init,
override, mutable counter or per-call thread lookup. A startup-only change
rebuilds control/archive; feature transitions rebuild hooked units/archive.
No main/controller change is part of this experiment.

```
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/host --sanitize \
  --entry-observers --alias-probes
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/arm --arm \
  --entry-observers --alias-probes
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/axes --arm \
  --arm-suite axes --entry-observers --alias-probes
python tools/test_collision_traversal.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --out /private/cost --arm \
  --arm-limit 44 --no-site-trace
python tools/test_collision_traversal_build.py --output-dir /private/make
```

Use `--arm-suite node2-full-oneway` for a full-wrapper 2D node chain and
`--arm-suite node2-full` for repeated two-child visits; `--default 0` selects
the immutable OFF control. Negative-control options intentionally fail.
Python `-O` is rejected. ARM validation uses the existing instruction fixture,
not Vita3K or a physical device.

For selective integration, carry the net source changes from base `1823407`
through the typed commit, preserving newer unrelated startup/runtime work.
Regenerate only the two primary bodies in the retained `code_013.c` with the
current full discovery/symbol set and profile hooks, or apply the qualified
`collision_traversal.hook` to their existing complete emitted bodies after the
plane-distance transform. Include the generated static 2D block adjacent to
its primary body. Keep the code-unit prologue, other functions and interior
entries. Do not replace the unit with the test oracle's limited discovery.
Stage the header/control, runtime source list and Makefile wiring. Verify actual
production `.su`, linked flags, original/fallback availability and cumulative
startup settings before any fresh-launch testing; the private fixture is not
a linked-game stack/correctness or frame-time receipt.

## Cumulative production integration

The net typed changes are integrated into the six-path gameplay source without
the rejected localization implementation. Both retained primary bodies match
the originals used by the audit. Applying the transform to those two bodies
leaves the rest of `code_013.c` unchanged, including independent entries and
the existing plane-distance hook. The integrated production-Makefile fixture
passes. A single startup message uses the optional mode getter; it adds no
per-query counters and does not establish hardware admission counts.

The full Vita build with startup mode one passes. Actual production stack-use
records confirm 3D recursion remains 72 bytes, 2D recursion falls from 40 to
32 bytes, and the wrapper remains 40 bytes. The nonrecursive blocks use 80 and
208 bytes. These match the fixture; this is still not a bound on every complete
game call chain.

Only `code_013.o` and `main.o` differ from the preceding six-path build, plus
the added traversal control object. Native helper regeneration produced
byte-identical source and objects. The private parent stage initially lacked
the existing `segment_sphere.py` module needed by the updated hook imports;
restoring that source allowed the resumed build to finish. The failure and
correction are retained in the private build receipts.

The package changes only `game-a.self` and `boot-game.txt`; its launcher/assets
contract matches the installed updater. Linked data confirms traversal mode
one. All six preceding startup selections remain in the build command, so
this is a seven-path cumulative candidate. Hardware performance and stability
are not established by these build checks. Evidence is under
`direct-cluster-query/collision-traversal-startup/`.

## Fresh-launch hardware follow-up

The seven-path runtime `fae0f1808a892ca9caf3e4a3692aaabf2fd307bda4cff72171dc17c504bf9730`
from source `c5a198a` is installed and boot-confirmed in updater slot 1. Slot 0
retains the preceding six-path runtime. Startup confirms traversal mode one;
there are no per-query counters, so the actual admission/depth distribution
is not measured. Native resolution, Original settings and effective 444/222 MHz
CPU/GPU clocks remain the comparison baseline.

Normal campaign menus restored New001's saved pistol checkpoint on Pillar of
Autumn, Normal difficulty. The profile, save directory and logged camera match
the preceding six-path observation. The ordinary displayed-frame counter
advanced 765 frames in 59.9916 seconds, or 12.7518 FPS, compared with 12.7725 FPS
for the six-path parent. This difference is **inconclusive**: live AI and draw
work vary, and endpoint timing bounds do not bound workload variation. All
seven paths remain enabled in the cumulative candidate; no speedup is claimed.

Two pistol fire presses, camera movement, forward movement and pause completed
without a detected stop or GPU fault. Screenshots show the weapon and world
still rendering. This short check does not establish long-session stability or
resolve the historical rocket/plasma/vehicle crashes. Installation, observation
and smoke receipts are retained in the private evidence directory above.
