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

## Direct timing fixture and remaining admission constraints

`tools/tests/native_92330.c` now accepts `--bench-flood REPEATS` in the
`XV_NATIVE_52240_TEST` build. The existing `--bench` / `--bench-game` paths
are rejected in that build: they call 56670 with a different ABI and would
not measure the direct candidate. Build with `tools/test_native_92330.py
--flood --output <private-output>` and run:

```sh
N92_STRICT_NAN=1 timeout 120 <private-output>/test-0 300 1 --bench-flood 1
```

The fixture restores the entire 24 MiB physical arena outside each timed
call, including stack, output and visited stamps. Direct 52240 does not
advance the epoch; repeating without restoration would progressively skip
work. Guest/native order alternates. Every pair checks the entire arena,
context and preemption count; use `N92_STRICT_NAN=1` for strict context NaN
comparison. Timer overhead is included. The small synthetic scenes use the
existing game-like fixture, not captured a30 inputs. Full-memory restoration
conditions caches, and the native test entry includes coverage counters.
These measurements are neither production timings nor a Vita FPS prediction.

The host smoke run (100 scenes, five repetitions) passed all comparisons:
500 calls each, 545 native flood entries and 1,850 portal tests. The separate
300-case randomized differential regression also passed, with 4,941 flood
entries, 28,125 portal tests, 14 stack-alias cases and no skipped timeouts.

Further admission audit: `xv_owner_thread_id()` returns the current native
thread identity, aliasing the scene helper to its owner; it is not a getter
for a globally registered owner. Comparing it to the current native thread
would admit unrelated threads. The light-census code instead records an
owner during object-job initialization and checks context, queue and fiber
state. It still requires an explicit scene-helper exclusion for this native.
Do not borrow this API without checking build availability and whether its
initialization thread is the actual caller at the direct query boundary.
The native's existing `__thread` journal also cannot be assumed isolated on
Vita (see native-1721b0's emutls notes). A future concurrent verifier needs
call-local or explicitly owned journal storage. No runtime hook is enabled.

Pi timing smoke (30 scenes, three repetitions, strict comparisons): 90 calls
per implementation, guest 8,617.2 ns/call versus native 6,723.7 ns/call
(1.28x); 96 flood entries, 321 portal tests. The initial 300-scene,
ten-repetition run ended at the external 120-second timeout with no summary;
it is not a pass. Its process was confirmed terminal before starting the
smaller smoke run. Logs: `../flood-direct/bench-arm-run.log` (timeout),
`../flood-direct/bench-arm-smoke.log` (pass). Both used only Pi cores 0/1.
Host 300-scene/one-repetition strict run also passed; private log
`../flood-direct/bench-host-300.log`.

Full 300-scene Pi timing run, one repetition, then completed with strict
comparisons: 300 calls each, guest 9,107.1 ns/call, native 7,275.5 ns/call
(1.25x, about 20% lower call time), 334 flood entries and 1,025 portal tests.
Log: `../flood-direct/bench-arm-300.log`; session 96815 exited zero. All timing
jobs are terminal. This retains the full scene set from the timed-out run.

Decision: retain the candidate, but do not prioritize a risky shared-state
runtime integration on this evidence alone. Corrected effect profiling puts
52240 at only 0.08–0.09 ms per reported Pi frame, while listener setup 26A50
is 0.39–0.40 ms. The small-query fixture does not establish the relative
speedup on real flood sizes; even the measured synthetic ratio cannot
explain a large whole-frame improvement. Next independent investigation
should split 26A50's distance/listener and spatial-query work (25590/2B460),
while perf260 still awaits the separate physical gameplay check.
