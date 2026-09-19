# Measuring repeated collision-result searches

The current fused query still searches its surface, edge and vertex result lists
linearly. Before adding a query-local index, measure whether those lists are large
enough to justify its maintenance cost. The prior campaign capture attributed
84% of sampled query time to the world route, but did not count list lengths.

The existing optional object-hold sampler now captures entry ESI for already
selected scope-6 queries. It validates the complete private-stack result span,
then revalidates the arena, page-table identity and saved stack-page mappings at
return. After the existing timer stops, it reads the surface, edge and vertex
counts at offsets 0, 0x404 and 0x808. Counts above the original capacity of 256
are invalid; unreadable output is reported separately. Neither contributes to
valid length statistics. No guest register or memory is modified.

Joined reports partition outcomes by the existing query origin and emit each
list's sum, maximum and buckets: 0, 1–8, 9–32, 33–64, 65–128, 129–256. These are
observed final lengths, not actual comparison counts or independent timings.
Sampling remains Off during ordinary gameplay; there are no per-search counters,
allocations, extra clock calls, lock releases or execution-routing changes.

Production worker tests pass under ASan/UBSan and TSan with two/one/zero workers
and both wait policies. They exercise valid, over-capacity and unreadable outputs,
entry ESI capture despite later register changes, exact sums/maxima/buckets,
per-origin outcome reconciliation, no guest changes and joined report reset.
The existing private-stack mapping guard remains responsible for validating reads.

## Hardware decision

`0.2.0-probe.2 / 2069f59+` booted in slot 0 with the complete perf.8 stack.
The saved campaign capture completed and restored sampling Off. All 135 observed
queries had valid result buffers; none was unreadable or over capacity.

| Route | Queries | Surfaces mean / max | Edges mean / max | Vertices mean / max |
| --- | ---: | ---: | ---: | ---: |
| World | 29 | 7.90 / 17 | 7.17 / 16 | 1.07 / 3 |
| Object | 106 | 0.28 / 3 | 0.19 / 2 | 0 / 0 |

The world samples total 14,144 microseconds; object samples total 2,086. These
remain inclusive sampled intervals. Final list sizes are small in this workload,
so adding a persistent result index is not justified by this capture. This does
not rule out larger lists in other scenes or count repeated encounters directly.
The proof plan is retained privately, but no index was added. The next candidate
is avoiding unnecessary model-hierarchy batch declines while preserving the
original final node's arithmetic. Raw captures and the reconciled summary are
under `query-result-census/` in the private unified-games workspace.
