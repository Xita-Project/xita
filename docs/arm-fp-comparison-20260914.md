# September 14: ARM floating-point comparison candidate

The Thumb-2/VFP implementation of `x87_compare` now compares its operands once
and uses the resulting unordered, less-than and equality flags. The preceding
portable expression emits up to three comparisons of the same operands with
the current VitaSDK compiler. Other targets retain the portable implementation.

The guest x87 status update and optional EFLAGS update retain their existing
behavior. The replacement uses a quiet VFP comparison, preserves signaling-NaN
exception behavior, and does not change arithmetic precision or rounding.
It changes no scheduling or rendering settings.

## Validation and limits

The reproducible synthetic ARM fixture passes 102,400 executions at `-O2` and
another 102,400 at `-O3`. Each compares the complete guest context and native
FPSCR against the preceding compiled expression. An independent condition-code
oracle checks the x87 result. Coverage includes both signs, zero/subnormal/
normal/infinite values, quiet and signaling NaNs, random bit patterns, all
six lazy flag kinds, 8/16/32-bit flag widths, and all sixteen combinations of
rounding, flush-to-zero and default-NaN controls. Native exceptions are masked;
preexisting exception/status bits are also exercised. The host runtime suite
passes with the unchanged portable path.

| Synthetic wrapper, no EFLAGS update, `-O2` | Previous instructions | Candidate |
| --- | ---: | ---: |
| Less | 29 | 25 |
| Equal | 34 | 25 |
| Greater | 34 | 24 |
| Unordered | 26 | 25 |

These counts include fixture input loads. They are not CPU cycles or measured
gameplay savings. Register allocation and caller context affect the result:
some `-O3` unordered and EFLAGS paths execute more instructions than the
preceding version. No FPS improvement is claimed.

The full game builds and passes a further 10,752 linked-ARM matrix, quaternion
and point comparisons, preserving complete guest context, memory and FPSCR.
Its updater boot is verified in the isolated CE emulator. The dashboard, main
menu, ordinary solo Blood Gulch startup, walking, turning and firing work;
the captured gameplay log contains no reported trap or crash. The emulator is
capped at 20 FPS, so this is a functionality check. A physical comparison is
still pending.

With VitaSDK on `PATH` and Unicorn/pyelftools installed:

```sh
python3 tools/test_arm_fp_compare.py
python3 tools/test_arm_fp_compare.py --optimization O3
```

Use `--output-dir` with a new directory to preserve the linked fixture and JSON
report. These tests require no game executable, assets or firmware. Private
reports are preserved outside Git under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/compare-audit`.
