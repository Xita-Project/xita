# Native subcluster visibility prototype

Blood Gulch and most campaign BSPs use `52E10` to test subcluster bounds and
publish visible triangle bits. The new experiment in
`tools/experiments/subcluster_bounds.c` replaces the expensive emulated
bounding-box calculation with a data-only operation. It is **not linked into
the game**. The preceding guarded portal-clipping runtime `639b47fd` remains
packaged and awaiting hardware connection; this experiment has not changed it.

## Algorithm and boundary

The original `5C300` first checks two enclosing axis-aligned boxes. It then
tests all eight corners against four side planes, accumulating which planes
exclude any or every corner. The `52EC1` caller passes zero for the additional
reverse-corner test, and consumes AX as a visibility predicate.

For finite coefficients and ordered finite bounds, each original fixed-order
plane expression is monotonic in each coordinate. Selecting the minimum and
maximum corner for that plane therefore needs only two evaluations. This avoids
the original guest-stack corner array and repeated x87/register/page machinery.
It preserves each plane's distinct addition order, double intermediates and
strict comparison against zero. It does not use a center/radius formula or
reassociate the arithmetic. Compile with `-frounding-math -ffp-contract=off`.

The result contains the original classification and scheduler debit: zero
backedges for broad-phase rejection, seven otherwise. Copied frustum/box inputs
are read-only and output slices are independent. The kernel and batch entry
compile to 584 bytes of ARM text with no global data. The original's other
callers and its optional reverse-corner behavior are outside this interface.

`tools/audit_subcluster_contract.py` independently verifies the owned executable,
pins both routines, checks the zero argument and result consumer, and confirms
that all 29 decoded leaf memory-write sites use the stack. It also checks the
three backward branches, including those in the unselected optional path.
Stack-only writes do **not** establish freedom from physical aliases.

## Evidence

- **5,952 retained-ARM comparisons** match classification, returned stack and
  remaining budget. They cover all 16 rounding/flush/default-NaN combinations,
  independently mapped page crossings, each x87 TOP, plane/AABB boundary values,
  signed zero, subnormals and extreme finite coefficients and coordinates.
- **83,360 comparisons** use all 41,680 subcluster boxes from 24 owned maps and
  82 BSPs, including all 181 Blood Gulch boxes. Two synthetic views per box are
  used, with FP modes distributed across boxes. These are not captured camera
  views or measurements of how many boxes gameplay actually tests.
- **25 enclosing-pass comparisons** run the retained `52E10` ARM object against
  a privately prepared caller using the prototype. External memory, visible
  triangle bits/count, callee-saved registers, returned stack, logical x87 depth
  and exact budget match. Cases include duplicate surfaces and clusters, empty
  lists, the global frustum, existing bits, the 16,384-surface cap, large surface
  lists and all 16 FP modes. Dead guest stack and caller scratch FP/flag state
  are not compared as live outputs; this is not a complete enclosing-engine
  state proof.
- **128 two-worker partitions**, each with 1,027 boxes, match serial results
  under four host rounding modes with unchanged inputs and disjoint outputs.
  Address and undefined-behavior sanitizers pass. This tests the pure operation,
  not the Vita queue, production admission or hardware core utilization.

The classifier experiment also exposed expensive serial surface publication.
`xs_test_surface_union` in the private ARM fixture replaces that loop while
keeping the first-unseen-surface stop at capacity and exact backward-branch
debits. The preceding and combined pass comparisons both pass. A synthetic
ordered case changes from 63,680 to 5,627 modeled ARM instructions. A bulk case
with 1,792 unique surfaces changes from 291,021 to 98,221; optimizing bounds
alone left it at 268,040. This identifies a second useful optimization rather
than assuming the math is the entire cost.

These are instruction counts, not Vita cycles, frame times or FPS gains. The
older native-bounds experiment retained much more emulated state and showed no
established hardware benefit; its result does not validate this new algorithm.

## Runtime integration still required

1. Admit only the pinned caller on the registered owner, with diagnostics,
   mapping validity, finite/ordered inputs, constants and physical aliases
   checked before mutation. Keep the existing path for every unsupported case.
   The surface prototype assumes valid nonnegative surface indices; production
   must establish their range and complete mapped extents.
2. Check the live-state contract through the enclosing visibility consumer,
   including physical x87 scratch, emulated status/flags and FP exception state.
   Preserve the original budget and callbacks. A batch cannot silently skip a
   yield that joins workers or changes the guest owner.
3. Capture bounded frustum/box batches into native-owned memory. Workers may
   classify those copies; the owner must publish surfaces in original order.
   Stop or invalidate speculative results at an original scheduling boundary
   unless their lifetime across it is proved. Carry the owner's FP controls to
   workers. Do not retain borrowed guest pointers across jobs.
4. Qualify the actual queue, failure paths and runtime guards, then integrate
   with the cumulative build. Verify a fresh runtime hash and ordinary hardware
   valley/campaign gameplay at standard settings before attributing any gain.

Private receipts: `direct-cluster-query/subcluster-bounds-20260918`, including
`contract.json`, `arm-with-union/result.json`, `owned/result.json`,
`pass-with-union/result.json` and `parallel.log`. Owned data and generated game
bodies remain outside Git. Device requests still timed out during this work;
the last verified installed runtime remains `175f18da`. Stable 20 FPS remains
unproven.
