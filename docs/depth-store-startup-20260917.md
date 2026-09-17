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
in the runtime executable and boot record. These local checks do not establish
general hardware correctness or a speedup.

## Fresh-launch hardware result

The cumulative candidate from `afe7e75` is installed in updater slot 0, with
confirmed runtime SHA-256
`3381599eb9c0f0605a53b1eb41226f9d74f95d4be0b7ee1848f5c5524f1e33b4`.
The four-change parent remains in slot 1. Both launches entered the same
Pillar of Autumn checkpoint directly from the dashboard, using New001 and
Normal difficulty. No built-in benchmark or live mode switch was requested.
The five startup selections report enabled; scene-census code remains absent.

| Ordinary gameplay observation | Displayed FPS | Frames / elapsed time |
| --- | ---: | ---: |
| Four retained changes, fresh launch | 12.748 | 765 / 60.012 s |
| Same changes plus depth-store policy, fresh launch | 12.660 | 760 / 60.030 s |

These are display-counter observations over host monotonic time. The logged
settings match, and the camera remains at approximately `(-28.66, 32.52, 0.62)`
facing `(0.56, 0.82, -0.15)`. Screenshots show the same column and pistol view.
Live AI and draw counts vary, so the roughly 0.09 FPS difference does not
establish a regression or a benefit. Endpoint latency bounds in the private
receipts describe timing uncertainty, not workload variation.

Crucially, the new policy reports **zero admissions** here: steady windows retain
60 first backbuffer stores and decline 60 continuations because stencil is
enabled. This is a conservative exclusion, not proof that every such draw writes
stencil. No store was omitted in the sampled view. Classify the new candidate as
**not applicable to this sample**, rather than concluding that omitted stores
do or do not help. Earlier exact query publication remains active: the recent
windows observe all 60 prefixes before final completion with zero fallbacks.

After the observation, short movement, pistol fire and the pause menu remain
responsive, with visible impact effects and intact scene/UI screenshots. No
fault marker appears in the captured observation log. This does not qualify
rocket explosions, driving, longer stability, or scenes that actually admit
the new policy. The current package retains all five startup selections; the
repository default remains OFF pending broader evidence.

Private receipts are under `direct-cluster-query/depth-store-startup/` in the
engine-restructure validation directory: `installation.json`, both
`*-campaign-passive/result.json` files, `*-analysis.json`, logs and screenshots.

## Keeping improvements together

The package retains the existing worker/rendering changes and these recent
additions together: collision-vertex conversion, object collection,
segment/sphere math, earlier exact query publication, and the conditional
depth-store policy. A new candidate does not replace the preceding compatible
changes. Correctness, actual admission and performance are separate questions:

- A correctness failure or reproducible regression requires a fix or rollback.
- An eligible change with an unclear timing result remains inconclusive.
- An enabled change with no eligible work is not tested by that view.
- A repeatable improvement in the combined build supports keeping the change
  enabled; scene-specific gains are not a general FPS guarantee.

Evaluate cumulative frame time after fresh launches. Savings in independent
work can add up, while overlapping work and a different limiting subsystem can
hide them. Do not add isolated FPS differences or promise a 5 FPS combined gain.
