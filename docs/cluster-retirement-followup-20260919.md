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
