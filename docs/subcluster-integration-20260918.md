# Guarded native subcluster visibility

The cumulative runtime can now use native bounds classification and ordered
surface publication inside Halo CE's `52E10` visibility pass. Blood Gulch uses
this path. The changes remove emulated register, stack and address-translation
work while preserving which surfaces are selected and their original order.
Rendering settings and existing optimizations remain in place.

`XV_TYPED_SUBCLUSTER=1` enables the experiment; its repository default is off.
It requires the Halo CE 3925 profile, native clipping and the registered
owner/worker backend. `tools/subcluster_hook.py` pins the owned executable and
the exact cumulative caller body before preparing one generated unit. The
original implementation handles every unsupported case.

## Admission and ownership

Both operations require the registered guest owner and a quiescent worker
boundary. Census, watch, trace and the older native-bounds comparison mode
disable them. Guards check stack bounds, caller return addresses, alignment,
mapped extents, physical overlaps, constants and scheduling budget before any
guest mutation. Nonfinite and unordered bounds decline through integer checks
that leave floating-point status untouched.

The classifier copies its frustum and box into native local storage. It uses
the [qualified support-corner algorithm](subcluster-bounds-20260918.md), including
the original expression order, double intermediates and exact backedge debit.
The ordered publisher validates and copies at most 4,096 surface indices before
writing. It retains duplicate handling, the first-new-surface stop at the
16,384-surface cap, and the original loop-budget deductions. Neither operation
retains a guest pointer after returning. The publisher uses about 17 KiB of
temporary native stack space.

There must be enough budget to complete an admitted operation without yielding.
Otherwise the original code executes its callbacks. Dead emulated scratch
registers, physical x87 scratch slots and incidental floating-point exception
flags are not reconstructed on successful calls. Tests compare the live outputs
through the enclosing visibility consumer, not complete engine-state identity
or arbitrary interior entry points. Diagnostic declines compare complete state.

## Qualification

- **34 retained ARM runtime comparisons** cover the actual guarded objects and
  preceding cumulative objects. Cases include duplicate surfaces/clusters,
  empty lists, global frusta, existing bits, capacity limits, large lists,
  all 16 FP control combinations and the enclosing `539C0` consumer.
  External memory, callee-saved registers, returned stack, x87 stack pointer
  and remaining budget match. Scheduled callbacks also match their number,
  surface bits/count, guest stack and budget, including a ten-callback case.
  Queued-worker fallback also matches complete context and FP status.
- The actual host worker backend passes **33 admission checks**. Both native
  operations and **51 rejection cases** pass address/undefined-behavior
  sanitizers with unchanged guest memory, page table, context and FP state on
  rejection. ARM comparisons use an explicit platform fixture; these host
  checks separately exercise real worker admission.
- **Three build transitions** reproduce enabled objects and byte-identical
  disabled objects. Nine invalid values or missing dependencies reject.
- The prior pure-math qualification covers 5,952 ARM cases, 83,360 owned-map
  comparisons and 128 host worker partitions. These are correctness results,
  not hardware utilization or frame-rate measurements.

Including runtime admission, a synthetic ordered pass changes from 63,680 to
15,824 modeled ARM instructions. A bulk case changes from 291,029 to 136,034;
the larger visibility consumer changes from 188,273 to 142,612. These model
counts identify reduced work but do not predict Vita cycles or FPS. Fallback
adds a small guard cost.

## Package and next step

The cumulative VPK builds successfully with native portal clipping retained.
Two existing objects change and two native subcluster objects are added; the
other 95 objects match the preceding package. Of 1,588 package members, only
the game runtime and boot manifest change. The updater contract is unchanged.
Runtime SHA-256 is
`73c30a348ac87df492963f756a6379a8bdabc460f275e5b5bbe703bd3d5dec49`.

Private receipts are in `direct-cluster-query/subcluster-integration-20260918`.
The authenticated device request timed out; no upload or restart was sent.
The last verified installed runtime remains `175f18da`. Deploy the qualified
package after verifying the current slot/hash, restart, and capture ordinary
Blood Gulch and campaign gameplay at native resolution and standard settings.
User-reported 12 FPS in the valley predates this candidate and is not attributed
to it. Stable 20 FPS is still unproven.

This change does not dispatch additional worker jobs. The pure classifier is
ready for bounded copied-input jobs, but those still need FP-control propagation,
queue integration and a lifetime contract across original scheduler boundaries.
The owner must continue publishing surfaces in original order.
