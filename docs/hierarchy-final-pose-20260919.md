# Retaining the final original hierarchy node

`XV_HIERARCHY_FINAL_NORMAL=1` allows small finite normal terms in the final
worklist pose. That pose is still processed by the original guest loop; the
native batch does not calculate its matrix. Every skipped pose, completed-prefix
matrix and produced matrix keeps the existing numeric gate. Subnormals,
Inf/NaN and values above the prior upper bound remain excluded. The default is
Off, and enabling it requires native model hierarchy support.

The existing parent-before-child validation guarantees that this final node is
not needed as an input by an earlier batched node. Publication, queue length,
scheduler budget, retained original iteration and shared guard are unchanged.
No tiny values are rounded to zero and no native arithmetic domain is widened.

The exact production helper passes 145 private ARM cases against both the current
native-leaf loop and an independent original loop/leaf lift. They compare the
complete context, 4 MiB arena, FPSCR and ordered callback-entry snapshots.
There are 130 newly admitted probes, 15 unchanged direct declines and 10 matching
callbacks. Cases include tiny final translation/quaternion/scale, uniform tiny
quaternions, minimum normal values, shuffled IDs and rounding/status modes.
This proves the tested child-loop region 0x8E0F0–0x8E5D0 with real leaves; it is
not a full outer 0x8DDF0-function or hardware performance claim.

The committed host regression additionally verifies each of the final pose's
eight words can be tiny while keeping actual batched words restricted. It compares
context/memory/FP behavior and retains disabled-mode checks. Both default Off
and selected On pass; On also passes ASan/UBSan. Build flags are strict 0/1 and
only rebuild the hierarchy unit when this option changes.

Two joined diagnostic rows make the next decision concrete. Numeric declines
are separated into constants, completed-prefix matrices, pose inputs and produced
outputs. Output failures record computed nodes, successfully computed nodes then
discarded, and the potentially salvageable prefix before one retained iteration.
These count retry-inclusive work, not independent recoverable batches or FPS.
The final-node row counts only successfully published batches that depended on
the new final-pose exception. There are no additional guest reads or FP operations
for decline classification, and reporting retains the existing shared guard.

Private qualification and source hashes are under `hierarchy-final-pose/`;
host checks are under `hierarchy-final-production/` in the unified-games workspace.
