# Model routing audit — September 19

The [crowded-view timing](scene-bucket1-detail-20260919.md) makes the shared
`5B710 -> 5B4A0 -> 5B190 -> A26B0` path a priority. Inspection of the retained
perf.24 generated code now extends that chain through `A2380`. This is a source
and isolated ARM experiment, not a hardware performance improvement.

## Repeated traversal and its dependency boundary

`A26B0` builds a stack-owned model packet, optionally prepares a matrix palette,
and publishes packet pointers through shared render state before calling
`A2380`. Reusing that whole stack address is unsafe. `A2380` traverses model
regions, the selected permutation/LOD geometry, its parts, material references,
and the global tag table. It selects ordered material passes: the incoming
flag's bit 1 selects either pass 2 alone or passes 0 through 2. Each pass starts
the region/part traversal again.

The part-selection prefix at `A2443..A247A` resolves the part, material reference,
shader handle and shader type. Actual publication happens later through
`70110`, `6B060`, or `6EFC0`; the latter route also transforms a point through
`B5EA0`. There is a linked-output fixup after traversal. Backedges retain guest
preemption checks. Thus gathering and publishing all parts concurrently is not
an equivalent implementation of this routine.

A future prepared routing list could avoid repeated pointer walks, but must
establish lifetime and mutation rules for the region selection, LOD, part flags,
material references and tag table. Dynamic material values remain live at their
original publication boundaries. Neither immutability across child calls nor
across preemption is established by this audit.

## First native prefix prototype

A private prototype replaces both generated copies of the part-selection prefix
with local intermediate values and publishes the same registers and final flags.
It retains original integer multiply/shift helpers, including their otherwise
inactive flag storage. It does not cache values across calls, reorder draws,
change workers or remove preemption boundaries.

The actual retained complete `A2380` body and candidate compile for Cortex-A9
Thumb using VitaSDK. Twenty-four paired executions cover zero, one, eight and
32 parts; one- and three-pass entry flags; and unchanged or callback-mutated
material types. All 360 child/preemption frontiers match complete context and
8 MiB guest arena hashes. Final complete context, arena and native FPSCR match.
The fixture uses noncontiguous guest pages and frequent expired budgets.

| Parts | Passes | Original instructions | Candidate instructions | Reduction |
| --- | ---: | ---: | ---: | ---: |
| 8 | 3 | 9,247 | 9,132 | 1.24% |
| 8 | 1 | 3,001 | 2,962 | 1.30% |
| 32 | 3 | 33,527 | 33,060 | 1.39% |
| 32 | 1 | 10,209 | 10,050 | 1.56% |

These rows include deterministic stand-in children, not the real shader,
allocator, driver or scheduler work. They are instruction counts, not CPU
cycles or predictions of FPS. Coverage does not establish all shader types,
alias cases, real publication behavior, or hardware concurrency safety.

The mutation fixture changes the first material from type 4 to type 5 at a
child boundary. In the 32-part three-pass case, the retained routine then makes
44 child calls rather than 42. This is a constructed dependency counterexample,
not evidence that the live game performs that mutation. It establishes why
assuming an immutable routing list without checking actual child writes would
be incorrect.

## Decision

Do not deploy this small prefix rewrite by itself or attribute any current FPS
to it. Keep it as a qualified starting point for a larger traversal rewrite.
The next useful boundary is the full region/part traversal with live publication
callbacks: first establish which routing fields those callbacks and preemption
can change, then remove repeated traversal or batch independent preparation
while preserving their observable state and order. Also retain the possibility
that downstream material publication dominates; the inclusive 22.2 ms model
interval does not prove the traversal itself is expensive enough to close the
frame-time gap.

Private reproducible sources, compiler commands, function-level instruction
counts and results are under `../model-route-prototype/`. Original generated
code stays outside the source repository. No device update was made for this
experiment; perf.24 remains the last verified installed build.
