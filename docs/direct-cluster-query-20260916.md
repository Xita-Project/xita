# Direct spatial query prototype

The new `xk_cluster_query.c` replaces the numerical part of Halo 3925's
`56670..566DE` spatial query with a typed traversal. An experimental guarded
runtime now connects it to the existing guest hook and worker pool. It is
**disabled by default and uninstalled**; there is no live unlock. A separate
[original-query diagnostic](query-work-census-20260916.md) is now installed and
measured on hardware; that executable does not contain this adapter.
The [earlier guarded adapter](worker-query-adapter-20260916.md) remains off.
This is a candidate for reducing measured world/light preparation, not a
demonstrated frame-rate improvement.

The [latest complete-call map tests](query-adapter-map-cost-20260916.md) reduce
visited-stamp capture to the BSP size and cover actual Blood Gulch/Battle Creek
geometry. Multi-cluster queries improve in instruction counts, but some small
queries still regress; selective admission and the lifetime audit remain open.

The [latest physical valley comparison](world-preparation-20260916.md#later-red-base-valley-capture)
barely changes FPS with resolution despite earlier observed GPU completion.
The phase trace identifies spatial polygon tests among the larger CPU targets.
Other views remain resolution-sensitive. The user's 20 FPS reports in Battle
Creek and Blood Gulch caves/bases are useful workload comparisons, not evidence
that this uninstalled routine helped them.

## Implementation

The query consumes validated immutable clusters, ordered portal adjacency,
portal vertices and two plane arrays. Iterative traversal uses a private stack,
visited bitset and ordered result list. It preserves first-64 output storage
while continuing to count and visit the remaining clusters, original traversal
order, epoch wrap and exact backedge budget consumption. Exceptional inputs,
overflow and exhausted budgets decline without changing guest memory.

The numerical path preserves the original double-operation order and explicit
float spill points. It includes separate distance/projection planes, dominant
axis ties, degenerate polygon edges and strict boundary comparisons. It is not
compiled with fast-math. Native floating-point traps must be disabled.

`xv_cluster_geometry_valid` checks bounds, signed portal indices, adjacency,
plane indices, finite geometry and the original 128-vertex scratch limit.
Callers must own allocations covering their stated lengths, validate once, and
keep every array immutable during a query. This API does not validate arbitrary
live guest pointers or establish synchronization by itself.

## Validation and cost

The numerical reference is independently generated from the locally owned,
hash-checked Halo 3925 image. Generated original code and map assets stay in
private test outputs. No original executable or map bytes are committed.

| Check | Result |
| --- | --- |
| Host ASan/UBSan, four rounding modes | 4,096 exact numerical comparisons |
| Large graph and budget cases | 296 traversals exceed output storage; 2,256 budget fallbacks |
| VitaSDK Cortex-A9 instruction oracle | 440 cases: 350 exact admitted results, 90 conservative declines |
| ARM rounding, FZ/DN and prior sticky controls | Ordinary coverage retained in every tested mode |
| Guest writes by ARM candidate | None across the full 8 MiB fixture |
| Owned-map UBSan comparison | 26,624 exact results, zero declines, across 13 BSPs |

The map test reads the original, unmodified BSP bytes through guest mappings.
The candidate uses separately decoded owned arrays with only referenced planes.
It covers Blood Gulch, Battle Creek (`beavercreek.map`), all nine Pillar of Autumn
BSPs, and both Halo BSPs. Inputs include both portal sides, portal centers and
nearby points, radii from zero through 1,000, epoch wrap and previsited clusters,
under four rounding modes. At least one test visits every cluster in each BSP.
This establishes numerical agreement for these inputs, not live map-transition
or rendering correctness.

The compact map arrays occupy **11,688 bytes for Blood Gulch**, **8,352 for Battle
Creek**, and at most **15,996 bytes** across this campaign sample. These figures
exclude the descriptor, allocator overhead and any second live snapshot.
The two plane roots share storage in this map fixture; synthetic tests cover
distinct roots. The C constructor below is now tested against the independent
offline decoder, but neither is registered with live game loading yet.

Ordinary synthetic traversals with nearest rounding and default native controls:

| Clusters | Original numerical prefix, ARM instructions | Typed query | Reduction factor |
| ---: | ---: | ---: | ---: |
| 7 | 27,275 | 3,971 | 6.87x |
| 31 | 134,147 | 19,331 | 6.94x |
| 65 | 285,507 | 41,090 | 6.95x |
| 256 | 1,131,637 | 163,139 | 6.94x |

These are instruction counts, not cycles or FPS. Modeled libc work is excluded:
the positive typed query clears/copies 212 bytes; this original prefix imports
none. Snapshot construction, locking, input capture, result publication and the
unchanged allocator/list tail are outside both measured kernels. The native ARM
object is 2,028 bytes of text, no data/BSS, with a reported 2,160-byte static query
stack and 48-byte validator stack. This excludes referenced library routines.

Two preliminary harness failures were corrected: an inline no-op memory-import
stub invalidated fixture initialization, and a Python reader assumed a contiguous
visited array across permuted guest pages. Memory stubs now use a separate
translation unit; the reader handles page boundaries and verifies its input
against the prepared visited array. The completed ARM matrix uses both fixes.

## Owned C snapshots and retirement

`xk_cluster_snapshot.c` constructs compact owned arrays through a caller-supplied
bounded reader. It copies cluster adjacency, portal data, vertices, axis lookup
and only referenced planes. Plane indices are compacted without changing portal
or adjacency order. Distinct distance/projection roots remain distinct; matching
roots share storage. No source or guest pointer remains in the query descriptor.
All allocation/read/validation failures discard the incomplete snapshot.

The caller must keep one source generation stable for the entire construction.
The reader contract is not itself a lock, mapping validator or live guest adapter.
`max_bytes` limits the two owned allocations; allocator overhead and libc sorting
scratch are additional. A successful constructor is not permission to capture
concurrently changing guest structures.

The store transfers one reference on publication. Reader leases pin immutable
arrays while computations run outside the caller's mutex. Retirement removes the
current snapshot immediately; outstanding leases remain valid until released.
Publication checks compare both snapshot identity and generation. Old snapshots
cannot be republished, and generation exhaustion rejects future publication.
Every shared-store/refcount operation requires the **same external mutex**.
This mechanism does not discover map changes or automatically acquire Halo's guard.

Validation now includes:

- ASan/UBSan construction, every one of 23 reader failure points, both owned
  allocation failures, malformed bounds, separate planes, exact storage limits
  and empty geometry, with no outstanding allocations.
- Retirement with two outstanding readers, followed by overwriting source bytes;
  both readers still produce the expected result while stale publication checks fail.
- Two concurrent readers through 2,000 publications and repeated retirement;
  both ASan/UBSan and TSan pass. This exercises the snapshot API under a host
  mutex, not the production object pool or live BSP transitions.
- Another 26,624 exact owned-map comparisons using the **C constructor**.
  Its decoded geometry is also checked against the independent Python decoder.
- VitaSDK Cortex-A9 compilation with warnings treated as errors: 1,724 bytes
  of text, no static data/BSS, and 232 bytes of reported constructor stack,
  excluding allocator/sort callees. This is a compile check, not a device run.

Before replay metadata, the host C snapshots retained 12,088 bytes for Blood
Gulch, 8,732 for Battle Creek and at most 16,460 in the campaign sample. With
the numeric source-address metadata described below, these are now **12,528**,
**9,148** and **17,000** bytes respectively. These include the descriptor and
index workspace, excluding allocator overhead. ARM pointer sizes differ. No
per-query allocation is required once a snapshot has been published.

## Floating-point state and epoch rebasing

The stateful `xk_cluster_query_fpu.c` prototype now reconstructs all eight x87
slots, the balanced stack position and the full software status word. It uses
the same quiet ARM comparison as the existing runtime and retains the original
double arithmetic and float spill points. State is reconstructed from the typed
math's intermediates rather than replaying each virtual x86 instruction. The
ordinary numerical-only prototype remains available for comparison.

The original status-word update also retains earlier TOP bits. Reconstructing
only the final comparison would miss this behavior; the stateful helper applies
each original comparison's status update. All input slots must be supplied,
including inactive slots. Failed attempts discard both outputs and require
restoring the entry native floating-point environment before the original path.

Validation of the stateful helper:

| Check | Result |
| --- | --- |
| Host ASan/UBSan, four rounding modes, varied entry stack/status and NaN payloads | 3,728 exact accepted comparisons; 368 conservative declines |
| VitaSDK ARM numerical results, all x87 slots/status and complete native FPSCR | 350 accepted matches across 440 cases; 90 conservative declines |
| C-constructed Blood Gulch, Battle Creek, a10 and a30 snapshots, host UBSan | 26,624 exact numerical/x87/exception comparisons; zero declines |
| ARM compile with warnings as errors | 2,320 bytes of text, no data/BSS; 2,176-byte query stack excluding callees |

Including state reconstruction and the fixture's entry-state copy, the ordinary
ARM instruction counts are:

| Clusters | Original prefix | Stateful typed query | Reduction factor |
| ---: | ---: | ---: | ---: |
| 7 | 27,275 | 5,762 | 4.73x |
| 31 | 134,147 | 27,818 | 4.82x |
| 65 | 285,507 | 59,063 | 4.83x |
| 256 | 1,131,637 | 234,401 | 4.83x |

The same limitations apply: these exclude the modeled 212-byte libc clear/copy,
snapshot construction, locking, guest general-register/scratch reconstruction,
publication and allocation/list work. They do not predict frame rate. A simpler
experiment that inlined the original helpers and compiled them at `-O3` preserved
full state but saved only about 1.5% of ordinary ARM instructions and regressed
short paths. It was not integrated or sent to hardware.

`xv_cluster_query_rebase` handles another requirement for future unlocked work:
another query may advance the shared epoch while a private result is computed.
It checks that the captured and current visited tables identify the same active
clusters, then rebases only the private result's epoch. A changed active set
declines without modifying the result. Paths which do not advance the epoch
explicitly return `NO_EPOCH`; publication must write neither epoch nor visited
words on those paths. Geometry and other mutable inputs still need independent
validation, and publication must remain one guarded transaction with the original
list tail. The helper itself performs no shared writes or floating-point work.

The rebase oracle executes the complete original numerical prefix before and
after compatible epoch changes. ASan/UBSan passes 1,024 full-context, full-memory
and native-exception comparisons: 740 epoch advances, 284 no-epoch paths, 212
wraps and 740 deliberately conflicting active sets. This proves the tested epoch
transformation, not typed general-register/scratch reconstruction or live races.

## Complete private prefix reconstruction

`xk_cluster_query_replay.c` now uses the shared typed traversal to reconstruct the
entire context and the original scratch writes at `566DE`. It records changed
halfwords in a private 16 KiB scratch area, leaving unmarked bytes unspecified.
It does not copy the old guest stack or read from live guest memory. The small
recursive-frame records are private; only initialized saved-register values are
read back from private scratch. Nonpositive-radius and no-cluster paths retain
their original register/flag behavior and do not publish an epoch or marker.

The snapshot also owns original per-portal plane indices and per-cluster adjacency
addresses. These numeric addresses reproduce register and scratch values; they
are never dereferenced by the query. They come from the same construction as the
owned geometry and share its lease lifetime. Per-query center/head addresses are
supplied in a copied layout descriptor. This metadata adds no source-memory
dependency to a computation after construction.

Validation now includes:

| Check | Result |
| --- | --- |
| Host ASan/UBSan, full prefix context, 8 MiB arena and native exceptions | 3,728 exact accepted comparisons; 368 conservative declines |
| VitaSDK ARM full prefix context, arena and complete FPSCR | 350 accepted matches across 440 cases; 90 conservative declines |
| C snapshots from all 13 owned-map BSPs, full prefix context/memory and exceptions | 3,328 exact comparisons; zero declines |
| Original whole query versus private replay followed by original allocation/list tail, ASan/UBSan | 928 exact comparisons; 96 conservative declines |
| Snapshot metadata, allocation/read failures and retirement | ASan/UBSan and TSan pass, including two readers and 2,000 publications |

The tail oracle uses the original allocator, with capacities 0, 1, 8 and 1,024
in both pools, and checks the whole final context/arena and `ret 16` stack
convention. It does not replace allocation failures with successful test stubs.
The shared numerical/FPU-only variant retains its previous quick ARM results
and instruction count after extraction into `xk_cluster_query_impl.h`.

The first replay used a generic tiny-write helper, which erased most savings.
Replacing it with inlined constant-size stores and one dirty-mask update per
word retains the same exact-state results. Final ordinary prefix counts are:

| Clusters | Original ARM instructions | Typed replay | Reduction factor | Dirty scratch bytes |
| ---: | ---: | ---: | ---: | ---: |
| 7 | 27,275 | 14,513 | 1.88x | 636 |
| 31 | 134,147 | 68,849 | 1.95x | 2,442 |
| 65 | 285,507 | 145,804 | 1.96x | 4,004 |
| 256 | 1,131,637 | 574,217 | 1.97x | 12,408 |

These include private context and scratch bookkeeping but exclude modeled libc
copy/clear work (1,668 bytes per positive query), mapping/alias validation,
snapshot construction, actual publication and the unchanged allocator/list tail.
The ARM object has 6,288 text bytes, no static data/BSS and a reported 6,464-byte
stack, excluding callees. The ARM replay output occupies 17,768 caller-owned
bytes, separate from numerical output and snapshot storage. This remains an
uninstalled prototype, not a measured frame-time improvement.

## Guarded batch runtime candidate

`xk_cluster_runtime.c` now builds an owned geometry snapshot on the guest owner
immediately before each queued worker batch starts. It releases the snapshot
only after all worker completion semaphores have been consumed. Any owner service
invalidates the batch before executing; invalidation cancels publication without
freeing storage still used by another worker. The owner FP environment is restored
after construction, including on failure.

Each lane has private replay storage. Admission checks the actual native worker,
context, depth-one math guard, captured stack mappings and private scalar inputs.
Physical overlaps between writable spans are rejected. Geometry sources cannot
alias worker stacks, the visited table, epoch or marker. The adapter captures
context, arguments, visited words, epoch, marker and FP state, computes privately,
then rechecks them and the mappings before publishing only marked scratch bytes
and changed visited words. The original allocation/list tail remains unchanged.
Every decline restores entry FP state and uses the original route. No query
unlocks the shared guard, and no epoch is reserved ahead of publication.

Build with `XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_WORKER_QUERY=1
XV_TYPED_CLUSTER_QUERY=1`; the existing exact runtime opt-in `XV_WORKER_QUERY=1`
is also required. The typed mode selects these new objects instead of the older
generated-C adapter. The build stamp tracks both query switches, including the
transition back to an ordinary build. No benchmark selector has been added yet.

The actual-pool fixture now prepares immutable geometry before each batch,
rather than rewriting it inside callbacks. Tests use the real production guard,
admission, scheduler and hook, with a generated original reference kept private:

| Check | Result |
| --- | --- |
| ASan/UBSan original-versus-hook comparison | 336 exact full-context, memory and FP comparisons; 68 typed admissions after cheap-case bypasses |
| Runtime disabled | 24 exact original-route comparisons; no typed admission |
| Aliases / mutable-input conflicts / invalid sources | 16 / 10 / 12 comparisons preserving the original state on decline, including last-live-stamp changes across a page boundary |
| Concurrent list updates and removals | 128 typed queries across both workers; expected final epoch and list memberships |
| Owner parking/service | Batch invalidated; original query remains usable afterward |
| Owner service during private computation | Publication declined; caller context, scratch, epoch and marker remain unchanged |
| Instruction budget exhausted | Original worker stop is preserved |
| TSan | Concurrent-list, owner-parking and in-flight invalidation scenarios pass |
| Vita SDK / actual Makefile | All new objects compile; worker object OFF / typed ON / OFF symbols and stamps verified |

These tests prove behavior with a controlled immutable source and explicit
invalidation. They do **not** prove that every live guest writer obeys that
contract. The source/retirement audit below remains a deployment prerequisite.
Per-batch allocation, geometry copying, input capture, validation and publication
are also additional costs; the earlier 1.9x prefix instruction result excludes them.

## Complete-path ARM follow-up

The first integrated adapter loses its numerical advantage on small inputs.
In the seven-cluster chain it executes 39,540 instructions versus 37,926 for the
original, before the snapshot build. Nonpositive-radius and invalid-start cases
are substantially worse. This is a reason to keep it disabled, not a hardware gain.

The revised adapter uses wordwise equality checks for aligned captures, publishes
through already-validated page spans without searching the span list per write,
and scans only scratch-mask words within the replay's maximum-depth bound. It
also sends nonpositive/nonfinite radii, invalid starts and one-cluster graphs
directly to the original route before full capture. No mapping, ownership or
mutable-state validation is removed.

`test_arm_cluster_runtime.py` now executes the production hook, actual pool guard
and admission code, full adapter and original allocation/list tail. Four native
rounding modes and five FP controls, including FZ/DN/sticky status, give **400
exact context/arena/FPSCR comparisons**, with 300 admitted typed queries. Pool
capacities 0, 1, 8 and 1,024 exercise allocation failures. The host pool additionally
tests stack offsets at both page edges, including a six-fragment stack/argument
capture. Concurrent-list and owner-parking TSan checks still pass.
An additional actual-pool test holds one lane after private computation while
the other requests an owner resource service. ASan/UBSan and TSan both confirm
that invalidation cancels publication without changing the captured caller
context, scratch, epoch or marker.

| Synthetic chain | Original query instructions | Revised query instructions | Snapshot construction, once per batch |
| --- | ---: | ---: | ---: |
| 7 clusters | 37,927 | 32,903 | 10,913 |
| 31 clusters | 180,391 | 126,460 | 43,239 |
| 65 clusters | 380,666 | 255,013 | 88,983 |
| 256 clusters | 1,222,594 | 693,716 | 345,447 |

These are instruction counts, not cycles or FPS. The fixture runs actual SDK
`memcmp` and integer/floating-point code, but models the firmware targets of
`memcpy`/`memset` separately. Copied/cleared bytes rise from 168 to 4,252 in the
seven-cluster query and from 1,536 to 17,392 in the 256-cluster query. There are
six additional thread-ID imports per admitted query. Kernel latency, contention,
cache behavior and real allocator bookkeeping are excluded; snapshot allocations
use a bounded fixture allocator. The original and candidate share the unchanged
uncontended guard and original tail. A tiny readiness callback adds test-only work.

The small-work regression is **not fully solved**. A seven-cluster graph whose
sphere rejects its first portal takes 9,595 candidate instructions versus 3,128
original instructions. Positive queries in a larger map can still visit only
one cluster. Map size alone is therefore not a sufficient eligibility test.
Actual worker query sizes/portal work need measurement before selecting this
path on hardware. The earlier physical census counted 12,578 worker queries but
did not measure their work. Avoid attributing the user's existing 20 FPS reports
to this uninstalled candidate.

The source audit finds 756 functions in the direct `8FB70` call closure and no
directly reachable writes to roots `39BE58/39BE50`. However, 45 functions contain
indirect calls, and field-level geometry writes are not fully covered by a root
search. `D3DResource_IsBusy` only updates its caller context; the known `12CC3`
event wrapper passes a null previous-state output. Other owner services are
quiescent. These observations narrow the audit but do not complete its lifetime
proof; conservative invalidation remains in place.

## Integration work still required

1. **Complete the live source-lifetime audit.** The original reset `58440` clears roots at
   `58492/5849E`, after calls that can retire data. BSP switching `58CD0` invokes
   load/unload helpers before publishing roots at `58D87/58D97`. Invalidate before
   retirement, construct after successful loading with workers drained, and
   publish an owned generation if a persistent cache is introduced. The current
   per-batch snapshot drains workers and invalidates all owner services, but still
   needs an audit of worker writes to the consumed geometry and mapping storage.
   Real construction/allocator cost also needs measurement. Pointer equality and the existing large-read
   texture-purge heuristic do not establish that lifetime. Generated overlapping
   entries also need coverage; an entry at `58D8A` contains the later root store.
2. **Add controlled measurement and deployment.** Admission and publication now
   pass the actual-pool tests above. Keep the guard held, add an owner-side
   OFF/ON/OFF selector at drained boundaries, and validate the complete private
   game build before considering an update. The current hardware contains only
   the original-query diagnostic, not the typed adapter.
3. **Measure real query work and frame time.** The new
   [original-query census](query-work-census-20260916.md) records per-worker
   result counts and budget deltas. Its first native-resolution valley run
   finds 79.5% of calls return 1–3 clusters; the 8–15 cluster bucket holds 10.3%
   of calls and 57.3% of counted backedges. Repeat in other views and establish
   an inexpensive eligibility rule before enabling the adapter. Output size
   alone is not an entry-time predicate or a CPU-time measurement. Compare
   end-to-end guard occupancy and hardware frame time,
   then exercise combat, driving and campaign BSP transitions. Even the complete
   synthetic instruction reduction is insufficient to retain a runtime change
   or claim stable 20 FPS.

## Reproduction

With Python dependencies, a host C compiler, the supported locally owned XBE and
its manifest, use a private output directory outside the repository:

```sh
python tools/test_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/host" --cases 1024 --sanitize
python tools/test_arm_cluster_query.py --reference "$RESULTS/host/reference.c" \
    --out "$RESULTS/arm"
python tools/test_owned_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/maps" --native-snapshot --maps "$MAPS/bloodgulch.map" \
    "$MAPS/beavercreek.map" "$MAPS/a10.map" "$MAPS/a30.map"
python tools/test_cluster_snapshot.py --out "$RESULTS/snapshot-asan"
python tools/test_cluster_snapshot.py --out "$RESULTS/snapshot-tsan" --sanitize thread
python tools/test_cluster_runtime.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/runtime-asan" --sanitize address
python tools/test_cluster_runtime.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/runtime-tsan" --sanitize thread --mode concurrent --mode parking
python tools/test_cluster_runtime.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/runtime-arm" --arm
python tools/test_arm_cluster_runtime.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/runtime-cost"
python tools/test_cluster_query_rebase.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/epoch-rebase"
python tools/test_cluster_query_fpu.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/fpu-host"
python tools/test_arm_cluster_query.py --reference "$RESULTS/fpu-host/reference.c" \
    --fpu --out "$RESULTS/fpu-arm"
python tools/test_owned_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/fpu-maps" --native-snapshot --fpu --maps "$MAPS/bloodgulch.map" \
    "$MAPS/beavercreek.map" "$MAPS/a10.map" "$MAPS/a30.map"
python tools/test_cluster_query_replay.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/replay-host"
python tools/test_cluster_query_replay.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/replay-tail" --tail --cases 256
python tools/test_arm_cluster_query.py --reference "$RESULTS/replay-host/reference.c" \
    --replay --out "$RESULTS/replay-arm"
python tools/test_owned_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/replay-maps" --native-snapshot --replay --cases-per-bsp 64 \
    --maps "$MAPS/bloodgulch.map" "$MAPS/beavercreek.map" "$MAPS/a10.map" "$MAPS/a30.map"
```

The ARM tool requires VitaSDK, Unicorn and pyelftools; `ARM_CC` can override the
compiler path. It tests generated ARM instructions without launching Vita3K.
The owned-map tool uses the host ELF toolchain and UBSan. Private receipts are
under `engine-restructure-20260914T2300Z/direct-cluster-query/`.
