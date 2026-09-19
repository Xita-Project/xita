# Index preparation follow-up

The retained perf14 campaign log reports roughly 2.1 ms/frame of index
preparation, versus 0.36–0.39 ms for constant submission in its final windows.
These are elapsed nested preparation intervals, not independent additions to
the frame total. Existing index reuse is disabled. Its earlier physical
[within-frame comparison](index-reuse-20260915.md) did not improve FPS; that
negative result remains in force.

The current retainer constructs maximum-index bounds and an exact 1 KiB bitmap
of referenced eight-vertex groups from captured index chunks. This costs scalar
bitmap insertions even with the NEON maximum/popcount path. The existing optional
cache discards all CPU metadata at each recording-frame boundary, so it cannot
avoid repeated coverage construction across frames.

## Distinct candidate: retain CPU metadata across frames

A private sizing prototype retains the existing exact-byte index mirror and its
bounds/coverage. Each eligible invocation validates current source bytes against
that mirror. A hit copies the mirror into the caller's destination and returns
the already computed metadata. It never reuses an earlier GPU pointer. A miss
captures current input and executes the existing production copy/coverage helper.

This targets CPU coverage construction. It does not remove GPU copies, extend
GPU index lifetimes, change triangles or use pointer identity as proof of equality.
The prototype uses the existing 64-entry, 64–4096-index cache layout: its ARM
size is 591,616 bytes. Enabling a currently unallocated cache would require that
additional memory. It is not a zero-memory optimization.

## Bounded cost check

The Vita-compiled prototype uses production `xv_index_copy.h`,
`xv_index_cache.h` and `xv_bytes_equal.h`. The runner models Vita firmware bulk
copy/clear calls and counts those bytes separately from executed ARM instructions.

| Indices | Original instructions | Cold instructions | Hit instructions | Original / cold / hit firmware bytes |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 1,189 | 1,240 | 129 | 1,288 / 2,448 / 1,160 |
| 256 | 2,601 | 2,651 | 315 | 2,056 / 3,600 / 1,544 |
| 1,024 | 10,197 | 10,247 | 1,059 | 5,128 / 8,208 / 3,080 |
| 4,096 | 40,581 | 40,631 | 4,035 | 17,416 / 26,640 / 9,224 |

All 228 comparisons preserve copied bytes, maximum bounds and every coverage
word. They exercise random, localized and repeated-maximum indices, odd source
addresses, empty input, admission boundaries, repeated calls, and tail mutations.
Destination sentinels remain unchanged beyond the requested copy. This does not
establish complete memory-access bounds, cache-collision behavior, frame-ring
exhaustion, scheduler ownership, physical timing or live campaign hit rate.

Cold calls copy more data. The instruction ratios exclude firmware execution,
cache/memory costs and production ring allocation. They cannot predict FPS or
an acceptable hardware hit threshold. Retaining only 64 directly mapped entries
may provide insufficient reuse across a roughly 150-draw frame.

Private evidence is `index-metadata-cost/{probe.c,probe.py,probe.elf,results.json}`
under the unified-games workspace. No production code or build flags changed.
Perf15 remains the UV-reuse candidate awaiting its physical test.

## Integration requirements if pursued

1. Keep CPU mirror lifetime separate from GPU allocation validity. Every frame
   must invalidate GPU pointers while preserving only eligible CPU metadata.
   Handle both normal `BeginFrame` and standalone `Swap` paths.
2. Validate exact current bytes and reference policy before using metadata.
   Capture one immutable version on a miss; derive both GPU bytes and coverage
   from it. No later reread of mutable guest data may supply the bounds.
3. Reserve space in the current ring before publishing a new GPU pointer. A
   prior-frame metadata hit cannot bypass ring exhaustion. Only an allocation
   already valid in the current frame can be reused when the ring is full.
4. Retain allocation-failure fallback, explicit policy invalidation and shutdown
   ownership. Test source mutation, direct-map collisions and two delayed GPU
   generations while overwriting CPU mirrors.
5. Count metadata hits separately from within-frame GPU-copy reuse, misses,
   ineligible sizes and copied bytes. Live evidence must establish the hit rate
   and frame behavior after restart before this joins the cumulative default.
