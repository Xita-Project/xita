# Guard-retained worker query boundary — 2026-09-16

This is a production C integration and an exact-state test boundary for the
`56670..566DE` query prefix. It is **not a performance optimization**. The original
shared geometry/math guard remains held throughout capture, private computation,
validation, publication, and the unchanged `566DE..5677E` allocation/list tail.
The measured adapter is substantially slower than the original. Keep both opt-ins
OFF; integrate source for review and subsequent restructuring only. Do not include
the enabled adapter in a hardware performance candidate.

The physical light census motivating this boundary observed 12,578 worker queries
and no owner entries in 120 frames. That establishes worker ownership in that
view. It does not establish traversal cost, positive-radius frequency, depth-one
eligibility, or a benefit from this adapter. No hardware or Vita3K run was made
for this change.

## Production boundary and controls

`XV_WORKER_QUERY=1` at build time requires `XV_EXPERIMENTAL_OBJECT_JOBS=1` and adds
the adapter and hook. The runtime environment must also contain the exact string
`XV_WORKER_QUERY=1`; absent, zero, and other values decline. The ordinary build
contains no adapter object, state, hook, or counters. An OFF/ON/OFF build marker
rebuilds the affected objects and removes the old archive member on return to OFF.

The hash-checked hook runs after the existing guard at `56670`. Admission checks
the native worker identity, exact lane context/job marker, guard token, depth one,
and the lane's mapped private guest stack. Owner execution, borrowed contexts,
nested guard scopes, trace/watch mode, and unsuitable layout decline. Successful
publication jumps to the original `566DE` tail. All other cases enter the original
prefix with unchanged live query state and the original budget.

The adapter creates no pool, allocation, HLE callback, or wait. The guard's existing
acquisition acknowledges an owner pause before entry. The private path never
parks again or invokes owner service while holding unpublished state. The original
tail retains shared allocator/list ownership; it can fail allocation exactly as
before. Budget exhaustion discards private work and reaches the original worker
STOP path; it does not refill the budget or introduce cooperative guest execution.

## Bounded state and publication

Each lane owns one fixed `Query` (85,304 bytes on ARM; 85,592 on this host):

- Two copies of 17,433 mutable bytes: 16 KiB guest scratch plus 20 entry bytes,
  1,024 visited bytes, four epoch bytes, and one marker byte.
- Full entry/result `xctx`, dirty-byte bitmap, and at most twelve mutable mapping
  spans. Dirty stores are recorded even when the value equals its previous value.
- At most 512 cached 64-byte read lines, with consumed-byte masks and original
  pointer identity; 1,024 hash slots. Unconsumed neighboring bytes are not replayed
  or validated as dependencies.
- Full initial/result native FP environments, bounded recursion (256 function
  entries), and a two-million memory-operation limit. The original backedge budget
  is separately preserved. Bound exhaustion falls back before live mutation.

Capture occurs at actual query entry under the guard, so no speculative pre-entry
provenance replay is needed. Mutable mappings cannot overlap physically. Consumed
read-only input/geometry bytes cannot alias mutable scratch/global bytes. Private
stack, center/start aliases, remapped pages, image/reset-root changes, consumed
geometry changes, context changes, visited/epoch conflicts, and layout failures
discard private work. Validation currently scans the conservative mutable capture
and consumed read dependencies. Publication then copies only coalesced dirty runs,
the full result context, and the native FP environment. No fallible operation or
callback follows the first live write.

The generated prefix preserves the original instruction sequence and x87 helper
semantics. Native FP state includes prior sticky exceptions and ARM FPSCR NZCV;
restoring exception bits alone was insufficient. Nonfinite f32 loads and nonfinite
f32 stores conservatively decline, restoring the initial environment before the
original translation runs. This avoids compiler-dependent NaN operand priority.
Finite extremes, subnormals, rounding, FZ/DN, and prior sticky states are covered
by the ARM oracle below. Trap-enabled native exception handling is not validated.

There is no immutable-lifetime or lock-release proof here. The existing guard is
the serialization mechanism. Mutation injection tests the validation boundary;
it does not legitimize unsynchronized writers racing with C reads.

## Validation and limits

The host fixture uses the actual object pool, admission code, guard, owner HLE
parking, actual production hook, and generated original allocation/list tail.
The independent reference is regenerated from the supported locally owned image.

| Check | Result |
| --- | --- |
| Full context, 8 MiB memory, native FP flags; both lanes | 336 comparisons; 228 private publications, 88 FP declines, 20 capacity declines |
| Runtime disabled | 24 exact comparisons, zero private work |
| Physical input/output/scratch aliases and stack admission | 16 rejections without live query writes |
| Epoch, visited, geometry, root reset, page remap, center and context mutations | 16 rejections preserving only the injected mutation |
| Existing owner-service parking | Both lanes park/proceed correctly; owner borrowed context declines |
| Concurrent workers, removal/query/list allocation | 128 publications, exact epoch and two-light membership; other lane cannot acquire the retained guard |
| Instruction budget | Original worker STOP is preserved |
| ASan + UBSan | All seven modes pass |
| TSan | Concurrent and parking modes pass; longjmp modes blocked by installed TSan runtime |
| Negative adapters | Tests reject skipped validation, early publication, missing dirty stores, and missing budget decrement |
| Actual Makefile mode/archive rules | OFF/ON/OFF restores identical OFF guest object; unchanged mode does not rebuild it |
| Existing object-pool and hook fixtures | Pass |

The TSan limitation reproduces with an independent four-line setjmp/longjmp
program: `ThreadSanitizer: can't find longjmp buf`, followed by its interceptor
CHECK failure. It is not a full TSan pass. The normal oracle intentionally
serializes whole-arena snapshot comparisons; the separate concurrent mode runs
the real pool concurrently, with query computation still serialized by the guard.
The fixture's `private-ready visits` count includes injected validation failures;
the production `applied` counter counts successful publication only.

Arbitrary-context DF=1 cases remain in standalone original/candidate comparisons.
Repeated allocator reuse uses DF=0, as the engine does; DF=1 causes the unchanged
original allocator's backward REP STOS to damage previous nodes. Requiring DF=0
only in that repeated-engine fixture does not weaken the standalone comparison.

Both lanes have the same successful helper-recursion depth distribution in the
336-case host matrix:

| Depth ceiling | 1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Publications per lane | 80 | 10 | 3 | 0 | 10 | 0 | 0 | 11 | 0 |

Maximum accepted helper depth was 67 and maximum guest scratch depth 4,056 bytes.
These are synthetic-fixture distributions, not physical workload distributions.
The 65-cluster fixture exercises traversal beyond the later output clamp; larger
256-cluster cases can exceed the read-cache bound and use the original path.
Generated ARM static stack reports show 64 bytes per recursive `52240` frame,
72 bytes for `51E90`, and 184 for the adapter entry. The configured recursion cap
is separate from the lane's 256 KiB guest-stack allocation and native stack.

## ARM oracle and cost

The VitaSDK Cortex-A9/Thumb build passed **960 full memory/context/FPSCR
comparisons**: four rounding modes, five FZ/DN/sticky configurations, three graph
sizes, and sixteen cases per combination. Cases include nonfinite input fallback,
subnormals, finite extremes/overflow fallback, invalid start, epoch wrap, exhausted
allocator, alias rejection, and budget fallback. Original guest units use the
normal production flags; only the adapter uses `-ffp-contract=off -frounding-math`.

This is an ARM instruction oracle, not a Vita scheduler/device test. It models
worker admission in one thread; host tests cover actual pool admission and races.
SDK setjmp, native FP environment, math and arithmetic helpers execute as ARM.
Memory imports are modeled and counted separately. Numeric budget fallback uses
a modeled preempt return; the real worker STOP behavior is covered on the host.

A separate sixteen-case production-adapter run removes the test readiness
callback and reads existing publication counters out of band. With seven clusters,
nearest rounding, and the default native control state:

| Case | Original ARM instructions | Adapter + original tail/fallback | Ratio | Adapter imported copy/clear bytes | Adapter imported compare bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Positive radius, successful query | 36,261 | 306,173 | 8.44x | 45,895 | 17,793 |
| Zero radius | 1,564 | 22,596 | 14.45x | 40,212 | 17,793 |
| Exhausted allocator, successful private prefix | 30,310 | 300,242 | 9.91x | 45,727 | 17,793 |
| Infinity input, original fallback | 36,813 | 40,190 | 1.09x | 39,840 | 0 |
| Quiet NaN input, original fallback | 1,484 | 4,616 | 3.11x | 39,864 | 0 |
| Scratch/geometry alias fallback | 36,253 | 60,219 | 1.66x | 40,355 | 0 |

The successful positive original imports only 168 copy/clear bytes. These are
instruction counts, not cycles or FPS estimates. Imported memory implementations,
caches, scheduler timing, and physical service costs are excluded; bytewise
inlined dependency validation is already included in the instruction count.
The cost disproves a performance rationale for enabling this version.

Final plain production ARM adapter object: **15,450 bytes text, zero data,
170,928 bytes BSS**. BSS comprises two 85,304-byte lane objects plus 320 bytes of
counters. This excludes added hook/worker-gate code, linker padding, and referenced
library code. Compiling the same unit with the feature OFF produces zero text,
data, and BSS; ordinary archives omit the unit entirely.

## Next executable step

Replace conservative capture/replay with a compact direct query over an
**engine-owned immutable BSP read snapshot**. Establish creation, ownership, and
invalidation at drained map/BSP load, unload, and reset boundaries before releasing
the guard. The snapshot must cover cluster adjacency, portals, collision planes,
vertex arrays, and the fixed lookup/constants used by the query closure. Raw
pointer reuse and unchanged bytes in this guarded run do not prove that lifetime.

Use the exact oracle to develop a direct traversal with small private visited and
ordered result storage. Prove which guest scratch bytes are externally observable
and reconstruct their exact dirty state without copying and validating 17 KiB on
every query. Preserve FP state, traversal order, epoch wrap, capacity behavior,
budget STOP, and the original allocation/list tail. Measure that version against
the original before retaining another runtime allocation.

Then test a single worker-local sequence: briefly capture inputs/snapshot identity
under the existing guard; release only at proven depth one; compute on immutable
inputs and private outputs; reacquire, validate generation and mutable dependencies,
then publish and execute the original tail in the same ordered transaction.
Conflicts must fall back before mutation. Do not reserve a shared epoch or publish
visited bytes before commit, wait for other workers while holding the guard, or
dispatch nested work to the same occupied pool. Entry order across workers is not
currently promised; any stronger ordering scheme needs its own ownership proof.

Physical eligibility/depth and end-to-end guard occupancy must be measured only
after the compact adapter has a credible cost. This commit provides exact-state
oracles and a production boundary for that work, not evidence of parallel speedup.

### Direct-traversal handoff

The numerical inputs at actual `56670` entry are the signed 16-bit starting
cluster at `[EAX+4]`, the center pointer at `[ESP+12]` (three f32 values), and the
f32 radius at `[ESP+16]`. The original tail also needs the entry light datum at
`[ESP+4]`, light-list head location at `[ESP+8]`, and the descriptor in EDI. Keep
their original addresses and the entry context for alias checks and exact spill
reconstruction; the private traversal need not repeatedly interpret those bytes.

| Read dependency | Current source location/layout | Lifetime work needed |
| --- | --- | --- |
| BSP root and cluster array | Image `39BE58`; root `+134` count, `+138` cluster array; 104-byte clusters with `+5C/+60` adjacency count/pointer | Own/version the complete reachable cluster and adjacency data |
| Portal array | BSP `+158`; 64-byte portals, two adjacent cluster IDs, plane index, bounds, `+34/+38` vertex count/pointer | Preserve source order and own/version referenced vertices |
| Collision plane arrays | BSP `+B4`, then `+10`; independently image `39BE50`, then `+10` | Audit both roots and transitions; their apparent agreement is not a lifetime guarantee |
| Fixed projection-axis lookup and zero constant | Guest `1EAF30` (24 bytes), `1F0A68` (f32) | Verify image/mapping identity; retain exact lookup tie/sign choices |
| Mutable traversal bookkeeping | Image epoch `2D2FAC`, marker `2D2FA9`; guest 256-stamp array at `2D2FB0` | Capture the new-epoch equality predicates or stamps under guard; validate before ordered publication |

The control flow first handles invalid start and nonpositive radius. The positive
path increments the epoch, sets the traversal marker, and calls recursive `52240`.
That routine writes eligible cluster output in original adjacency order, marks
visited stamps, and examines each unvisited neighboring portal through `51E90`.
The portal predicate performs plane-distance and bounding-sphere rejection, chooses
a projection axis using `11840`, projects the portal polygon and query center,
and calls `B77C0` for the 2D polygon/radius test. Passing portals recurse. The
visited/traversal work continues after the 64 stored-output limit. Preserve exact
comparison order, strictness, f32 spill rounding, and backedge budget effects.

The numerical result is the ordered cluster output and count. Observable prefix
state additionally includes the visited/epoch/marker stores, exact dirty scratch
bytes, GPR/lazy flags/x87 state, native FPSCR, and budget decrement at `566DE`.
The original allocation/list tail remains the publication owner. A compact
implementation can use private bitsets for visited/newly visited predicates and
bounded ordered output, but cannot silently discard dirty scratch/context effects
under the existing full-state contract. The older provenance proof provides a
way to reconstruct spill bytes from actual entry registers without initial 17 KiB
capture; applying it to a direct native traversal still requires a new oracle.

The no-probe ARM sample also records disjoint instruction counts by containing
compiled function (no production timer or callback):

| Seven-cluster positive-query component | ARM instructions | Share of candidate |
| --- | ---: | ---: |
| `q_read`, `q_write`, `q_pointer`, `q_offset` memory/cache machinery | 223,847 | 73.1% |
| `xv_worker_query` boundary, including inlined capture/validation/publication/prefix | 39,715 | 13.0% |
| `q_52240`, `q_51E90`, `q_B77C0` traversal bodies | 25,333 | 8.3% |
| Dirty-count reporting helper | 6,279 | 2.1% |
| Original tail, guard model, FP environment, imports, remaining runtime | 10,999 | 3.6% |

The original's corresponding traversal bodies execute 26,774 instructions. Thus
most of the regression is adapter machinery, not extra geometric traversal.
Inlining prevents separating the boundary's copy setup, validation and publication
into truthful independent function totals. Imported copy/clear/compare **bytes**
above remain separate from these instruction counts; this is not a cycle estimate.
Compiler-identical original helper symbols can alias in the ELF, so classification
uses their function address and role, not the `ref_` spelling alone. Full per-function
counts are in private `arm-cost-breakdown/result.json`. Benchmark selector 41 is
reserved for a future viable worker-query candidate; this change adds no selector.

## Reproduction and private artifacts

Run `tools/test_worker_query.py --xbe <owned-image> --manifest <owned-manifest>
--out <private-output>` with the recompiler's Python environment. Add
`--sanitize address`, `--sanitize thread --mode concurrent --mode parking`, or
`--arm` for focused checks. After one generation, `--reuse-generated` permits
independent test builds without rewriting generated inputs. Use
`tools/test_worker_query_negative.py` with the same input arguments and
`tools/test_worker_query_build.py` for regression-negative and mode-switch checks.

Run `tools/test_arm_worker_query.py --reference <private-output>/reference.c
--out <private-arm-output>` with Unicorn/pyelftools installed; add
`--quick --no-probe` for the sixteen-case cost sample. The SDK compiler can be
selected with `ARM_CC`. The generator checks the supported owned image hash and
the instruction/helper closure before emitting anything.

Original reference C, disassembly-bearing translated includes, original-function
JSON, and extracted lookup bytes remain local/private. The three generated inputs
under `recomp/kernel` are ignored and are not part of this source change. Only
handwritten source, generators, synthetic fixtures, and this document are committed.
Private execution evidence for this review is in
`/tmp/ce-worker-query-evidence-20260916` (not a durable public artifact).
