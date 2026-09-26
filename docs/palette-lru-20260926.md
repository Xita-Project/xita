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

## Hardware candidate

Perf263 / 04471a8c built successfully on the perf262 private stage. Package
`palette-lru-candidate/xita-perf263c.vpk` changes only `game-a.self` and
`boot-game.txt`, retaining the existing update contract and assets.

- Runtime 34805522 bytes, SHA-256
  `b0a6dbea50ce2bbd1e5ca113f0639c1dd501f29c00db2d89b653ff70b14c8f17`.
- VPK SHA-256
  `c5f1ef4b0a8050e3cf830e71f5edb243b26ebbb761b765a1ff3f7a126fa6e125`.

Deployment started with output in `palette-lru-candidate/deploy.log`. Confirm
the final update receipt/live version before calling it installed. Preserve
the a30-perf211 namespace and renew the awake lease after restart. No hardware
FPS or cache improvement has yet been established for this candidate.

## Native ARM and deployment follow-up

The Raspberry Pi ran `palette-pi` pinned to core 0 and passed 480 comparisons:
counts 1/4/16/32/64, two finite matrix patterns, all eight guest x87 TOP values,
three native FP states, cold and warm cache. Complete context, 8 MiB arena,
admission and raw FPSCR matched the native uncached baseline. Evidence is
`pi-main.c`, `pi-build.json`, `pi-build.log` and `pi-result.log` in the private
candidate directory. This supplements the 156 instruction-runner comparisons;
it does not exercise the concurrent game scheduler or establish Vita FPS.

The first static Linux link encountered a fixture/libc `abort` symbol conflict;
the final test build namespaces fixture `abort` and `getenv` explicitly instead
of interposing libc. No production source change was required for the harness.

Deployment session 6523 completed: runtime bytes/hash verified, slot 0,
boot_confirmed true. Awake lease renewed, then the version-asserting protected
a30 launcher started under session 18014 (`launch.log`). That session must be
polled to completion; exclude loading. Perf262 remains the previous slot.
Next capture settled ordinary lifepod and outdoor/weapon gameplay, joining cache
counters with measured Present intervals and slow-frame counts. No new trace or
heavy per-call timers should contaminate those timing windows.
