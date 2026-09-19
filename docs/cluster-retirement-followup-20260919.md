# Spatial-query retirement hooks and remaining worker boundary

The current perf.30 holder capture confirms that `56670` remains a substantial
outer lock holder. It combines a numerical query prefix (`56670..566DE`) with
a datum allocation and linked-list publication tail. The existing typed query
prototype replaces only that prefix. Releasing the entire original function's
lock would expose shared list/allocator state; do not do that.

Added explicit typed-query invalidation at the audited primary entries of
`58440` (map reset) and `58CD0` (BSP switch), before any original instruction or
retirement callback. Previously the prototype had per-batch teardown and owner
service invalidation but no explicit hooks at these two retirement entries.
The hooks only invalidate the batch's atomic validity flag. They neither free
an in-flight snapshot nor construct a new one; existing drained batch teardown
retains responsibility for storage retirement. Ordinary builds compile the hooks
out, and perf.30 remains unchanged on hardware.

The complete audited-image check remains required by `HaloHooks`; each entry
also checks its own 16-byte signature. The owned-image hook test passed both
entries, all 32 single-byte prefix mutations, unrelated entry rejection, and
unsupported-image suppression. It verifies insertion before other entry hooks.
This is not a live map-transition or concurrent-worker proof.

Still required before enabling the typed adapter:

- Account for indirect/overlapping entry paths and writes to consumed geometry
  within a worker batch, not merely changes to root pointers.
- Select useful queries without penalizing the common tiny traversal cases.
- Measure snapshot construction plus capture, numerical work, replay, validation
  and list publication together. Earlier synthetic gains excluded real kernel
  and allocator latency; some complete small queries regressed.
- Only then shorten a depth-one query transaction using immutable geometry,
  private query/result state and validation before publication. Keep nested
  collision transactions intact until their actor writes are isolated too.

The earlier frame-snapshot buffer prototype is still not a complete game-state
snapshot. These retirement hooks advance the existing spatial-query integration;
they do not enable stale-frame AI or remove render/simulation joins.

Reproduce the owned-image boundary check with:

```sh
python tools/test_cluster_lifetime_hooks.py --xbe /path/to/default.xbe \
    --manifest /path/to/game_manifest.json
```

## Construction read-set validation follow-up

The guarded prototype now owns the exact bytes read to construct its geometry
snapshot. Before numerical work and before publication it checks both their
current guest mappings and contents. In-place geometry changes and page remaps
therefore invalidate the candidate without requiring a cooperative callback.
Source captures are bounded to 8,192 distinct spans and 1 MiB; allocation failure
or inconsistent repeated reads declines construction. Batch teardown frees them.
The existing query lock is still retained, and this is not an unlocked-worker proof.

A 1,024-bucket index replaces a linear scan when deduplicating construction reads.
Collisions compare the complete address and length. The ARM fixture now models
bounded malloc separately from zero-clearing calloc, counting both allocations.

The quick full-call ARM comparison passes 14 context, guest-memory and FPSCR
comparisons. Instruction counts (not hardware timings) are:

| Synthetic clusters | Original query | Guarded typed query | Indexed construction |
| --- | ---: | ---: | ---: |
| 7 | 37,927 | 40,986 | 14,938 |
| 30 | 174,455 | 158,745 | 57,926 |
| 65 | 380,666 | 333,486 | 123,400 |
| 256 | 1,222,594 | 1,003,773 | 482,192 |

The earlier linear read-set lookup needed 4,585,940 construction instructions for
256 clusters; indexing removes that quadratic lookup in these fixtures. It does
not establish a gameplay gain. The seven-cluster candidate still regresses and
a first-reject query uses 17,678 versus 3,128 instructions. Rechecking all source
bytes per query is conservative but expensive. Query admission and a proven
source lifetime/write boundary remain prerequisites for useful hardware trials.
Firmware bulk-copy bytes, real allocation cost, contention and cache behavior
must be considered separately. No new runtime was installed for these tests.

Private reproducible outputs: `cluster-source-arm/` (linear index),
`cluster-source-arm-indexed/` (indexed), alongside the unified source checkout.

### Private-input correction and single publication check

Follow-up review found that the read-set insertion had also reached `read_input`.
That was incorrect: private center/start inputs vary per query and are already
captured and revalidated separately. They no longer enter the BSP source read-set
or allocate memory. The ARM test now asserts zero query allocations and executes
three queries with changing centers against one geometry snapshot, checking full
memory/context/FPSCR equivalence and successful candidate admission each time.
This corrects the prototype; the earlier table includes the unwanted input work.

The entry source scan is also removed. Numerical execution uses the owned bounded
snapshot; no live geometry pointers or output writes are used during that work.
The complete live source mapping/content check remains immediately before
publication, under the retained guard. A stale snapshot can cause wasted private
calculation but its result cannot be committed. Root, input, epoch, visited and
context validation also remain. Exceptional/budget declines publish nothing.

The final quick ARM comparison passes all 14 fixtures and the three-query reuse
check. All nine ASan/UBSan worker modes pass, including unannounced geometry
mutation, remapping and in-flight invalidation. Updated complete query instruction
counts are 36,080 versus 37,927 original (7 clusters), 140,542 versus 174,455
(30), 295,053 versus 380,666 (65), and 854,872 versus 1,222,594 (256).
Construction is unchanged. The tiny first-reject case still regresses at 12,772
versus 3,128, so useful admission is still needed. No hardware FPS gain is claimed.
Outputs: `cluster-source-single-validation/` and `cluster-source-single-host/`.

### Bounded query admission

The adapter now checks up to four starting-portal bounding spheres before doing
visited capture and replay. If none intersects the query sphere it declines to
the untouched original function. Higher-degree starts are admitted immediately,
bounding this heuristic's overhead. The check reads owned geometry only and
restores the incoming floating-point environment on both admission and rejection.
Stale geometry can affect path selection, but successful publication still requires
complete source validation; rejection never changes the guest result.

The synthetic first-reject path now costs 5,134 ARM instructions including the
original fallback, down from 12,772 without admission. Original alone costs
3,128, so overhead is reduced, not eliminated. The traversing seven-cluster case
is 36,194 versus 37,927 original; thirty clusters is 140,688 versus 174,455.
Construction remains 14,938 and 57,926 instructions respectively, per batch.
These figures cannot be translated directly into FPS.

All 14 quick ARM equivalence cases and the three-query snapshot reuse sequence
pass. Nine ASan/UBSan worker modes pass. The normal fixture now verifies exactly
62 admitted queries (31 per lane) across 336 full context/memory comparisons;
six formerly admitted distant-center queries intentionally use the original.
Mutation, remapping and concurrent guard checks retain their previous coverage.
Private results: `cluster-admission-arm/` and `cluster-admission-host/`.
The prototype remains disabled on hardware; batch construction/amortization and
real query distribution must be accounted for before claiming useful savings.

### Real-map cost gate: not ready for hardware

The 24 default-FPSCR inputs per owned map all pass full context/arena/FPSCR
comparison, but all 48 are admitted: the starting-sphere heuristic does not
exclude these expensive validation/short-result cases. Before further changes,
total candidate instructions exceed original by 27% on Blood Gulch and 16% on
Battle Creek, excluding per-batch construction (125,755 and 123,710 instructions).
These synthetic distributions are not hardware workload weights, but contradict
using the long synthetic traversals as evidence of a generally faster adapter.

A two-input Blood Gulch function profile attributes 36,998 instructions to memcmp
and 10,337 to pointer translation per candidate query. The current follow-up uses
the existing grouped-word equality helper for aligned whole-word source spans;
unaligned/tail spans retain memcmp, and every captured byte/mapping is still
validated. This passes 14 synthetic ARM cases, snapshot input reuse, nine
ASan/UBSan worker modes and all 48 owned-map comparisons. It saves about four
thousand instructions on the shown Battle Creek cases, insufficient to resolve
the short-query regression. Keep the adapter disabled.

The owned-map runner now supports per-function profiling, permits deliberate
fallback admission while requiring actual candidate coverage on each map, and
asserts that queries allocate no snapshot storage. Private receipts are
`cluster-admission-owned/`, `cluster-admission-owned-profile/`,
`cluster-wordwise-owned/` and `cluster-wordwise-host/`.

Next architectural requirement: avoid scanning unrelated geometry on every
query, using a proved write/lifetime boundary or a validated per-query dependency
set. Merely accelerating numerical traversal does not pay for whole-map validation.

### Cluster dependency validation

Each captured construction span now records the clusters that depend on it.
Global root/block descriptors, axes and the zero constant remain unconditional;
unclassified spans also remain unconditional. Shared portal/plane/vertex spans
accumulate dependencies from every incident cluster. Marking is based on exact
construction reads and page splits; failure to resolve a dependency rejects the
snapshot rather than omitting a check.

Publication validates the start cluster plus every newly visited cluster. The
start is included even when already stamped with the incoming next epoch. All
incident portal descriptors, adjacency, planes and vertices are included, even
when a portal rejected traversal. This is intentionally broader than the exact
numeric read-set. Mutations outside this set may leave the query valid; a later
query that uses them checks and rejects the stale snapshot. The lock remains held.

Nine ASan/UBSan modes, 14 quick ARM comparisons, three-query snapshot reuse and
48 owned-map full-memory/context/FPSCR comparisons pass. Eight added direct
admission/publication checks cover unrelated distant vertices, incident vertices,
shared planes and start adjacency with both unstamped and already-stamped starts.
Relevant mutations decline without changing live memory, context or FPSCR.

| 24-input map sample | Original query sum | Candidate query sum | One snapshot build |
| --- | ---: | ---: | ---: |
| Blood Gulch | 2,026,609 | 1,926,257 | 183,652 |
| Battle Creek | 2,209,926 | 2,152,445 | 177,538 |

These are ARM instructions, not time. Query sums improve about 5% and 3%, but
charging one build to these 24 queries still gives net regressions of 4.1% and
5.4%. Real batch query count, allocator cost and cache behavior remain unmodeled.
The dependency table makes construction more expensive. Do not enable by default;
snapshot reuse/lifetime and cheaper dependency lookup are now the cost targets.
Private receipts: `cluster-dependencies-mutations/`, `cluster-dependencies-host/`
and `cluster-dependencies-owned/`.

### Validated reuse across drained batches

The object owner now pauses query admission after joining workers while retaining
owned geometry and dependency storage. At the next batch it may reuse that storage
only if runtime roots/arena/image identity, publication mappings, every captured
source mapping and byte, and source-stack exclusion still match. Otherwise it
frees and rebuilds. Explicit invalidation clears reuse eligibility even between
batches. Shutdown frees the cache after joining, before guest stacks are freed.
Per-query dependency validation and the shared transaction guard remain unchanged.
This does not permit queries between batches or unguarded simulation overlap.

The full scan on reuse costs 62,769 ARM instructions on Blood Gulch and 57,884
on Battle Creek, versus construction of 183,658 and 177,544. A hypothetical batch
containing the 24 sampled queries plus one successful reuse is approximately 2%
below original query instructions on Blood Gulch and effectively flat on Battle
Creek. This is not a gameplay-weighted batch or hardware prediction. A batch with
invalidation must pay construction again; real reuse rate is a key unknown.

Tests pass: nine ASan/UBSan pool modes; 14 synthetic full-state ARM cases;
three-query reuse; eight relevant/unrelated geometry mutations; and five batch
cases (unchanged, private input change, geometry change, page remap and explicit
invalidation). The latter verify reuse versus new allocations, guest memory and
FPSCR preservation. All 48 owned-map comparisons also exercise a no-allocation
batch reuse before querying. Receipts: `cluster-cache-arm/`, `cluster-cache-host/`,
`cluster-cache-owned/`. No hardware installation or FPS result yet.

### First physical candidate: perf.31

Built from the retained perf.30 stage with `XV_WORKER_QUERY=1`,
`XV_TYPED_CLUSTER_QUERY=1` and the explicit experimental
`XV_WORKER_QUERY_DEFAULT=1`. Ordinary builds still default this path off;
an explicit runtime environment value overrides the build default. The config
stamp includes the default so changing it rebuilds affected objects.
Existing native matrix packing and earlier cumulative settings are retained.

The isolated stage inserts the query call after generated local declarations
and before the original `56670` entry, retaining the original `566DE` tail and
shared guard. Checked map/BSP retirement hooks precede original retirement
instructions. The Vita build completed successfully. Package comparison verifies
only `game-a.self` and `boot-game.txt` changed; the asset/update contract matches.

The authenticated updater verified and booted slot 1. Remote status and dashboard
capture confirm `0.2.0-perf.31 / a1198f4`; perf.30 remains in slot 0. Runtime is
32,197,634 bytes, SHA256
`cd5cba3709e47f382f193c86e74cea0a3a94f497dc5e5b03c7a1872edf43026c`.
Private build, package/deployment receipts and screenshots are under
`typed-query-hardware/` beside the source checkout. The campaign navigation
sequence completed and a loading screen is visible. Active gameplay, actual
query admission/reuse and performance have not yet been established by this
installation receipt; record them separately after loading completes.

### perf.31 normal-play hardware results

Campaign loaded the retained checkpoint and completed the existing movement /
combat input sequence to the crowded corridor. Captures show world geometry,
NPCs, weapon and HUD. No crash was observed during these captures; this is not a
complete combat/rendering regression pass.

Six 60-frame windows per view:

| View | perf.30 frame ms | perf.31 frame ms | perf.31 model packets / draw subset ms |
| --- | ---: | ---: | ---: |
| Checkpoint | 78.77 | 78.20 | 9.112 / 4.273 |
| Crowded corridor | 170.87 | 168.97 | 23.857 / 11.009 |

Checkpoint camera matches (-28.66,32.52,.62), direction (.56,.82,-.15), but model
calls differ (7.50 versus 7.70/frame). Corridor camera differs slightly:
perf.30 (-27.52,37.09,.62), direction (-.93,-.34,-.15); perf.31
(-27.56,37.07,.62), direction (-.89,-.43,-.15). Model calls decrease from
26.07 to 23.36/frame. Neither comparison establishes a causal performance gain.
The heavy scene remains about 5.9 FPS, far below the 20 FPS objective.

The latest checkpoint query report contains 2,380 applied queries, 123 reused
batches and no rebuilds or declines. The latest corridor report contains 4,341
applied queries, 1,728 bypasses, one changed-state decline, 116 reused batches,
seven successful builds taking 3,380 us total and seven owner-service
invalidations (last 0x1D665C). Source changes and build failures are zero. These
are report-window totals, not per-frame values or independent whole-frame costs.
This confirms runtime activation and successful cache reuse on hardware.

Private captures: `typed-query-hardware/checkpoint-settled.log`, its summary,
`campaign-first.png`, and `corridor/settled.log`, summary and screenshot.
The next concurrency boundary to investigate is only private query calculation
between guarded capture and guarded validation/publication. The original list
allocation tail and outer actor/collision transactions must remain protected.
Current perf.31 still retains the guard throughout; it does not test overlap.

### Private query overlap prototype (not installed)

`XV_QUERY_OVERLAP=1` in the runtime environment enables an additional typed-only
experiment; it defaults off. The adapter captures entry/input/visited state
under the original depth-one guard, borrows the immutable snapshot retained
through worker join, releases the guard for private replay, then reacquires via
the existing park-aware lock path. Replay now explicitly reads the captured
entry context, not the live worker context. No live geometry, list allocator,
owner callback or publication occurs in the unlocked section.

Both successful and declined numerical calculations reacquire before returning.
The result FP environment is captured before reacquisition, and the entry FP
environment is restored afterward. Existing source/epoch/visited/input/context
validation decides publication; a competing query can cause fallback to original
execution under the guard. No epoch rebasing is attempted. Nested scopes and
active hold sampling cannot suspend. The caller retains its original cleanup
token and restored lock depth through the unchanged allocation/list tail.

All nine existing ASan/UBSan worker modes pass with overlap enabled. A new mode
uses a barrier to require both workers to reach the unlocked phase before either
resumes, across 64 pairs / 128 queries. It verifies final epoch and both lights'
list membership, not complete whole-world equivalence. The normal modes retain
full context/arena/FP comparisons. The overlap, owner-parking and in-flight
invalidation modes also pass TSan; Vita ARM compilation passes. This establishes
the tested ownership/synchronization mechanics, not real performance or broad
gameplay safety. Perf.31 on the Vita still holds the guard during calculation.
Private receipts: `cluster-overlap-host/`, `cluster-overlap-pairs/`,
`cluster-overlap-tsan/` and `cluster-overlap-compile/`.
