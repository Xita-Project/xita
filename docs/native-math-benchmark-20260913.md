# Native-math off/on/off comparison

Hardware now confirms that the [object-basis helper executes](hardware-20260913-object-basis.md),
but different camera positions prevent measuring its benefit against the prior
run. The existing benchmark can now select either experimental math helper.
This adds measurement controls, not another optimization or worker.

## Selection and behavior

Build the selected helper with `XV_NATIVE_OBJECT_BASIS=1` or
`XV_NATIVE_MODEL_PALETTE=1`. Both are included in the private test candidate.
The Makefile passes those feature definitions to the runtime as well as the
guest libraries so unavailable helpers can be rejected explicitly.

Select one comparison in `ux0:data/xita/xita.cfg`:

```ini
XV_BENCHMARK_OBJECT_BASIS=1
XV_BENCHMARK_MODEL_PALETTE=0
```

For the palette comparison, reverse those two values. Object-basis selection
takes precedence if both are enabled; both take precedence over older vertex
worker/reference and other comparison selections. A selected helper that was
not compiled in, or a master `XV_NATIVE_MATH=0`, rejects the test without
silently choosing another candidate or changing settings.

In a loaded first-person view, press **L + R + Square**, release the buttons and
wait for the three phases. Each has 60 settling frames followed by 120 measured
frames, for 540 frames total. At 10–20 FPS this takes roughly 27–54 seconds.
Ordinary input is neutral during the test. Camera consistency is checked; live
simulation and animation continue, so repeat trials still matter. Either existing
benchmark chord cancels the test.

The guest owner drains submitted work before switching the selected override.
Phases force off, on, off, then restore the configured runtime default on
completion, cancellation or loss of view. The other math helper, vertex worker,
resolution, graphics options and queue policy remain unchanged. The override
never bypasses the master math-disable, memory-layout, floating-point or
scheduler-budget guards. No object pointer or scratch allocation crosses a phase.

Results use `[object-basis-compare]` or `[model-palette-compare]`, including exact
measured frame counts and elapsed microseconds. Pool the off intervals by elapsed
time rather than averaging rounded FPS. Existing helper counters confirm that
the on interval actually accepts work; their 60-frame boundaries can straddle
phase transitions and are not exact per-phase counts.

Keep instrumentation identical in every arm. The next hardware comparison uses
`XV_PHASE_TIMING=0` to avoid the detailed phase-timer cost; helper and benchmark
reports remain available. This does not make its overall FPS directly comparable
with the previous instrumented gameplay capture.

## Validation

The benchmark host tests cover precedence, unsupported builds, master-disable,
fixed resolution, settling exclusion, camera movement, completion, cancellation,
view loss and restoration. Production frame-acquisition tests verify that each
override routes only to its selected helper, preserving the queue and unrelated
switches. Both helper differential suites pass optimized and under ASan/UBSan:
four configuration modes, 4,096 existing original-code comparisons per mode,
plus 96 override-transition comparisons and configured-default restoration.
Existing layout, floating-point and scheduler-handoff guards remain covered.

The native package passes archive integrity and embedded EBOOT checks. Its 1,584
non-executable entries match the preceding installed package byte-for-byte;
generated guest translation units are unchanged. Private validation and package
hashes are under `2026-09-13-worker-sizing/validation/native-math-benchmark`.
Hardware FPS benefit and broader gameplay parallelism remain unproven.

An isolated Vita3K run reaches Blood Gulch through the normal solo menu, completes
the object-basis off/on/off sequence with all camera checks passing, and records
accepted native work again after restoring the configured enabled default.
The old vertex-worker comparison flags remain present but do not select the
wrong test. Walking, turning and firing respond afterward. This is a capped
emulator functionality check, not a Vita performance result. The owned emulator
was stopped and its previous executable/configuration restored.

Candidate EBOOT SHA-256:
`168c0f9ad63fbbc92046d7f575d77d163348bb0f15c43d1e687be82ebe5fef2d`.
VPK SHA-256:
`07cc696d67c739ffbf5243faebc085454597c17482a1271311edfc944d368e75`.

The package was copied to
`ux0:VPK/xita-math-benchmark-20260913-168c0f9.vpk`, with object-basis comparison
selected and detailed phase timing off. Package and configuration hashes were
verified again after a read-only remount, then USB was unmounted. **The VPK still
requires installation in VitaShell.** The device's installed executable was not
overwritten by the transfer. Private before-config and transfer receipts are
preserved with the validation artifacts.
