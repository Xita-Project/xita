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
