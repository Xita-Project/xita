# Depth-store fresh-launch retest

This retests the existing read-only backbuffer continuation policy alongside
collision-vertex conversion, object collection, segment/sphere math and earlier
exact query publication. Earlier short comparisons used reduced graphics
settings and had mixed results; they do not settle its value in the current
combined build. Use ordinary gameplay after restarting Xita, with the user's
graphics settings preserved.

`XV_DEPTH_STORE=1 XV_DEPTH_STORE_DEFAULT=1` initializes the existing mode before
any rendering worker starts. The repository startup default remains zero and
accepts only 0 or 1. The startup log records configured mode and availability.
Shader overrides can still make the policy unavailable, and each scene must
still pass the original read-only proof. No new scene, fence, wait or resource
retirement rule is introduced.

Only D3D owns the startup value. Changing that value while compiled ON rebuilds
the D3D object; changing it while compiled OFF has no effect. Compile-feature
transitions retain the existing main/D3D/shader/UI rebuild dependencies. The
existing comparison controller still restores the mode that was active before
it started, including a startup value of one; no live comparison is required
for the gameplay retest.

This policy suppresses a **forced** depth/stencil store only after an earlier
successful backbuffer store and proof that the continuation is read-only.
Loads, attachments, first/offscreen stores, shader checks and UI-tail checks
remain unchanged. Descriptor admission proves that the policy is being used;
it does not prove that the driver skips a physical memory transfer or that FPS
improves. Retain inconclusive results separately from correctness failures.

## Local validation

`tools/test_depth_store.py` now exercises the first actual replay with each
startup default before any mode setter runs, unavailable shader configurations,
and a query notification on an admitted read-only intermediate continuation.
The full 490-case replay oracle passed in eight queued/synchronous,
query-enabled/disabled and sanitizer configurations; the 1,164 referenced
embedded fragment programs matched the owned package's GXP files.

`tools/test_depth_store_startup.py --output-dir <new-private-directory>` passed
six actual startup/controller builds under Address/UndefinedBehaviorSanitizer,
including restoration from an initially enabled mode, cancellation, lost control
and unavailable shader inputs. Ten real-Makefile transitions verify default and
feature dependencies without rebuilding guest code. Invalid values are rejected.

The full Vita package built. Its ELF contains both depth-store and query-boundary
startup values of one, with the three CPU helpers retained. Scene-census code is
compiled OFF. Package members differ from the four-feature gameplay build only
in the runtime executable and boot record. Physical gameplay and measured
benefit remain to be established; these local checks do not establish general
hardware correctness or a speedup.
