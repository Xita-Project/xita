# Call-local sampler translations — 2026-09-27

Candidate only, disabled unless XV_MATERIAL_SAMPLER_POINTERS=1 is explicitly
compiled. Perf273 remains installed; no hardware speedup is established.

The existing ordered sampler helper repeatedly translates the same two stack
slots and texture-state page. The candidate resolves those pointers once per
helper call, retains every stack/table store and argument read in original
order, and retains intermediate register updates. It is not a persistent cache
and does not skip state writes or reorder draws. Stack spans crossing a page or
wrapping address zero and guest spans overlapping the host context use the old
path. Checked-address builds also retain the old per-access path.

## Qualification so far

Private receipts: ../sampler-pointer-candidate/.

- Host ASan/UBSan and Cortex-A9 Thumb ARM/Pi fixtures each passed 16,384 complete
  context/64 KiB memory comparisons against the retained ordered helper.
- Cases vary page mappings between calls, distinct virtual pages sharing physical
  memory, stack/table overlap, byte alignment, page boundaries, wrapped virtual
  addresses and table/context overlap. No real concurrent remapping is modeled.
- Pi CPU0 timing: baseline/candidate/candidate/baseline, two million groups each,
  140.317 / 64.497 / 65.623 / 146.609 ns per group. This is an isolated noinline
  helper microbenchmark, not a predicted frame-time reduction. Production caller
  inlining, memory traffic and contention can change the result.
- The retained generated-instruction fixture completed under ASan/UBSan: 4,096
  context/memory cases each for owner and helper admission, plus diagnostic
  fallback, all passed with the candidate enabled. This uses mocked ownership
  and guest mapping; it does not establish real thread scheduling safety.

## Remaining gates

Review mapping lifetime guarantees for admitted owner/helper calls, including
render-view watchdog behavior. The pointer shortcut assumes mappings remain
stable within this no-call/no-yield sequence; it must not weaken existing memory
ownership requirements. Qualify Vita compiler output and production integration,
then add a tracked build switch and measure ordinary gameplay before promotion.
No Makefile default or currently installed build was changed. Preserve the other
qualified optimizations and the a30-perf211 save namespace.
