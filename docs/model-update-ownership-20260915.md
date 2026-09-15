# Model-update ownership audit, September 15

The private point experiment found no eligible calls in its two emulator views.
This extends the earlier [pose-job boundary audit](pose-job-boundary-20260914.md)
with the measured point caller, concrete shared readers and paths that re-enter
model preparation from collision transactions. The native hierarchy kernel was
already proposed there; this is additional dependency evidence, not a new kernel
implementation. Removing the point helper's mutex does not establish independent
object updates.

## What the original addresses establish

The normal object callback `8FB70` calls `8DDF0` at `8FC07`. `8DDF0` resolves
the object through the object table, prepares its animation/model transforms,
then calls the point helper at `8E65D`, returning to `8E662`. Independently
generated suffix functions, including `8E087`, contain copies of this code.
The observed return PC identifies the original instruction, not which generated
entry function executed it. The canonical caller is therefore described by its
original `8DDF0` region rather than inferred from one suffix body.

| Data | Observed access | Implication for worker ownership |
| --- | --- | --- |
| Object table at `2FC6AC`, entries via `+34` | Resolves live object pointers throughout the callback | A queued object ID is insufficient without stable identity and lifetime. |
| Object `+19E` and `+1A2` | Signed relative offsets select pose data and 52-byte matrices | Output resides in shared object allocation, not the worker's private stack. |
| Object `+CC`, `+D0` | Selects another object's matrix and a node within it (`8DE84..8DEBE`) | Attached-model computation depends on another object's transform. |
| Model nodes (`+BC`, 156-byte records) | The loop traverses node links at `+20/+22`, and uses the parent index at `+24` | Node calculations are ordered by hierarchy; arbitrary per-node parallel work is invalid. |
| Object `+50..+58` | Point-helper output at `8E65D` | Three floats are published into shared object memory. |
| Object `+5C` | Copied from tag data, then conditionally scaled by object `+60` | Publication extends past the point helper's guarded call. |

The three-vector/scalar pair is consistent with bounds center and radius. This
interpretation is supported by the consumers below; no assertion of an original
debug-symbol name or exhaustive field layout is needed for the ownership finding.

## Concrete consumers and ordering

`112A0` resolves the requested object and copies both `+50..+58` and `+5C` to
caller outputs. `125A0` independently copies the same vector. `84C00` resolves
the object and passes the vector and scalar to its spatial-query path. In
`4B4A0`, the fallback at `4B534..4B577` resolves the object supplied in `EDI`,
reads both fields, and passes them to `4AA90`. These are actual object-pointer
data flows, not merely matches for an offset occurring somewhere in a function.

The generated direct-call graph contains these possible paths:

- `4C980 -> 425D0 -> 4B4A0` (shared bounds reader).
- `4C980 -> 425D0 -> 8DDF0` (model update inside an existing transaction).
- `96430 -> 95680 -> 95440 -> 42EC0 -> 42C30 -> 425D0 -> 4B4A0`.
- `96430 -> 95680 -> 95440 -> 42EC0 -> 42C30 -> 3FCE0 -> 8DDF0`.

These are static possible paths; they do not prove that every branch executes
in a given frame, or enumerate indirect calls. They do prove that assuming the
model routine is reached only from the unguarded tail of `8FB70` is incorrect.
Existing collision transactions must retain their synchronous fallback.

The outer callback also recursively visits object links at `+C8` and `+C4`,
conditional on `+CC`. The queue admission at `90294` checks object-table flags
and sometimes another object field; it does not explicitly validate disjoint
parent/child components. Root ownership and absence of overlapping subtree jobs
remain to be established rather than assumed from separate CPU contexts.

The shared point guard is not a complete publication protocol already: ordinary
stores publish matrix components earlier in `8DDF0`, and the scalar at `+5C`
is written after the point call has returned. Existing experimental whole-object
scheduling remains unproven for these dependencies. This audit does not widen
the locks or remove any of them.

## Next restructuring boundary

The node loop `8E0F0..8E5D0` is a stronger candidate than another private-stack
point shortcut. Its visible calls are quaternion conversion, matrix multiplication
and the existing basis helper. Animation evaluation and type-specific callbacks
occur earlier; object publication and later consumers require separate treatment.

First extract and compare the serial native hierarchy calculation against the
existing private original-call fixtures. Measure complete input gathering,
computation and publication before adding dispatch overhead. Before scheduling
this region independently:

1. Establish a supported invocation's live object identity, bounded pose/node
   ranges, parent-transform dependency, and complete output range. Decline nested
   collision invocations and unsupported layouts without changing their behavior.
2. Capture inputs at a defined owner boundary and compute a complete palette in
   private storage. Preserve parent-before-child node order and existing floating
   point behavior, including fallback cases.
3. Publish matrices and dependent bounds as one ordered operation before the
   first consumer that needs the new values. A delayed write without consumer
   coverage merely moves the race. Determine whether disjoint objects have a
   useful overlap window before making this a worker queue.
4. Compare original outputs and consumer-visible ordering in fixtures, then test
   emulator combat/attachments and hardware frame time. Smaller instruction
   counts or higher core utilization alone are not successful validation.

The earlier `A2781` render-palette batch is a different region and has already
failed to establish a consistent hardware gain; see the
[palette comparison](model-palette-batch-20260914.md). This audit does not repeat
that experiment or claim a frame-rate improvement.

Private evidence is under `model-update-audit` in the September 14
engine-restructure validation directory: generated routine extracts and their
hashes, original-address instruction lists, candidate references, and the direct
call graph. Original code and game assets are not included here.
