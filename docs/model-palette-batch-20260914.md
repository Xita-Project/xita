# September 14: bounded model-palette batch

The optional `XV_NATIVE_MODEL_PALETTE` helper now compiles with `-O3
-funroll-loops -ffp-contract=off`, matching the native leaf math unit. It keeps
its existing default-off configuration and scheduling/alias guards. The compiler
can remove intermediate register state that the original loop overwrites before
any guest handoff, while preserving every output matrix and the final context.

Before any output write, the batch checks every input word. Signed zero and
finite magnitudes from 2^-30 through 2^30 are admitted; other values retain the
existing per-matrix path. ARM uses integer NEON comparisons for this check;
the products still use scalar VFP operations with separate rounding points.
Native exception enables decline the batch. All four rounding modes and
flush-to-zero/default-NaN controls are retained.

The guard is necessary. A private unguarded trial exposed arithmetic NaN payload
differences. The existing native leaf also has an established operand priority
that can differ from the generic original lift for NaNs. The guarded batch does
not alter that fallback: accepted cases match the original lift exactly, and
rejected cases match the existing leaf, including payloads and native FP status.

## Validation

The updated ARM runner compiles the independent original lift/runtime at O2
and the native math/batch units at their actual O3 settings. It checks the entire
guest context, 1 MiB arena, handoffs and FPSCR. Every declined direct invocation
must leave the guest memory and context unchanged before the fallback runs.

- 1,728 ordinary finite comparisons across rounding, sticky status and FZ/DN.
- 1,728 exceptional-value comparisons, including infinities and signaling NaNs.
- 7,176 boundary comparisons covering all 26 input components, both ends of
  multi-node batches, signed zeros and the numeric admission endpoints.
- 1,728 explicitly disabled and 1,728 unset-configuration comparisons.
- 16,384 host comparisons with ASan/UBSan, plus the existing layout, alias,
  identity-guard, unchanged-default and guest-handoff checks.

All 14,088 ARM comparisons pass. Native trap-enable bits are read-as-zero in
this Unicorn Cortex-A9 model, so those requested modes are explicitly listed as
unsupported in its reports rather than counted as validated declines.

| Nodes in accepted fixture | Existing leaf loop | Bounded batch |
| ---: | ---: | ---: |
| 1 | 564 | 632 |
| 2 | 1,121 | 942 |
| 4 | 2,227 | 1,547 |
| 8 | 4,439 | 2,742 |
| 16 | 8,863 | 5,123 |
| 32 | 17,711 | 9,909 |
| 64 | 36,349 | 19,472 |

These are ARM instruction counts with modeled memory imports, not cycles or FPS.
The one-node case is more expensive and full-frame performance remains unproven.
The previous O2 batch was 1,824 instructions in the four-node fixture and 23,712
in the 64-node fixture under the same O3 leaf baseline. The new bounds checks are
included in the table above.

## Reproduce

Generate `original.c` privately with `tools/test_model_palette.py --xbe ...
--manifest ... --output-dir ...`. Run `tools/test_arm_model_palette.py
--reference ... --output-dir ...` for each of `--values finite`, `edges` and
`boundaries`, plus finite `--enabled off` and `unset`. VitaSDK, Unicorn and
pyelftools are required. Owned game code and linked binaries stay outside Git.

The full VPK builds with the same 1,588 packaged members; only the gameplay
executable and boot record change. Emulator gameplay and physical off/on/off
comparisons are the next gates. Private reports are under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/palette-unroll`.
