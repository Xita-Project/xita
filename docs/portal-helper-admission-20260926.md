# Typed portal clipping and the copied scene context

The current perf266 profile attributes about 2.9 ms inclusive to 53540. Its
recursive child 532E0 already contains the native integer-loop lowering and
calls xv_portal_polygon at 534D5 before falling back to B7F10. Existing B71C0
native clipping remains separate; this investigation does not claim all clipping
is translated or all existing optimizations are disabled.

The typed portal adapter first calls xv_object_census_boundary. That admission
requires c == &xk_cur->ctx and the current guest fiber, in addition to drained
worker/service state. Scene execution instead calls BCB30 with its static copied
ctx and a private overlap stack. xv_owner_thread_id/pthread aliasing only handles
native identity; it cannot make those distinct context addresses equal.
Consequently the existing guest-owner typed adapter does not admit the normal
copied scene context. Keep this distinct from counting actual portal call rates:
no per-site hardware admission census has yet been captured.

The clip controller startup is also guest-owner-bound through
XV_CLIP_TRIAL_PRESENT. The captured perf266 log contains no clip-region-trial or
portal-native reports. Absence alone does not prove a specific decline reason,
but the source establishes two ownership dependencies to handle deliberately.

A production admission regression now checks copied-context rejection and
unchanged memory/context even with valid owner identity. Host ASan/UBSan passed
33 backend admission checks, normal native-owner acceptance and 25 decline
checks. The startup fixture needed its missing current owner pthread hook;
this uses pthread_self because that fixture has no scene alias. Private receipts
are ../portal-helper-audit/host-fixed/. The first attempt failed to link that
missing platform hook; it was not a runtime failure.

## Implementation boundary

Add a separate opt-in scene-helper admission, not a global census relaxation.
Require actual helper identity, its own live context/generation, private stack
bounds and active isolated render memory view. Preserve all mapping/alias,
input, FP, preemption and output checks. Keep math workspace local. Avoid sharing
mutable owner controller/statistics without explicit synchronization. Verify
complete fallback state and the enclosing recursive traversal on ARM, then
capture hardware admission counts and ordinary gameplay. Do not claim the full
2.9 ms can be removed: traversal/projection work and fallback remain.

## Hardware status

The normal-settings restore coordinator (session 7406) ended after its 180-second
readiness deadline with connection refused. The preceding companion quit/launch
was acknowledged, but dashboard/readiness never returned, so no normal-settings
launch or new awake lease is confirmed. Perf266 remains the last installed build;
no additional restart or upload was sent. A screen-status question is pending.
Offline implementation can continue; the performance goal is not blocked by
this hardware contact issue alone and remains unmet.
