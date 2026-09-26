# Collision transform register lowering — perf258 candidate

September 26, 2026. Hardware result pending; not yet deployed.

The existing native ray cast replaces 88E90 and its BSP traversal, while
1731D0 still prepares object collision rays using B6210 (inverse transform),
B5EA0 (point transform) and B5E40 (direction transform). B5EA0 already has a
native entry hook and is left unchanged. B6210 and B5E40 still use memory-backed
x87 slots in the maintained perf257 stage.

Both have register-lowered bodies in the existing regeneration. The splicer
confirmed compatibility with the maintained bodies. Differential tests then
found an exceptional-value mismatch in **B5E40**, on both host and Pi:
case 1588 combines infinite scale with a quiet NaN vector component. Both
results are NaNs, but their payloads differ in an x87 slot and the output's
third component. This is not a finite-value mismatch. Nevertheless B5E40 is
excluded from the candidate; the existing body is retained.

## Selected change

Perf258 changes **B6210 only**, through the existing `--x87-regs` lowering.
No inverse algorithm, precision, transform order, native hook, physics rule,
cache or worker policy is changed. All other translated function bodies compare
byte-identically to perf257. The candidate retains perf257's optional diagnostic
hooks, but ordinary gameplay uses `XV_SCENE_PHASES=0`.

```sh
python3 tools/test_collision_transform_registers.py BASELINE/code_016.c \
  REGISTER_REGEN/code_016.c --output PRIVATE_OUTPUT --function inverse \
  --extra='-fsanitize=address,undefined'
```

The test extracts private bodies and compares complete xctx and the 4 MiB
synthetic guest arena. Cases exercise all x87 TOP values, status variation,
rounding controls, direction flag, zero/unit/nonunit/exceptional scales,
overlapping inputs/outputs, virtual aliases, stack overlap and page boundaries.
A shared REP STOSD stand-in checks the transform's string-fill contract;
runtime memory-watch callbacks are not covered. The test does not prove game
behavior, actual input frequency, or concurrent caller ownership.

**1,728 inverse cases passed host ASan/UBSan and Pi ARM**, including the
expanded control-word/direction-flag fixture. The ARM binary targets Cortex-A9
Thumb/NEON with hard float, `-O2`, `-fno-strict-aliasing`, `-ffp-contract=off`.
It ran on Pi core 0. Four alternating-order repetitions measured about
238–242 ns/call for the reference and 190–194 ns/call for register lowering,
including the context reset. These warm synthetic times are not Vita timings.
The standalone ARM symbol shrank from 3,016 to 2,136 bytes.

The initial two-function run deliberately remains a failing result for B5E40;
do not describe that run as wholly passing or silently remove its failing
case. `--function vector` reproduces the excluded candidate's discrepancy.

Private candidate directory: `collision-transform-candidate/`, containing
body hashes, commands, logs, one-function audit and the build stage. Generated
code and game inputs stay outside Git. The full Vita build is in progress.
Next: confirm the built candidate, deploy a compatible package with rollback,
then inspect ordinary a30 gameplay/firing with timers off. A frame-rate gain
and the 20 FPS objective remain unproven.
