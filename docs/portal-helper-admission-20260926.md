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

## Candidate implementation and qualification

The un-deployed candidate adds process-start opt-in `XV_SCENE_PORTAL=1`.
The scene route requires real helper identity, its copied context, a valid
non-wrapping generation, overlap stack bounds and a bound thread render view.
It bypasses only the guest-owner controller; diagnostics, exact caller layout,
budget, finite inputs, mapping/alias checks and numeric fallback remain.
Explicit `XV_NATIVE_CLIP=0` or `XV_CLIP_REGISTERS=0` disables this route too.
Scene acceptance/decline counters are atomic and reported separately.

Review caught two global-versus-thread-table mistakes before deployment:
the new view admission compared `g_xpt` instead of `X_PT`, and the existing
adapter validated spans through `g_xpt` while guest reads used `X_PT`.
Both now use the actual thread table. Tests provide distinct tables: a physical
alias only in the helper table must decline; an alias only in the live table
must not reject valid helper data. No broad memory-view or scheduling rewrite
is included.

Host ASan/UBSan and static ARM Linux binaries on Raspberry Pi core 0 pass:

- 33 existing backend admission checks and 25 native-owner decline checks.
- Scene route acceptance without initializing the guest-owner controller,
  complete-state declines, default-off behavior, and 64 owner/helper output
  equivalence cases, including FP state reset between each pair.
- Actual production render-view admission with distinct live/helper tables.
- Actual production scene helper identity, generation and private stack bounds,
  including rejection from another pthread.

The adapter fixture mocks scene identity/binding services; the last two fixtures
exercise the production guards independently. This is not an end-to-end Vita
scheduler proof or a replacement for the enclosing generated traversal test.
Production Vita compilation of the three changed C units passes using the
perf266 build flags. No new VPK has been packaged or deployed yet.

Private receipts: `../portal-helper-candidate/host-tables/`, `guards/`, `pi/`,
and `object-build-result.json`. `tools/test_portal_owner.py` now also includes
the production guard fixtures for future runs. The next gate is ARM enclosing
recursive traversal equivalence, then hardware admission counts and ordinary
gameplay. There is no measured FPS gain for this candidate.

## Hardware status

The user reported LiveArea or sleep after the first normal-settings restore
failed its readiness deadline. One subsequent companion launch succeeded;
normal a30 launch inputs completed and the remote endpoint returned on perf266.
The keep-awake lease was renewed. Perf266 remains installed; the candidate above
has not changed the device. The sustained-20-FPS goal remains unmet.

## Perf267 traversal and package gate

The current retained perf266 ARM objects were compared to the perf267 adapter
with a copied-context fixture. All 33 cases pass (28 traversal cases plus five
53540 outer-consumer FP modes). Outside dead recursive stack scratch, guest
memory, callee-saved registers, logical x87 depth, return stack and remaining
budget match. Declines also preserve complete context and FP status.
Recursion depths 4/16/32 and invalid generation, disabled mode, low budget,
noncontiguous pages, crossing and empty polygons are covered.

The harness needed current owner-ID, private-clip-release and phase-timer
platform services. Earlier attempts stopped on those missing fixture imports;
they did not produce a game-state mismatch. Actual scheduler/worker identity
remains covered separately by the host/Pi guard tests. No Vita timing is inferred
from the modeled instruction count reduction. Receipts are
`arm-scene3/result.json`, `arm-scene-outer/result.json` and
`traversal-receipt.json` under the private candidate directory.

Perf267 (`c1a2d36a`) builds and packages successfully. Against perf266, only
`game-a.self` and `boot-game.txt` differ; the update contract stays unchanged.
Runtime SHA-256: `5f89ebc86d5e45d90c8a779bdeb73a1aca6df0b02d27910e8ca02520d07f2584`.
The launch will retain the a30-perf211 save namespace and existing settings,
replacing the disabled scene-wait observer key with `XV_SCENE_PORTAL=1` to stay
within the 32-key environment limit. Hardware installation/results are not yet
established by this package receipt.

## Hardware deployment

The updater verified perf267 and confirmed its boot in slot 0; remote status
reports `V0.2.0-perf.267 / c1a2d36a`. Perf266 remains the other slot. The normal
a30 launch sequence is running with the opt-in key and a renewed awake lease.
Deployment receipts are `../portal-helper-candidate/deploy.log` and `launch.log`.
Installation is not evidence of a frame-rate gain; admission and settled
ordinary-gameplay measurements remain pending.
