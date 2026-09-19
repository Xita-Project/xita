# Palette prefix reuse prototype — September 19

The [hardware draw attribution](scene-draw-attribution-20260919.md) leaves
14.78 ms of the crowded model-list interval outside the two timed draw APIs.
That does not attribute the residual to palette math, but makes repeated model
preparation a relevant candidate. This prototype changes numeric palette
preparation rather than the previously rejected whole-traversal register wrapper.

## Boundary

The existing guarded `xv_math_model_palette` batch computes a palette and then
reproduces the last original matrix call's context and scratch state. The private
candidate caches only the products before that last matrix. The final matrix,
register publication, scratch writes and scheduler-budget decrement still run.
The original bounds, alias, budget, numeric and FP admission checks remain.

A single private entry stores the exact prefix input bytes and outputs. A hit
requires matching count, floating-point control and actual pose/node-matrix
contents. It does not assume guest addresses, frames or material tags are
immutable. Changed inputs miss. Numeric cache misses compute with incoming sticky
exception bits temporarily cleared, retain the prefix's raised bits, and restore
those bits combined with the incoming status; hits replay those accrued bits.
The existing admission rejects enabled FP exception traps. The last matrix
continues to compute against the current input even if its prefix hits.

The first implementation used libc `memcmp`; its 64-matrix hit cost 24,801
modeled instructions versus 19,461 for the existing batch. That version is
rejected. Replacing the exact byte comparison with NEON integer word comparisons
produces the following results for nontrivial finite matrices:

| Matrices | Existing batch | Cache miss | Cache hit | Hit reduction |
| --- | ---: | ---: | ---: | ---: |
| 4 | 1,553 | 1,709 | 1,195 | 23.1% |
| 16 | 5,130 | 5,686 | 3,071 | 40.1% |
| 32 | 9,918 | 11,010 | 5,601 | 43.5% |
| 64 | 19,461 | 21,625 | 10,624 | 45.4% |

These are Cortex-A9 instruction-model results from VitaSDK production-style
`-O3 -funroll-loops` compilation, not cycles or hardware FPS. Firmware copies
are counted separately, not executed as real memory traffic: for 16 matrices,
miss/hit copy totals are 1,688/908 bytes; for 64, 6,680/3,404 bytes. The simplified
instruction break-even hit rate is about 20–22% for 16–64 matrices, excluding
copy latency, cache misses, contention and lookup costs of a larger cache.

## Qualification and limitations

Seventy-one paired executions compare the entire current batch against the
candidate, including complete x86 context, 8 MiB guest arena, admission result
and native FPSCR. Coverage includes identity and nontrivial palettes, counts
1/4/16/32/64, three FP entry states, cold and repeated inputs, changed pose or
node prefixes, changed final matrices, incoming sticky-bit changes, rounding
changes, changed count, another output stack, expired budget and NaN fallback.
The one-matrix path introduces no cached prefix. These cases do not prove all
numeric inputs, alias arrangements or original-lift composition; the baseline
is the previously qualified native batch, not a newly executed independent lift.
No worker concurrency, real guest scheduling or Vita3K validation is claimed.

The source repository's runtime is unchanged. The single-entry private design
is a cost/correctness probe, not a production cache: interleaved different models
may yield few adjacent hits. Next test a bounded owner-safe lookup against actual
model order and account for misses, copies and retained memory before deployment.
Do not introduce a broad material/model snapshot that bypasses live publication.
The existing math ownership guard must remain intact. None of the isolated
instruction savings establishes how much frame time palette math consumes.

Private sources, rejected byte-comparison results, compiler commands and all
71 comparison results are under `../palette-prefix-cache/`. Perf.25 remains the
last hardware build; no optimization or FPS improvement is claimed for this
prototype. Halo 2 remains parked while the CE campaign work proceeds.

## Bounded interleaving follow-up

The private candidate now has 32 entries arranged as eight sets of four ways.
Model/pose/node addresses select and identify a candidate; exact input contents,
count and FP control still prove reuse. Address identity alone never accepts a
hit. Full sets evict round-robin; empty ways are used first. The retained math
ownership guard remains unchanged. Storage is 320,288 bytes including replacement
indices, approximately 312.8 KiB, excluding a few scalar counters. This is a
static host-side pool in the prototype, not an allocation in the current game.

All 142 paired ARM executions pass. In addition to the previous 71 cases, the
fixture fills four inputs in each of eight sets, revisits all 32 in reverse
order, and then exercises five colliding identities in a single four-way set.
All 32 balanced revisits hit; the fifth collision evicts one entry, an un-evicted
identity still hits, and the evicted identity recomputes correctly. Every pair
compares full context, the 8 MiB guest arena, admission result and native FPSCR.
The synthetic identities carry distinct pose data. These are constructed model
orders, not a captured gameplay trace or a measured hardware hit rate.

The 16-matrix collision-survivor hit costs 3,104 modeled instructions versus
5,130 for the existing batch (39.5% fewer); the collision miss costs 5,774.
Firmware copy bytes remain separately counted and not cycle-modeled. This keeps
the isolated cost result positive despite bounded lookup overhead, but cache
set distribution, changing poses and memory traffic may change the live result.

Proceed to explicit opt-in runtime integration with joined hit/miss/eviction
counters and a retained off path, then test actual gameplay. Do not allocate a
larger pool just to inflate the synthetic hit rate. Original-lift composition,
production-build transitions, live resource use and hardware whole-frame benefit
remain unverified for this candidate. The previous single-entry and rejected
byte-comparison sources/results are retained separately in the private directory.

## Opt-in runtime integration

`XV_PALETTE_PREFIX_REUSE=1` now enables this path in `xk_palette.c`. It defaults
to Off and requires the Halo CE 3925 profile, recompilation and the existing
native model-palette feature. Non-ARM compilation with this feature enabled is
rejected. Only the palette object owns the option; a content-sensitive stamp
handles repeated On/Off builds. No guest code regeneration is needed.

The existing math ownership guard covers cache lookup, comparison, arithmetic
and publication. `[palette-prefix]` reports hit/miss/eviction counts, hit/miss
prefix matrix counts and pool storage. Reporting resets counts but retains cached
inputs. Reuse remains exact-content-based even if addresses or maps are recycled.
The final matrix and guest context/scratch publication remain on the old path.

All 142 paired comparisons pass against the integrated source (test-only symbol
renaming and reset access). Six production ARM build transitions pass, and 107
unrelated objects remain identical. Off restores the preceding complete palette
object byte for byte. Malformed selectors and missing native-palette/profile/
recompilation prerequisites fail before compilation. The first harness expected
no unrelated initial compilation; the copied build also refreshed its version
consumer with identical output. The corrected harness permits that initial
refresh only while still asserting every unrelated object hash; subsequent
feature transitions compile only the palette object.

Receipts are `../palette-prefix-cache/results.json` and
`build-gate-2/receipt.json`. Hardware cache reuse, memory headroom, whole-frame
performance and original-lift composition beyond the prior native-batch proof
remain to be established. This is an opt-in trial, not a release-default change.
