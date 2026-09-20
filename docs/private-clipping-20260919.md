# Private-stack clipping candidate

Perf.40 gameplay identifies contended acquisitions inside `clip_region_native`.
The optional `XV_CLIP_PRIVATE=1` path shortens the B71C0 guard when all mutable
inputs, outputs, arguments and scratch storage belong to the calling worker.
Ordinary builds default to zero. This candidate is not installed on hardware.

The original guard is acquired, shared counters are updated, and the original
stack probe runs first. Admission then checks the exact worker context and
outermost guard, unchanged private stack mappings, count/capacity bounds, no
optional output pointers, disjoint control/plane/output spans, canonical image
constants, forward string direction and adequate instruction budget. Identical
input/output remains valid because B71C0 uses its original stack temporary.
Other aliases, shared spans, nested transactions, hold instrumentation and
uncertain cases retain the original guard. Arithmetic, output order and guest
state are unchanged. The fused region's completion counters remain atomic.

At most 64 input vertices and capacity 64 are admitted. Capacity checks precede
output appends; exceptional arithmetic does not remove the bounded loop count.
The 65,536 remaining-backedge requirement keeps the admitted region away from
the worker instruction-budget stop. No owner-service call occurs between release
and the inner clip return; the next original lock boundary can park the worker.

## Current validation

- `tools/test_clip_private_admission.py`: 118 ASan/UBSan cases covering mappings,
  boundaries, aliases, guard depth, worker context, constants and controls.
  Admission preserves guest context and memory, including on rejection.
- `tools/test_clip_private_execution.py`: 512 ASan/UBSan owned-image cases
  compare the original wrapper, existing locked fused wrapper and candidate.
  Complete guest context/memory match; 1,623 inner clips actually release.
  Host floating-point exception flags match locked versus released fusion.
  Cases include in-place, shared-input, shared-output and enclosing-lock paths.
- The changed worker backend and generated region compile with VitaSDK for
  Cortex-A9/Thumb/NEON.

The initial execution harness incorrectly entered the fused interior directly;
its failures were not accepted. It now uses the production first-positive-test
wrapper hook. Host exception flags are compared against the existing fused
implementation, not used to assert equivalence of host and ARM FP behavior.

These fixtures are local correctness tests, not hardware FPS comparisons.
They do not establish actual parallel scheduling correctness or hardware gains.
Concurrent production-worker/owner-service tests and ARM context/memory/FPSCR
execution remain required before enabling this in a cumulative hardware build.
Private generated code and receipts are in `../clip-private-execution/`.

The candidate is intentionally bounded rather than removing the shared guard
from all clipping. On hardware the release counters must show how much work
qualifies; they cannot by themselves establish a frame-time improvement.
