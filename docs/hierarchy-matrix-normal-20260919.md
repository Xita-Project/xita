# Preserving small normal matrix terms in native model batches

The perf9 campaign capture found 98.0% of numeric hierarchy declines at the
existing parent-matrix check. The batch previously required every nonzero matrix
term to have magnitude at least 2^-30. Small finite normal terms can legitimately
remain after matrix composition and cancellation.

`XV_HIERARCHY_MATRIX_NORMAL=1` lowers that limit to the smallest normal float,
2^-126, for completed-prefix and produced matrices only. Signed zero remains
allowed, the upper bound stays 2^30, and subnormal/Inf/NaN terms remain excluded.
Actually batched pose inputs retain their existing range. The separately qualified
final-original-pose allowance remains independent. There is no rounding to zero,
new parallel ownership, changed arithmetic, reordered publication or reduced
scheduler accounting. A rejected speculative output still restores entry FPSCR.

The option defaults Off and requires `XV_NATIVE_MODEL_HIERARCHY=1`. Its strict
0/1 configuration and incremental build stamp affect only `xk_hierarchy.o`.
The existing stage counters measure the resulting declines, and an additional
joined row records whether the matrix option is enabled.

## Qualification

The exact production helper passed 112 focused Vita-compiled ARM cases against
both the native-leaf loop and an independent original loop/leaf lift: 64 direct
admissions, 48 unchanged declines and eight matching callback-entry snapshots.
Complete context, 4 MiB arena and FPSCR matched across the three paths. Tests
covered all rounding modes, flush-to-zero off/on, tiny parent basis/translation,
scale underflow, cancellation at the normal/subnormal boundary, propagation to
later children, disabled paths and exceptional input declines. In particular,
an output of `0x00800000` was admitted, `0x00400000` declined with FZ off, and
the corresponding flushed-zero scale result was admitted with FZ on. These are
executed boundary cases, not just inputs named for their intended behavior.

This qualifies the tested child-loop region 0x8E0F0–0x8E5D0 and real leaves;
it is not proof of the whole outer function or a performance result. Private
evidence is in `hierarchy-matrix-normal/production/` in the unified-games
workspace. See [the perf9 hardware result](hierarchy-final-pose-20260919.md)
for the measurement that motivated this change.

The host regression exercises tiny normals in each of the 13 parent-matrix
words and checks five excluded values per word. Both default Off and selected
On pass 222 full hierarchy comparisons per enabled/unset/disabled/math-disabled
mode; selected On also passes ASan/UBSan.
