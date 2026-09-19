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

## Physical Vita result

`0.2.0-perf.10 / ae13c9d+` was installed and boot-confirmed in slot 0. Runtime
SHA256 is `4a4185483c78f092d70a59b7f32922a5391af9d6d7ad4edade9a27422d881ff3`.
The package preserves the preceding assets, update helpers and Halo 2 bundle;
only `game-a.self` and `boot-game.txt` differ from perf9. Incremental default,
explicit Off, On and On-noop configuration checks passed, as did rejection of
invalid values and the missing prerequisite.

The same Normal Pillar of Autumn save loaded with the marines visible, native
resolution and graphics settings unchanged. In the last twelve complete
60-frame ordinary-play windows, all completed-prefix matrix declines disappeared
(29,778 in perf9, zero in perf10). Median natively prepared nodes rose from
14,817.5 to 17,748 per window, about 19.8%, while pose declines remained small
(632 total). There were no constant or produced-output declines. These are work
counters, not a 19.8% frame-rate gain.

Median total frame time was 78.2 ms / 12.8 FPS versus perf9's 78.4 ms / 12.8 FPS.
That difference is too small to call a clear overall gain. Tick-owner elapsed
fell from 36.444 to 35.746 ms, while render-owner elapsed rose from 40.089 to
40.505 ms; both include nested work/waits and scene variation. Draw count stayed
at a median 152/frame. Capture was 5.978 ms, streams 6.824 ms and join 0.042 ms.
The option remains in the cumulative research build because it measurably reduces
fallback work without an observed frame-time regression in this capture.

With this gate no longer rejecting the bulk of candidate work, further hierarchy
numeric relaxation is not the next priority for this checkpoint. Remaining tick
and render work needs a larger reduction. Evidence is under
`ce-perf10/gameplay/` (`checkpoint.log`, `.png`, `checkpoint-summary.json`,
`checkpoint-hierarchy.json`) in the private unified-games workspace. No diagnostic
benchmark was used, and this short checkpoint capture does not establish long
session or combat stability.
