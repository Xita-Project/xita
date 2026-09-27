# Scene/model attribution follow-up — September 27

With the profiling-stack exclusion in place, a bounded 120-second Pi campaign
capture completed (planned timeout 124). Object workers were explicitly disabled,
matching their observed admission state on perf272 hardware. Both scene and tick
phase observers were enabled. Private receipts: ../scene-followup-pi/.

Last ten reported windows' median inclusive / remainder milliseconds:

| Scope | Inclusive | Remainder |
|---|---:|---:|
| 5DBC0 scene root | 24.575 | 0.010 |
| 5B760 model-list pass | 11.825 | 0.060 |
| A26B0 model packet | 9.560 | 8.160 |
| 54010 ordered callbacks | 2.200 | 2.200 |

Inclusive rows overlap. Remainder is NOT pure arithmetic or removable execution:
untimed callees and native waits remain charged to parents. The headless harness
also differs from the current Vita runtime, so these are targeting evidence,
not FPS predictions or a controlled hardware comparison.

Inspection of the retained A26B0 body found two A2380 calls without child timers:
UV-scope setup separates each call from its guest return push, so the older
push/call-only patcher missed them. A2380 calls 70110, 6B060, 6EFC0 and B5EA0.
This explains an attribution gap; it does not establish their relative costs.

A private follow-up shard adds existing --any-call observers to A26B0 and A2380
(14 call sites total, no patcher warnings). All other linked objects are retained
from the corrected harness. The ARM build passed and its bounded capture is
running under ../model-material-split-pi/. Inspect its result before deciding on
a native rewrite; do not treat the 8.16 ms remainder as A26B0's own cost.

Related regression checks also passed: phase off/both/tick-only behavior,
stack-overflow recovery, and hash-hint lookup equivalence after table reordering.

## Completed split and instruction-footprint candidate

The split capture completed with expected timeout 124. Last-ten medians:
A26B0 inclusive 9.975 ms, remainder 0.530 ms; A2380 aggregate inclusive 9.850 ms,
remainder 0.590 ms; 6EFC0 inclusive 1.875 ms. Observed A2380→70110 edge around
6.98 ms includes native material children. This agrees with the older hardware
attribution in draw-route-profiling.md and codex-return-20260924.md; it is not a
new discovery of an 8 ms outer-loop optimization. Do not repeat the already
rejected draw-route/wrapper rewrites based on these overlapping times.

A distinct compiler-footprint experiment leaves native 70110's source and
arithmetic unchanged, changing only its compilation from -O2 to -Os. VitaSDK
objects report n70_fast 0x416c → 0x387c bytes (16,748 → 14,460), n70_gen
0x6430 → 0x57b0 (25,648 → 22,448). Smaller code might reduce instruction-cache
pressure but can increase execution cost; no speedup is established.
Private commands/objects: ../material-size-candidate/size-receipt.json.

Host differential fixture passed 2,048 cases, including 139 cross-page windows,
134,607 observed events and zero state/verification mismatches. Existing NaN
normalization/skip policies remain; this is not a new exhaustive FP proof.
ARM fixture built successfully with Cortex-A9 Thumb flags; Pi execution is
running. No production build flag or installed Vita executable changed.

The ARM/Pi differential fixture also passed all 2,048 cases with zero mismatches
under existing verifier normalization rules. Isolated game-like timing (64
inputs × 1,000 repetitions, stand-in callees, Pi core 0) completed baseline,
size, size, baseline runs: native 3,964 / 3,965 / 4,027 / 3,987 ns per call.
Cycles were 7,603 / 7,638 / 7,759 / 7,626. No Pi speed improvement is established;
smaller instruction footprint is the remaining hardware-specific hypothesis.
Receipts and counters are in ../material-size-candidate/bench-*.log.

`XV_NATIVE_70110_SIZE=1` now opts only this native material object into -Os.
Default 0 retains existing compiler behavior. A tracked config dependency handles
both size and phase changes without stale objects. The production Makefile
fixture passes unchanged-repeat, 0→1→0, phase-switch, unrelated-object and invalid
option checks. This is a reversible hardware experiment, not a default speedup.

## Perf274 hardware candidate

Full Vita build succeeded: perf274 / 910817dc. Comparing retained compiled
objects against perf273 shows only recomp/kernel/xk_native_70110.o changed.
Its actual n70_fast/n70_gen symbols match the smaller-code probe. Generated
game objects remain byte-identical. Package membership/assets are unchanged;
only game-a.self and boot-game.txt differ, with the same update contract.

Runtime: 34,799,886 bytes, SHA-256
`ce4dc06994b6afff2636f7b09b10700089bc98557d2c66953c53bc04602773cd`.
Package SHA-256
`42e53607146a3d50c85ed5368b3770d5ec4f36f609c388f359a4613b0d0708d3`.
Receipts: ../material-size-hardware/. Update/apply is running; confirmation
of installation must come from the updater's verified boot receipt.
The planned launch preserves all 32 gameplay overrides, 360p and a30-perf211.
No change to worker tolerance, rendering, save policy or simulation settings.

### Perf274 verified installation

The updater completed successfully: verified=true, boot_confirmed=true, slot 1,
with the runtime SHA-256 above. Live status independently reported
0.2.0-perf.274 / 910817dc, timing_frame=0 at the dashboard. The a30 launch
sequence has started with the protected test-save namespace and the same
32 overrides. The keep-awake lease was renewed. Perf273 remains the rollback
in slot 0. Installation is verified; gameplay performance is not yet measured.
