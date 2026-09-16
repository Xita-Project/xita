# Direct spatial query prototype

The new `xk_cluster_query.c` replaces the numerical part of Halo 3925's
`56670..566DE` spatial query with a typed traversal. It is **source-only**:
there is no Makefile registration, guest hook, live unlock, or hardware update.
The [earlier guarded adapter](worker-query-adapter-20260916.md) remains off.
This is a candidate for reducing measured world/light preparation, not a
demonstrated frame-rate improvement.

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
distinct roots. Snapshot creation currently exists only in the offline oracle.

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

## Integration work still required

1. **Own snapshot lifetime.** The original reset `58440` clears roots at
   `58492/5849E`, after calls that can retire data. BSP switching `58CD0` invokes
   load/unload helpers before publishing roots at `58D87/58D97`. Invalidate before
   retirement, construct after successful loading with workers drained, and
   publish an owned generation. Pointer equality and the existing large-read
   texture-purge heuristic do not establish that lifetime. Generated overlapping
   entries also need coverage; an entry at `58D8A` contains the later root store.
2. **Preserve observable state.** Numerical success does not reconstruct the
   guest context, scratch or native FPSCR at `566DE`. The original allocation and
   list tail needs the correct ordered output and entry state. Prove which other
   state is dead, or reproduce it exactly, before connecting this result. The
   current caller's immediate register restores are not a whole-call-chain proof.
3. **Publish in one guarded transaction.** Capture mutable inputs, compute on
   owned arrays, validate the generation and shared visited/epoch state, and
   publish with the original allocation/list tail. Any failure must preserve
   the original fallback. Release the guard only at a proven depth-one boundary;
   do not reserve an epoch early or wait for other workers while holding it.
4. **Measure the complete path.** Compare end-to-end guard occupancy and hardware
   frame time, then exercise combat, driving and campaign BSP transitions. The
   kernel's instruction reduction alone is insufficient to retain a runtime
   change or claim stable 20 FPS.

## Reproduction

With Python dependencies, a host C compiler, the supported locally owned XBE and
its manifest, use a private output directory outside the repository:

```sh
python tools/test_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/host" --cases 1024 --sanitize
python tools/test_arm_cluster_query.py --reference "$RESULTS/host/reference.c" \
    --out "$RESULTS/arm"
python tools/test_owned_cluster_query.py --xbe "$XBE" --manifest "$MANIFEST" \
    --out "$RESULTS/maps" --maps "$MAPS/bloodgulch.map" \
    "$MAPS/beavercreek.map" "$MAPS/a10.map" "$MAPS/a30.map"
```

The ARM tool requires VitaSDK, Unicorn and pyelftools; `ARM_CC` can override the
compiler path. It tests generated ARM instructions without launching Vita3K.
The owned-map tool uses the host ELF toolchain and UBSan. Private receipts are
under `engine-restructure-20260914T2300Z/direct-cluster-query/`.
