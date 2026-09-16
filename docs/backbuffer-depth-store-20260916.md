# Read-only backbuffer depth-store candidate

`XV_DEPTH_STORE=1` compiles a private experiment; its runtime mode starts Off.
The `depth-store` remote comparison (selector 40) runs Off/On/Off at the current
resolution and restores the initial mode after completion, cancellation or loss
of player control. This is not a dashboard setting or an established FPS gain.

The renderer currently forces depth/stencil stores when returning from the
backbuffer to an offscreen target. A continuation that only reads depth can
reuse the memory stored by an earlier scene. For a proved read-only continuation,
the candidate disables **forced** store; it retains the same attachment and
forced load. The driver may still store it. Nominal surface sample counts are
not measured memory traffic.

The pump inspects the retained mesh and UI event order before each backbuffer
scene begins. It requires a successfully ended prior backbuffer scene and a
known subsequent target transition. Any clear, depth write, enabled stencil,
UI event within the scene, invalid command/target, unknown shader or development
shader override keeps the original store. The first and final backbuffer scenes
and every offscreen descriptor also retain their original stores.

Shader admission checks trusted embedded GXP metadata for the selected program,
alpha specializations, heuristic fallback and constant depth-only program.
It does not load files or create/link shaders during the scan. Unknown metadata
declines the optimization. Mode changes occur only after the existing pump/GPU
drain; the candidate adds no synchronization call or rendering boundary.

## Validation

`python3 tools/test_depth_store.py --stage <owned-build-directory>` executes the
production render-target replay with an independent memory-store/load model.
It compares depth, stencil, color, query results, notifications, ordered events,
failure drains and the caller's unchanged descriptor with the mode Off and On.
Repeated read-only returns, an intervening writable return, UI target switches,
clears, shader depth exports, missing specializations, unsupported lists, frame
slot reuse, native/scaled sizes and first/final scenes are included. There are
350 comparisons per run, repeated with queued/synchronous target submission,
query-boundary support disabled/enabled, and Address/UndefinedBehaviorSanitizer.
All passed. Controller tests cover unavailable builds, admission, cancellation,
lost control and exact initial-mode restoration.

The owned-stage metadata check passed for 1,164 referenced embedded fragment
programs, comparing embedded bytes with the actual GXP files and rejecting depth
exports/buffer stores. The authenticated remote-interface tests passed under
Address/UndefinedBehaviorSanitizer. These host checks do not establish GXM driver
behavior, physical correctness or a performance improvement.

Physical acceptance still requires eligible-scene counters, matched-camera
native-resolution comparisons, world/HUD/effect images and combat/driving checks.
Leave the candidate Off unless that evidence supports enabling it. Private
validation receipts are in `validation/engine-restructure-20260914T2300Z/depth-store-validation/`
under the worker-sizing backup directory.
