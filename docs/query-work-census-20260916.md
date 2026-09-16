# Original spatial-query workload

The existing `light-census` diagnostic (selector 37) now measures the original
`56670..566DE` query prefix on the guest owner and both object workers. It remains
Off outside an explicitly enabled diagnostic. This measures work distribution
and observer overhead; it does not enable the typed query or change rendering.

The previous physical census counted 12,578 worker query callsites in 120 frames
but deliberately read no worker query metadata. The complete typed adapter now
saves instructions on larger synthetic traversals and loses on some short ones.
We need the real distribution before selecting that implementation. A large map
does not imply that every query traverses many clusters.

## What is counted

The entry hook runs after the existing shared transaction guard. The exit hook
runs at `566DE`, before the original 64-result clamp and list-allocation tail.
Neither hook reads guest memory, changes the guest context, releases the guard,
allocates, reads a clock or writes a log line. It reads the current context's
result count, stack pointer and scheduler budget, after verifying native thread
and context identity.

Each native thread owns a separate aligned row. Lane 0 is the guest owner;
lanes 1 and 2 are the existing object workers, not physical CPU IDs. Reports and
resets use the existing drained owner boundary after worker joins. Foreign
threads, borrowed worker contexts on the owner and wrong worker guard tokens are
rejected before context inspection.

For each lane, `[query-work-count]` reports:

- Entries, completed prefixes and invalid samples.
- Completed samples at guard depth one, nested depth or unavailable depth.
- Query counts, summed backedges and maximum backedges by result-count bucket.
- Separate counts and backedge sums for depth-one queries.
- A backedge-count histogram.

Result-count buckets are **0, 1, 2–3, 4–7, 8–15, 16–31, 32–63, 64–127,
128–255, 256**. Backedge buckets are **0, 1, 2–3, 4–7, 8–15, 16–31, 32–63,
64–127, 128–255, 256–511, 512–1023, 1024 or more**.

Backedges are the difference in the existing scheduler budget across the
numerical prefix. They include taken loop edges in its children. They are not
cycles, elapsed time, portal tests or GPU utilization. A nonpositive/reset
budget, unexpected stack position, changed depth or count above 256 invalidates
the sample. An entry without a completed prefix remains visible. The receipt
parser rejects invalid or unmatched samples, missing/duplicate rows,
inconsistent totals and counters in an Off arm.

The benchmark starts with `query-work-schema 1`. New clients require all query
rows when that marker is present, including if every row was lost. Old census
receipts without the marker or rows remain readable and contain no query-size
evidence. Formatting and output stay outside the 120-frame measurement windows.

## Validation

The actual worker-pool fixture compares against prefix checkpoints in separately
generated original code. ASan/UBSan passes 336 full-context/memory/FP comparisons
and 24 original-only comparisons, with exact result buckets and backedge sums.
Counts above 64 are checked before clamping. The concurrent test completes 128
queries across both workers. Owner depth-one/nested admission, forbidden reads
from foreign threads, borrowed contexts, invalid samples and drained resets are
also covered. Concurrent, in-flight invalidation and owner-parking tests pass
under TSan. Original instruction-budget stops remain intact.

Owned-image emission checks preserve all original instructions and verify both
hook positions with trace markers enabled and disabled. The Vita SDK compiles
the observer and pool. Benchmark/controller and client tests check Off/On/Off
restoration, complete reports, old receipts and missing new-schema reports.

The isolated diagnostic build starts from the last installed source stage and
changes the observer, reporting and two guarded sites in `code_010.c`. The typed
adapter is absent. Existing compiler flags, including matrix NEON, are retained.
The completed package has the same 1,588 members; only the game executable and
boot digest differ. Object hashes confirm that only the query translation unit,
worker admission, census and benchmark objects changed; all other objects match
the previous stage. Candidate runtime SHA256 is
`b3ace3af9da6b78654d2bc0fda026b6549bbf61338b6764f6fef11bf07a86fa5`.
The prepared package still needs updater verification and a physical census;
host checks do not establish its hardware cost or a frame-rate improvement.

## Hardware comparison

Use the existing authenticated client with a loaded first-person view:

```sh
python3 tools/vita_remote.py --config "$PAIRING" benchmark \
    --kind light-census --runs 1 --timeout 240 "$RESULTS"
```

Keep standard graphics and the camera fixed. Preserve Off/On/Off FPS alongside
the query rows to quantify observer overhead. Repeat in the Blood Gulch valley,
a base or cave, Battle Creek and campaign before treating one distribution as
representative. If most work is in tiny queries, avoid the full captured adapter;
if larger depth-one traversals dominate, quantify their end-to-end cost before
choosing an eligibility rule. This observer does not prove snapshot immutability
or safe guard release. Combat, driving, campaign transitions and GPU-crash
stability remain separate required checks.
