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
