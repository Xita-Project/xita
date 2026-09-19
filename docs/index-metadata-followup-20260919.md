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

Private sizing evidence is
`index-metadata-cost/{probe.c,probe.py,probe.elf,results.json}` under the
unified-games workspace. The subsequent production integration is described
below. Perf15 remains the UV-reuse candidate awaiting its physical test.

## Integration requirements

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

## Production integration

`XV_INDEX_METADATA=1` in the startup environment or `xita.cfg` enables CPU
metadata retention, including the existing within-frame GPU-copy reuse. It
defaults Off and requires closing and relaunching Xita. A research build can set
`XV_INDEX_METADATA_DEFAULT=1`; an explicit startup setting (including `0`)
still takes precedence. This lets a remote update select the experiment without
editing device configuration. There is no new dashboard
menu row and no change to shader programs, geometry, frame count or GPU fences.

Both `BeginFrame` and standalone `Swap` now call the same index frame-boundary
helper. With metadata retention Off it resets all identities as before. With
retention On it clears every cached GPU pointer while retaining CPU mirrors.
A byte-identical hit with no current GPU pointer must pass the ordinary append
capacity check and copy its mirror into the current slot. It publishes that
pointer only after copying. Failed capacity checks preserve the caller's index
pointer and vertex count and leave reference coverage invalid. A subsequent hit
in that same frame can reuse the now-current GPU allocation.

The existing `XV_INDEX_REUSE` control retains its prior meaning. Explicit
benchmark overrides disable cross-frame metadata and reset the entire cache;
restoring the startup policy also resets it. Thus the existing off/on/off
diagnostic remains a within-frame comparison, not a hidden mixture of policies.

`[index-metadata]` reports successful fresh uploads from CPU metadata, matching
entries rejected for ring capacity, and bytes actually copied. The existing
`[index-reuse]` hits/saved-byte counters still count only within-frame GPU-copy
reuse. Ordinary index-copy accounting includes metadata reuploads. These
counters do not establish elapsed savings or FPS.

`tools/test_index_reuse.py` passes ASan/UBSan for both options Off, each option
alone, both On, invalid metadata selection, and forced cache-allocation failure.
The production retainer is exercised with 60 delayed-slot generations, source
rewrites, odd addresses, direct-map collisions, policy transitions, and explicit
cross-frame full-ring rejection/retry. Two older GPU generations remain checked
while CPU mirrors are replaced. Separate production vertex-reference tests pass
4,000 draws across 500 slot generations and all 16 benchmark selector
combinations. An independent lifetime/capacity review found no blocker.

The modified renderer compiles with the retained Vita flags and generated shader
assets. The physical hit rate, frame-time benefit, additional-memory impact and
gameplay stability remain unverified. Production integration is not a claim that
the prototype instruction ratios survive real allocation, cache or GPU costs.


## Startup-default qualification

Both compiled defaults pass the production retainer's ASan/UBSan fixture: 18
configurations cover absent and explicit settings, within-frame reuse, invalid
selection and allocation failures. An explicit metadata `0` disables retention
even in a default-On binary.

The native renderer object was built through defaults 0→1→1→0→0→1. Switching
rebuilds the owning object; repeating a value preserves its modification time;
returning to each value restores the same object hash. Empty, `2` and multi-value
Make selections are rejected. The project default remains Off pending hardware
results. No GPU allocation or retirement behavior changes with this selector.
