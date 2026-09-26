# Collision transform register lowering — perf258 candidate

September 26, 2026. Installed and boot-confirmed; short gameplay tests completed.

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
code and game inputs stay outside Git. The full Vita build succeeded. Its
B6210 symbol is 2,140 bytes, down from 3,044 in perf257. Runtime SHA-256:
`28bd07ffe8d0d2668a17ba533f72ba0a0d9ac94f99deff06468ea3deadc688ef`.
The compatible package replaces only `game-a.self` and `boot-game.txt`,
preserving the asset contract. Remote upload/apply verified that hash and
confirmed slot 0; perf257 remains in slot 1 for rollback. The protected a30
launch uses `XV_SCENE_PHASES=0` and the in-app keep-awake lease.
## Hardware results (timers off)

All marked frame intervals were present; benchmark mode was off. These are
ordinary scripted gameplay samples, not matched A/B evidence.

| Phase | Frames | Mean ms | FPS | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Lifepod idle | 143 | 56.816 | 17.60 | 73.125 | 108.161 | 109.332 |
| Lifepod AR firing | 64 | 75.844 | 13.18 | 98.316 | 117.736 | 117.736 |
| After firing | 125 | 65.286 | 15.32 | 84.074 | 97.433 | 126.968 |
| Outdoor movement/firing | 707 | 51.673 | 19.35 | 64.458 | 74.354 | 99.479 |

The outdoor sample covers timing frames 9896–10602: four forward/turn/fire/
reload/backward cycles after leaving the pod. The final screenshot shows the
player outside, intact terrain and weapon, a reloaded AR and full health. No
crash occurred during these short sequences. Of 707 outdoor frames, 334 exceeded
50 ms; none exceeded 100 ms. This does not establish sustained 20 FPS, NPC
combat, long-session stability, or a fix for the Warthog/rocket crash.

The lifepod values are broadly similar to perf255/256. No frame-time gain is
established for the single B6210 change. Private receipts are
`ar258-summary.json`, `ar258-events.json`, `move258-summary.json` and
`move258-events.json` in the candidate directory. Host input brackets do not
identify the precise weapon simulation tick.

Next: attribute impact-triggered effects/sound and the object-update branches
that grow while firing before choosing another implementation boundary. The
existing particle-callee profile limits what further 80720 math alone could
save. Keep all qualified earlier changes; do not infer a speedup from the Pi
microbenchmark or remove gameplay work to improve the number.
