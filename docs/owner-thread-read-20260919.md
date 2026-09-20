# Reuse one fresh native-thread identity during admission

The Vita `xv_owner_phase_active` path checked worker identity, then presenting
thread identity. With initialized object workers, each check called
`sceKernelGetThreadId`. Material UV/fog and other native helpers use this
admission path repeatedly.

The object backend now exposes an ID-classification query. The owner predicate
reads the current thread ID once and uses that same fresh value for both tests.
It does not cache identities across calls or weaken context, fiber, generation,
active-phase or guest-job-marker checks. Older backends without the new weak
query retain the original path. The existing worker-thread API retains its
uninitialized fast return and one identity read when initialized.

Validation:

- Production worker-query extraction passes lifecycle, changed-ID and identity
  read-count cases under ASan/UBSan (`tools/test_worker_identity.py`).
- The production owner/UV ARM fixtures pass 245 context, memory, FP, caller and
  lifetime comparisons. Direct owner, foreign-thread and worker-context calls
  verify exactly one kernel identity read with the expected accept/reject result.
- The existing host owner tests passed their fresh-process cases and TSan
  concurrency run. Their subsequent synthetic Make transition stage failed
  because its scaffold lacks `version.json`; the full legacy driver is not
  claimed passing. No Make configuration change is part of this implementation.
- Both modified runtime translation units compile with VitaSDK.

Private evidence is under `../owner-thread-read-arm/` and
`../owner-thread-read-tests/`. This removes a redundant kernel query, not a
simulation join or data-ownership check. No hardware timing gain is established;
perf.39 remains installed until a later cumulative update is confirmed.
