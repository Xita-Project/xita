# Direct portal-flood candidate

Status (2026-09-26): isolated differential-test candidate only. No runtime hook,
setting, VPK change, hardware deployment or FPS gain. Perf260 is unchanged.

The corrected Pi effect profile places 0.08–0.09 ms of the two firing windows
under the translated 52240 call from 8D320. Native 92330 already contains a
portal flood implementation inside its replacement for 56670, but does not
intercept this separate caller. This is a reuse opportunity, not a reason to
rewrite the whole object-query wrapper.

`XV_NATIVE_52240_TEST` adds a test-only direct entry around `n9_flood`. It
reconstructs general registers, stack-pointer return, exit lazy flags and x87
scratch/status, and accounts for guest back-edge preemption. The surrounding
light query previously overwrote those flags; its existing validation did not
prove this new boundary. All added fields and code are compiled out of normal
builds. The existing 56670 runtime behavior is unchanged.

The differential fixture reuses randomized BSP graphs and shuffled memory
pages, selects a valid start cluster, supplies the direct flood ABI, and varies
list capacity among 0, 1, 2, 64 and -1. Its allocated list holds 64 entries.
It compares guest arena, context (using the existing inactive-flag and NaN
rules), and preemption-call counts. Test counters require recursive floods and
portal tests so a passing wrapper-only path cannot satisfy coverage.

Validation:

- Host: 1,000 cases each for plain O2, thread-table/render-view O2 and plain O0.
  Each exercised 16,325 flood entries and 55,237 portal tests, with 52
  stack-alias scenes and no skipped timeouts or reported mismatches.
- Host O2 variants each allowed one differing NaN-payload memory word under the
  preexisting test policy. O0 allowed none. This is not bit-for-bit equivalence.
- Pi Cortex-A9 Thumb O2, thread-table/render-view: 1,000 cases, identical coverage
  counts, zero mismatches, zero skipped timeouts and zero NaN-payload memory
  words reported. The context comparator still treats two NaNs as equivalent.
- Existing light-query regression: 300 cases, zero differential mismatches;
  296 internal verify comparisons, zero mismatches, four journal-fail fallbacks.

Private receipts/artifacts: `../flood-direct/host`, `../flood-direct/arm`,
`../flood-direct-host.log`, `../flood-direct-arm-build.log`,
`../flood-direct-legacy.log`. Pi run session 64052 is terminal. No job remains.

Reproduce host tests:

```sh
python3 tools/test_native_92330.py <stage>/recomp 1000 --flood --output <private-output>
```

`--build-only --output` and `--cc`/`--extra` permit a preserved ARM build for
execution on Pi. Generated guest code must remain outside the repository.
The older light-query summary prints zero root flood counters for direct tests;
use the separate `direct flood coverage` counters for this candidate.

Before runtime integration: audit the 8D320 call's object-math lock and query
serial routing; preserve helper-thread/private query-state behavior. Resolve
or explicitly constrain NaN equivalence, cover the actual caller's invariants,
and compare in ordinary gameplay under safe verification synchronization.
Do not enable direct replacement just because the encompassing light-query
native is enabled. No hardware gain is established by these tests.
