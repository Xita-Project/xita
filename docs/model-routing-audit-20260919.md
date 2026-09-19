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

## Full-traversal register-local follow-up

The next prototype keeps all eight guest GPRs local for the complete retained
`A2380` traversal. It publishes/reloads them at every child call and actual
expired-budget preemption and publishes them at every return. All memory reads,
material decisions, callbacks, linking and ordering remain live. This tests a
larger context-traffic boundary without assuming cross-pass immutability.

It passes another 48 paired complete-routine comparisons: 24 at budget 3 and
24 at budget 20,000, with 570 total matching child/preemption frontiers. The
same context/arena/FPSCR comparisons and child-stub limitations apply. These
are two fixture configurations, not actual scheduler or concurrency tests.

With unchanged materials and budget 20,000:

| Parts | Passes | Original instructions | Local-register instructions | Increase |
| --- | ---: | ---: | ---: | ---: |
| 8 | 3 | 9,142 | 9,198 | 0.61% |
| 8 | 1 | 2,971 | 3,026 | 1.85% |
| 32 | 3 | 33,062 | 33,390 | 0.99% |
| 32 | 1 | 10,059 | 10,234 | 1.74% |

At budget 3, the 32-part cases increase by 3.66% and 4.55%. Thus the negative
result is not just an artifact of frequent forced handoffs. Do not deploy this
prototype. Register-local conversion at this boundary is not a demonstrated
shortcut to the campaign target. Preserve it as negative evidence rather than
repeating the same rewrite with a different wrapper.

Direct child inspection also narrows the ownership boundary. `6EFC0` allocates
or selects a render packet, writes returned link/output fields and shared render
state. `70110` can call it, then allocate an additional packet field through
`5FD40`; its `[ebx+6c]` write is to that returned packet, not evidence that it
modifies the shader tag itself. Direct writes in `6B060` are stack preparation;
its descendants and HLE calls still require inspection. Preemption enters
`xd3d_lockstep_preempt` and can yield guest execution. A direct-write list alone
therefore does not prove routing inputs immutable.

Before attempting another traversal rewrite, attribute the inclusive model cost
to preparation, material packet construction, draw recording and waits. The
larger callbacks are still included in the live 22.2 ms scope but replaced by
stand-ins in this experiment. The experiment cannot rank their actual cost.
Private results are `local-register-budget3.json` and
`local-register-budget20000.json`, with matching fixture variants and logs.

## Perf.26 crowded capture and shared early model route

The final six 60-frame windows in `../ce-perf26/current-corridor/settled.log`
have a fixed logged camera `(-27.41,35.96,0.62)`, forward
`(-0.91,0.39,-0.11)`, and loaded/active gameplay. They average 159.47 ms/frame
and 465.5 draws/frame. This is not the perf.25 camera, so it is not a matched
speed comparison. Palette-prefix reuse is 54.22% of eligible batches and
51.85% of prefix matrices; these counters do not establish time saved.

The early `5B760` interval averages 24.96 ms, while the later `5B710` interval
averages 21.58 ms. The latter contains 5.16 ms inside the two instrumented draw
APIs and 16.42 ms outside them. There is no equivalent draw subtraction yet
for `5B760`; its whole 24.96 ms must not be called preparation self-time.

Inspection of the exact retained perf.26 generated bodies establishes:

- `5A7B0`, called by `5B760`, builds the list rooted at `2D1FDC` using two
  `52D50` calls and records its count at `2D1FD8`. It also updates shared state.
- `5B760` calls `D8C40`, then iterates that list through `5B4A0` using a
  stack-owned descriptor. Its outer loop and list backedge retain preemption.
- `5B710` later reads the same list root/count and also calls `5B4A0`, using
  its incoming descriptor. Same list storage does not prove unchanged contents
  or equivalent descriptor/render state between passes.
- `D8C40` additionally reaches `A26B0` directly, alongside `D6F70` and
  `5AE10`. Thus model-packet preparation is shared across more than just the
  later list-processing interval.

This changes the next measurement boundary: separate `5A7B0` list generation,
`D8C40`, and the early `5B4A0` loop before replacing another outer wrapper.
Then attribute shared `5B4A0 -> 5B190 -> A26B0` preparation and material
publication. Do not skip either pass or cache whole stack packets. The two
outer intervals total 46.54 ms, but that is an inclusive target envelope, not
proven duplicated work or available savings. Existing whole-register and small
prefix negative results above still apply.

The full captured scene buckets average 43.99/45.53/0.08/2.39/3.75/0.005 ms.
The broader render phase averages 99.27 ms and game-update phase 57.52 ms.
Nested scope times and asynchronous worker sums must not be added to these.
The raw CPU-observed GPU completion latency is not GPU service time.

Private extracted bodies and paired analysis JSON remain outside the repository.
This audit makes no runtime change; the installed build remains perf.26.
