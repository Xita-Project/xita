# Typed portal polygon experiment

This records the initial prototype. The kernel has since moved to
`recomp/kernel/xk_portal_polygon_math.c`; see the
[guarded runtime integration](typed-portal-integration-20260918.md) for the
subsequent owner checks, deeper traversal tests and deployment status.

The next visibility optimization replaces emulated registers, guest stack
scratch and repeated address translation with a small native polygon operation.
At this prototype milestone it was **not linked into the game or installed on
Vita**. Hardware was running the cumulative `175f18da` clipping-span build.

## Boundary found in the owned executable

The caller audit verifies three distinct reachable direct calls to `0xB7F10`:
`0x530CE`, `0x534D5` and `0x561E5`. The proposed admission is specifically the
portal traversal call at `0x534D5`, inside `0x532E0`.

That caller immediately tests and stores AX as a signed polygon count. A
positive result passes the output polygon to the next traversal; `-1` passes
the parent polygon instead, and zero skips the child. It sets ECX/EDX before
recursion and overwrites EAX/ECX at the loop continuation. It contains no direct
x87 instructions. This supports a narrower data interface at this call site;
it does not justify globally replacing the function or discarding arbitrary
guest context state.

The typed operation takes point arrays, clipping-boundary points, capacity and
tolerance. It uses two bounded native work arrays. Double intermediates and the
engine's float rounding points remain, including the asymmetry in constructing
an edge plane, near-plane behavior, duplicate removal and capacity failure.
Returned count is authoritative; output is only consumed for a positive count.

An explicit work result counts the original backward branches. A prospective
adapter can debit the cooperative scheduler budget without maintaining every
emulated loop register. For admitted counts up to 256, `edges * 257 + 1` is a
conservative upper bound on those deductions. The experimental caller adapter
falls back when the available budget is insufficient, keeping original yields.

## Findings and validation

Two initial failures refined the implementation:

- Unaligned input words crossing independently mapped pages do not behave like
  the original lift's integer word copies. The proposed native admission needs
  word alignment; ordinary aligned points can still cross page boundaries.
- Ordinary compiler optimization moved a floating-point negation across a
  multiply, changing directed-rounding results. Compiling the typed kernel with
  `-frounding-math` and `-ffp-contract=off` resolved the observed differences.

Final local checks:

- **2,048 ARM geometry cases** compare a freshly emitted, fingerprinted original
  against the typed operation. Returned counts, every positive-result output
  byte and remaining scheduler budget match across all 16 combinations of
  rounding, flush-to-zero and default-NaN modes. Inputs are finite, word-aligned
  and disjoint; independently mapped pages are exercised. Results include 1,188
  positive polygons, 364 empty results and 496 capacity failures.
- **15 whole-traversal cases** use the exact retained ARM objects from the
  installed clipping-span build. Every guest byte outside the dead recursive
  stack region, callee-saved registers, return stack pointer, logical x87 depth
  and remaining scheduler budget match. Scenarios include crossing/empty
  polygons, parallel portals, cycles, a diamond, noncontiguous pages, rounding
  modes and low-budget fallback. In the low-budget case, full final context and
  FPSCR also match while the typed path is bypassed.
- The graph scenarios reach at most two actual recursive traversal levels.
  A 16-cluster chain fixture is not evidence of 16 active recursion levels.
  Deeper visible graphs still need qualification before deployment.
- Two deliberately broken adapters (zeroing the result count and shifting
  output geometry) are rejected by the whole-traversal comparisons.
- **1,936 native calls** pass address and undefined-behavior sanitizers with
  exact-size input allocations and maximum workspace/capacity combinations.

The fresh original lift executes 12,248,267 modeled ARM instructions over the
128 ordinary-mode polygon cases; the typed adapter executes 1,356,127. This
comparison is against the independent original lift, not the already optimized
installed helper.

The whole-traversal comparison does use the installed objects: an ordinary
projection case falls from 27,196 to 13,679 modeled instructions, a crossing
case from 27,399 to 13,711, and the diamond case from 76,111 to 41,355. The
low-budget fallback has small added admission overhead. These are synthetic
instruction-model results with modeled libc copies, **not hardware cycles,
frame-time measurements or an FPS prediction**.

The compiled typed kernel and duplicate helper occupy 982 bytes, while the
geometry-only adapter currently reserves 10,336 bytes of native stack. That
adapter copies all inputs and owns its scratch; it has not yet been optimized
or qualified as a production interface.

## Integration still required

`xv_preempt` calls `xk_yield`, which joins outstanding object jobs before another
guest fiber runs. Original math locks also serve worker coordination. Those
effects cannot be removed just because geometry outputs match. The next
production adapter should:

1. Admit only the pinned portal call on the guest owner, with workers quiescent,
   diagnostics disabled and enough remaining budget to avoid a skipped yield.
2. Validate finite bounded inputs, expected constants, alignment, mapped spans
   and physical aliases, including aliases with guest scratch/stack pages.
   Unsupported cases must use the existing cumulative implementation.
3. Establish the full caller state contract, including discarded physical x87
   scratch slots and flags, through the enclosing visibility consumers. Preserve
   live values, logical stack state and exact budget deductions.
4. Check deeper visible graphs, admission failures, worker interactions and
   native stack usage. Then build and verify a fresh runtime on physical Vita
   and check ordinary valley/campaign gameplay at standard settings.

This native data interface also makes later parallel work easier to reason
about, but this experiment does not add another visibility worker. Portal
traversal order and shared visibility outputs still require an ownership plan.

Private receipts are in `direct-cluster-query/typed-polygon-20260918`:
`caller-contract.json`, `oracle-budget/result.json`, `tree-admission/result.json`,
the two negative-control directories and `memory-check.log`. The read-only
device receipt confirmed runtime `175f18da`, slot 0, benchmark mode zero; no
update or restart was performed during this experiment. Stable 20 FPS remains
unproven.
