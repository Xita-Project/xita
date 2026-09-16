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
The authenticated updater verified the package and confirmed this digest booted
in slot 0. The previous `c999c012…` runtime remains in slot 1. The executable's
unchanged main object still reports the earlier build timestamp; the boot digest
identifies this update.

## First physical result

A fixed Blood Gulch valley view completed all three arms on the physical Vita.
The launch log confirms native 960×544, Standard textures, original material,
glow, particle and model quality, and temporary decals, cosmetic effects,
reflections and object shadows enabled. Triple buffering, extended compressed
textures and the existing worker settings were retained. Effective clocks were
CPU 444 / bus 222 / GPU 222 / crossbar 166 MHz; the requested 500 MHz was unavailable.
The previous session used 360p, Low textures and three disabled visual switches,
so its FPS is not a standard-settings comparison.

| Observer Off before | Observer On | Observer Off after |
| ---: | ---: | ---: |
| 9.345 FPS | 9.084 FPS | 9.350 FPS |

The camera check passed. The observer added about **2.9% frame time** relative to
the mean Off FPS in this one trial; it was restored Off after measurement. This
is diagnostic overhead, not an optimization gain or a before/after build test.

Both workers supplied **15,483 complete queries over 120 frames** (129/frame),
with no invalid or unmatched samples. The owner row was empty. Of these, 15,086
were at guard depth one and 397 were nested.

| Returned clusters | Worker queries | Summed loop backedges |
| --- | ---: | ---: |
| 1 | 6,352 | 33,348 |
| 2–3 | 5,955 | 194,927 |
| 4–7 | 1,588 | 65,108 |
| 8–15 | 1,588 | 393,030 |
| Other buckets | 0 | 0 |

Queries returning 1–3 clusters account for **79.5% of calls**. The 8–15 bucket
accounts for **10.3% of calls and 57.3% of counted loop backedges**. This supports
investigating selective acceleration and reducing adapter overhead. It does not
establish CPU-time shares: backedges are not cycles, and output size is known
only after traversal. An inexpensive eligibility rule still needs evidence.
The typed adapter remains absent from the installed executable. Other views,
campaign, driving and combat remain unmeasured with this census.

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
