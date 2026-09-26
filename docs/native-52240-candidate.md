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

## Strict floating-point check and ownership audit

`N92_STRICT_NAN=1` now disables both NaN scratch-slot equivalence and the
aligned memory-word NaN exemption in the differential fixture. Defaults for
existing light-query testing remain unchanged.

Strict host O2/render-view testing rejects case 859 of the 1,000-case seed-1
corpus: x87 scratch slot 7 differs (`7FF92F2000000000` native versus
`7FF9988BA0000000` guest). Both are NaNs, but their payloads differ. The
candidate is therefore not host bit-exact. Do not hide this with a tolerant
pass summary. Log: `../flood-direct-strict-host.log`.

Strict Pi Cortex-A9 Thumb O2/render-view testing passes all 1,000 seed-1 cases:
zero mismatches, no skipped timeouts, 16,325 flood entries, 55,237 portal tests,
52 stack-alias scenes. Native run session 69770 is terminal. Private compiler
receipt is `../flood-direct/strict-arm/test-0.build.json`. This is bounded
ARM evidence, not a proof for every input or a performance measurement.

The actual perf260 stage's 8D320 and 52240 bodies have no local object-math
lock. Both route query serials/stamps via `X_QS8/X_QS32`. `xk_qserial.h`
redirects those accesses to `xv_qserial_private` on the scene helper; the
native flood currently reads the mapped live image globals. Enabling it on
that helper would bypass an existing correctness mechanism.

A future hook must positively identify the actual guest owner thread and
exclude scene helpers and worker/job contexts unless their routing and
transaction are separately modeled. `xv_object_is_worker_thread()==0` alone
is not owner proof (the header explicitly says so). Do not add an unconditional
math mutex: the caller may already hold a transaction, and owner service
parking makes naive nested locking unsafe. Preserve the caller's existing
synchronization; verify-mode journaling must also decline while a render-view
merge could mutate the observed globals. No such runtime hook is enabled yet.
