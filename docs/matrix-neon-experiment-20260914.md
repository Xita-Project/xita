# September 14: optional NEON matrix experiment

This experiment evaluates a bounded SIMD version of the existing native matrix
multiply. It is compiled out of ordinary builds and disabled at runtime when
included. Instruction counts show only a small saving after admission checks;
there is no hardware FPS result yet.

Build with `XV_NATIVE_MATRIX_NEON=1` to include it. In that build,
`XV_NATIVE_MATRIX_NEON=1` in the runtime configuration enables it, or the remote
`matrix-neon` benchmark runs an off/on/off comparison and restores the original
setting. An ordinary build rejects that benchmark because the helper is absent.
No dashboard default changes.

The helper accepts the caller's already-validated page, alignment, scratch and
alias layout. All numeric checks finish before output changes. Rotation inputs
are loaded before writes; right-hand rows are consumed before an exact in-place
output overwrites them. Partial overlaps and split mappings retain the existing
fallback. The guest's eight SSE vectors and double-precision scale result retain
the existing native helper's operation order.

NEON does not accumulate VFP exception flags in the same way as scalar VFP.
Admission therefore requires masked exceptions, nearest rounding and an already
set inexact flag. Each input must be signed zero or have magnitude from
2^-30 through 2^30. These bounds keep nonzero intermediates normal and finite;
numeric and FP-control cases outside this subset use the preceding scalar code.
The `[matrix-neon]` report counts accepted, disabled, FP and numeric cases.

## Validation

The linked ARM candidate passes 48,000 comparisons against the preceding full
game ELF. Each checks the whole guest context, a 2 MiB guest arena and FPSCR:

| Fixture | Comparisons | Purpose |
| --- | ---: | --- |
| Bounded matrices | 6,144 | Random finite inputs, exact/physical/partial aliases, alignment and page boundaries |
| Exceptional floats | 6,912 | Zeros, subnormals, infinities, NaNs, rounding and sticky flags |
| Numeric boundaries | 24,960 | Both neighbors of the minimum/maximum at every input component, plus exceptional values |
| Default setting | 3,840 | Compiled helper stays disabled |
| Explicitly off, random bits | 6,144 | Scalar fallback preserves state |

The bounded fixture accepts 700 calls; the boundary fixture accepts 5,680 and
declines 5,664 numerically. Other calls use existing layout fallbacks. Counts are
not representative gameplay coverage.

In the bounded fixture, accepted calls average 446.16 preceding instructions
and 439.89 candidate instructions. Including all layout fallbacks, that fixture
executes about 0.09% more instructions overall. These are interpreter instruction
counts, not Cortex-A9 cycles or a gameplay improvement.

With the build option omitted, the complete math object is byte-identical to
the preceding build. Host benchmark tests check admission, priority, completion,
cancellation, absent-helper rejection and setting restoration. Real loopback
HTTP tests cover the new remote kind and benchmark exclusion.

Use `tools/test_arm_math_runtime.py` with private baseline/candidate ELFs,
`--functions f_000B5B40`, and `--native-matrix on`, `off` or `default`.
Fixture options are `--bounded-matrices`, `--matrix-boundaries`,
`--float-edges` and `--random-floats`; they are mutually exclusive. The complete
boundary cycle uses `--cases 6240`. The runner requires VitaSDK, Unicorn and
pyelftools. Owned game data and linked game artifacts remain outside Git.

Private reports are under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/matrix-neon`.
Emulator off/on/off and a physical comparison remain the next gates.
