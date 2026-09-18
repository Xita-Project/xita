# Captured visibility pass on existing workers

`XV_NATIVE_VISIBILITY_PASS=1` connects the copied-data worker backend to the
single prepared call `539C0:53AF2 -> 52E10`. After portal traversal, the owner
captures visible clusters; independent bounds classifications run on existing
workers plus the owner. Surface publication remains on the owner, in original
order. This cumulative candidate retains prior portal/subcluster optimizations.
Both new flags default off.

**Hardware performance and deployment remain unverified.** The remote service
timed out. The user's 12 FPS valley report predates this candidate.

## Contract

- Require the registered owner, idle object queue and disabled tracing,
  phase/census and comparison diagnostics. Unsupported cases use the original.
- Capture at most 128 visible entries, 512 boxes and 32,768 surface references,
  with at most 131,072 addressable surfaces. Validate mapped physical spans,
  count/pointer arithmetic, finite ordered bounds, and aliases against output
  and original stack scratch. Shared reads and repeated clusters are allowed.
- Require remaining loop budget greater than
  `8 * boxes + references + visible_entries`, so the original cannot yield in
  the replaced interval. Deduct exact original backedges, including early exits.
- Packets under 24 boxes stay on the owner to avoid kernel wake overhead. Larger
  packets use the configured zero, one or two existing workers. This threshold
  is an initial policy, not a measured optimum.
- Workers see copied native data only. They restore their FP environment and
  never access guest arrays, call guest functions, publish surfaces or submit
  graphics. All workers join before publication. Preserve first-unseen order,
  duplicates and the 16,384 selected-surface cap. All declines precede mutation.

The adapter adds 2,675 bytes of ARM text and 213,572 bytes of BSS, separate from
the worker packet. No new threads, stacks or per-pass heap allocations.
`[visibility-pass]` records admissions and decline reasons; `[visibility-jobs]`
records lane items. These counters are not timing measurements.

## Qualification

- **39 retained ARM comparisons** against the preceding cumulative runtime:
  ordered/duplicate/global/preset surfaces, cap handling, noncontiguous pages,
  packets through 513 boxes, FP modes, queue/budget fallback and full enclosing
  caller. Observable memory, preserved context, budget and scheduler callbacks
  match; fallback also compares full context and FP state.
- **145 original/pass comparisons and 43 unchanged-state declines**, using the
  actual pthread backend under ASan/UBSan and ThreadSanitizer. Includes worker
  counts 0/1/2, four rounding modes, remapped input/output/stack pages, distinct
  image base, bad pointers/bounds and physical aliases. A held worker confirms
  no early guest publication and rejection of foreign-thread entry.
- **779 queue calls/declines** pass both sanitizer modes after the small-packet
  change, including legacy-job handoff and FP restoration. Pure bounds math is
  unchanged from earlier owned-map qualification.
- Three VitaSDK flag transitions preserve identical disabled objects and
  reproducible enabled objects; eight invalid flag/dependency settings reject.

The ARM fixture executes classification sequentially; the pthread tests cover
the real queue separately. A reproducible Unicorn invalid-instruction exception
at a Thumb conditional-block boundary in unchanged portal code disappears when
the same objects are relinked 64 bytes later. Both comparison sides use that
relocation. No production instructions or assertions were patched.

One synthetic full enclosing caller decreases from 142,612 to 133,278 modeled
instructions, including capture but excluding real wake/join costs. Declines
can add overhead. These counts do not predict FPS.

Private receipts: `direct-cluster-query/visibility-pass-20260918`, including
`arm-qualified2`, `owner-asan2`, `owner-tsan`, `queue-asan`, `queue-tsan` and
Make/package records. Original lifted game bodies stay outside the repository.

## Next hardware check

Verify the installed runtime hash and fresh launch, then use ordinary gameplay
at native resolution and standard settings. Check admission/decline counts and
lane items alongside valley, driving, effects and campaign frame times. Watch
for culling changes and dispatch stalls. If budget/capacity forces frequent
fallback, choose a smaller capture boundary without bypassing the original
scheduler contract. Stable 20 FPS remains the goal.
