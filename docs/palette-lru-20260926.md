# Palette cache recency replacement

Perf262's settled lifepod logs show approximately 615–620 palette-prefix hits,
552–553 misses and 449 evictions per 60 frames. The existing 32-entry cache is
active, but round-robin replacement does not protect recently reused entries.
These counters establish replacement activity, not its cost or a promised gain.

The candidate changes only replacement to four-way least-recently-used ranks
within the existing eight sets. Identity lookups update recency, including
content misses. Actual reuse still requires the existing exact matrix contents,
count and FP-control checks. Math ownership, numeric admission, exception-bit
replay, final-matrix execution and guest scratch/context publication are unchanged.
Thirty-two one-byte ranks replace eight four-byte cursors: total cache storage
remains 320288 bytes. No broader cache or lifetime is introduced.

Private qualification: `../palette-lru-candidate/run.py`, `production.log` and
`results.json`. The fixture compiles current production source with test-only
symbol renaming/reset access, against the current native palette implementation
with prefix caching disabled. All 156 ARM/Unicorn comparisons pass, checking full
context, 8 MiB guest arena, admission and FPSCR. Includes changed inputs, counts,
rounding/sticky flags, fallback, 32 interleaved identities, set collisions and
repeatedly touching a hot entry before insertion. The latter retains the hot
entry and evicts the cold entry; round-robin would evict the hot entry.

A first approximate-age prototype passed its smaller fixture but was replaced
with rank-preserving LRU before production integration. An intermediate harness
finished its state comparisons then failed reading an optimized-away static
storage-size symbol; the fixture now exports an explicit test-only size constant.
Use `production.log`/current `results.json`, not the preserved initial results.

For one 16-matrix finite case, modeled instructions are 5804 cold / 3165 hit
against 5130 uncached. Earlier round-robin hit counts were around 3104; recency
has overhead and requires better reuse to benefit the frame. Instruction counts
exclude real cache contention and are not Vita time or FPS.

Not yet hardware-qualified. Next build on the cumulative perf262 baseline, then
collect ordinary gameplay: hit/miss/eviction and matrix counts, settled frame-time
distribution, and correctness. Do not claim a gain from fewer evictions alone.
