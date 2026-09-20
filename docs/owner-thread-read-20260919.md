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
the subsequent hardware receipt is recorded below.

## Cumulative build

Perf.40 (`28c90b6`) builds successfully. The first attempt correctly rejected
an added declaration in the collision generator's pinned `xk_object_jobs.h`.
That unnecessary shared-header declaration was removed; the owner observer
already declares its optional backend query locally. The header again matches
the reviewed input byte for byte, and the existing generation checks pass.
No pin was relaxed or replaced to accept this change.

The linked ELF includes strong definitions of `xv_object_is_worker_id` and
`xv_owner_phase_active`. Object disassembly confirms the single-read path and
older-backend fallback. Only the gameplay executable and boot record differ
from perf.39. Runtime SHA-256:
`4d35c3bd97dcb2999acec62831ed20959a4a6ef9928fa809ffb2255d47baa1cb`.
Private receipts are in `../owner-thread-hardware/`. Hardware deployment and gameplay evidence follow below; the build alone does
not establish an FPS result.


## Hardware receipt and next target

Perf.40 / `28c90b6` was installed through the updater, fully restarted, and
boot-confirmed in slot 0. Campaign gameplay is visible in the private
`owner-thread-hardware/followup.png`; the remote status reports the expected
version. No automated off/on/off comparison was run for this validation.

The first launch checker accepted a loaded-state log entry too early: its
`gameplay.png` still shows loading, and the pulled full log ends with loaded=0.
That receipt must not be used as gameplay proof. The later follow-up capture
shows the pistol, marine, world and HUD, with loaded=1/active=1 in its log.
Future launch checks must corroborate current state visually instead of treating
one log match as sufficient.

The first post-load 60-frame window included texture decoding and averaged
6.2 FPS. Later windows in `steady-tail.log` report 12.6 and 12.7 FPS with no
texture decoding. These are observations of one ongoing run, not a measured
improvement over another build. Heavy-gameplay 20 FPS remains unproven.

The later complete phase report contains:

- Game update FA920: 2,236,902 us / 60 entries = 37.28 ms per entry.
- Scene BCB30: 2,363,781 us / 60 entries = 39.40 ms per entry.
- Object batch time: 1,677,168 us / 60 frames = 27.95 ms/frame, nested in
  game update, not additional to it.
- Worker lock waits: 682,897 / 768,193 us per 60 frames, or 11.38 / 12.80
  ms/frame. The lanes overlap; do not add them as recoverable frame time.
- Zero busy display-slot waits. This does not imply zero GPU cost.

The initial address decoding in this investigation was wrong: it omitted the
Vita module load slide. The reported clipping attribution is withdrawn. See
[the corrected hardware follow-up](private-clipping-20260919.md#corrected-wait-attribution).
Use the runtime `xv_object_math_lock` anchor and the matching ELF before resolving
recorded return PCs; feeding absolute runtime PCs directly to addr2line can
produce plausible but unrelated function names.

The timing-hook audit also found that `XV_POSE_PIPELINE` explicitly requires
bucket-0 model hooks. Turning off scene instrumentation wholesale would disable
or invalidate an existing optimization. Keep ownership/lifetime hooks intact
when separating optional timing overhead later.
