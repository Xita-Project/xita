# Collision query coverage in the current campaign build

The cumulative `0.2.0-perf.7 / 02998dd+` remains near 12.8 FPS at the saved
campaign checkpoint. A fresh, bounded object-hold observation completes on
that installed runtime and restores sampling Off. This is attribution, not an
optimization comparison; sampled inclusive intervals include scheduling and
must not be added as independent frame costs.

Across 30 sampled collision-wrapper calls, `172BF0` takes 28,032 microseconds,
collection `171F10` takes 23,125, and solving `170C10` takes 4,677. The nested
geometry query `88110` has 143 entries totaling 17,428 microseconds. Those query
entries can occur both directly and inside object collection, so the counts
alone do not identify which caller dominates.

The existing fused query only covers `172C95 -> specialized 171F10 -> 171F94`.
Primary object-space collection `172F40` still calls generic `88110` at `17301B`.
Its local geometry, transformed center/radius and stack result are ordinary
query inputs, but a new route needs complete parent/continuation qualification
and evidence of its actual workload. No new optimized route is enabled here.

The optional hold sampler now partitions its existing `88110` measurements
by the validated worker-private return word: world `171F99`, object `173020`,
other, or unreadable. It uses the same start/end interval, so counts and elapsed
time reconcile exactly with the existing query row. It does not use a guest
return address to choose execution, release a lock, or change guest state.
Foreign contexts, unsampled scopes, nested query entries and disabled sampling
still decline before attribution. Joined reports clear the new fixed-size
per-lane counters with the existing sample records.

The production worker tests pass normally, under ASan/UBSan and under TSan
with two/one/zero workers and both contention policies. Synthetic private return
words exercise world, object and other buckets. Report counts, elapsed sums,
maximums and reset behavior reconcile to the original query measurement. The
existing stack-range/mapping check guards reads; unreadable entries retain a
separate bucket and do not cause an unvalidated read.

Private evidence is under `query-object-space/` and
`ce-perf7/object-holds-current/` in the unified-games workspace. The separate
model audit found first-person-only remaps, an already batched palette and
already shared deferred pose snapshots; those do not presently justify another
large model-copy rewrite. A full caller fixture for the object-space query is
the next implementation gate.
