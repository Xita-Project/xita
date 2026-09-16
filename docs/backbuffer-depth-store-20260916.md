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
scene begins. It requires a successfully ended prior backbuffer scene. An
intermediate span ends at a known subsequent target transition; a final span
also requires the caller's explicit read-only tail contract. Any clear, depth write, enabled stencil,
UI event within the scene, invalid command/target, unknown shader or development
shader override keeps the original store. The first backbuffer scene and every
offscreen descriptor retain their original stores.

For the final scene, `main.c` proves its remaining work is viewport setup,
the owned settings panel and EndScene. Scaled settings use a separate depthless
scene; unknown dialogs decline. The renderer separately checks its own overlay
frame. Both checks inspect the actual linked clear/settings fragment programs
and reject missing programs, depth exporters, invalid frame slots or an
unready UI. The two UI entry points disable depth writes and stencil. This
contract does not cover ordinary recorded UI batches or arbitrary callbacks.

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
480 comparisons per run, repeated with queued/synchronous target submission,
query-boundary support disabled/enabled, and Address/UndefinedBehaviorSanitizer.
All passed. Controller tests cover unavailable builds, admission, cancellation,
lost control and exact initial-mode restoration. Final-tail cases exercise the
production UI proof, including an independently modeled external depth write
that must keep its store, absent programs, inactive UI, wrong frame slots and
a caller declining the contract.

The owned-stage metadata check passed for 1,164 referenced embedded fragment
programs, comparing embedded bytes with the actual GXP files and rejecting depth
exports/buffer stores. The authenticated remote-interface tests passed under
Address/UndefinedBehaviorSanitizer. These host checks do not establish GXM driver
behavior, physical correctness or a performance improvement.

The native Vita build also passes, with the existing worker and visual-switch
configuration retained. Package comparison finds the same 1,588 members and
only `game-a.self` and `boot-game.txt` changed. The final-tail follow-up hash is
`c999c0120bf47608a2cadcacb6a9872bfece819e3eed3bba691f1c24ab670095`.
Visibility-placement, query-boundary and render-target lifecycle regression
tests pass; no generated guest translation unit was
rebuilt for this candidate.

## First hardware comparison

The initial version, which conservatively retained every final store, was
installed and verified with runtime hash
`daeac492ee59d6d03476eea85a81f973d931a1526de967d33dd828bac13c4f52`.
In a fixed Blood Gulch blue-base view at 640×360, its Off/On/Off result was
**19.522 / 19.474 / 19.707 FPS**. Each arm settled for 60 frames and measured
120; all camera checks passed, and the original Off mode was restored.
The view was position `(51.8824, -76.4291, 0.7041)`, direction
`(-0.96857, -0.24882, 0)`, with 108 HLE draws/frame. Saved textures were Low;
the map log reports temporary decals On and cosmetics/reflections/shadows Off.
This is not an Original-graphics or native-resolution baseline.

There were **zero accepted intermediate stores**: each complete 60-frame On
report recorded 60 first and 60 final backbuffer scenes. The final classification
occurs only after scanning the recorded span, so those final spans had already
passed the mesh depth/stencil/shader checks. The unproved external UI tail was
the remaining exclusion. This result supplies the reason for the bounded final
tail extension above; it establishes neither bandwidth savings nor an FPS gain.

## Final-tail hardware comparison

The follow-up runtime `c999c0120bf47608a2cadcacb6a9872bfece819e3eed3bba691f1c24ab670095`
was installed and its running hash verified. It now admits one final backbuffer
scene per frame: complete 60-frame On reports show 60 first scenes retained and
60 final continuations accepted, with no intermediate continuations. This
confirms that the experiment changes the descriptor; it does not prove that the
driver avoids a physical store.

| Fixed view | Resolution | Off before | On | Off after |
| --- | --- | ---: | ---: | ---: |
| Red-base entrance | 640×360 | 20.481 | 20.700 | 20.412 |
| Same entrance, trial 1 | 960×544 | 15.416 | 15.930 | 15.862 |
| Same entrance, trial 2 | 960×544 | 15.718 | 15.900 | 15.409 |
| Looking along the valley from red base | 960×544 | 13.193 | 13.126 | 13.209 |

Values are FPS, with 60 settling and 120 measured frames per arm. All camera
checks passed. Relative to the mean of the two baseline frame times, the entrance
trials save 0.600 ms at 360p and 1.180/1.367 ms at native resolution. The valley
trial instead costs 0.430 ms. Baseline variation limits the small entrance result;
there is no demonstrated valley improvement or general performance gain.

The entrance camera is `(85.2646, -157.7749, 0.6005)`, direction
`(0.93712, -0.34907, 0)`, with 97 HLE draws and eight BSP draws/frame. The valley
camera is `(85.2245, -157.7518, 0.6005)`, direction
`(-0.77306, 0.63437, 0)`, with 128 HLE draws and 13 BSP draws/frame. Texture detail
remains Low, material quality Original, temporary decals On, and
cosmetics/reflections/object shadows Off. These are native-resolution trials,
not a fully Original-graphics baseline or the user's exact earlier valley view.

No capture or bulk log reads occur inside the timed arms; network status polling
is the same in every arm. The remote interface prohibits benchmark screenshots,
so the saved before/after images are Off-state checks, not visual validation of
the On state. No new GPU-fault or worker-stop report was found in the comparison
intervals; this does not establish combat/driving stability.

The candidate remains **Off**. All comparisons restored its original mode, and
the temporary native-resolution setting was returned to the user's 360p setting.
An immediate fixed-camera native/360p/native check in the same valley view,
with the depth-store experiment Off, measured **13.108 / 13.224 / 13.068 FPS**
(76.287 / 75.619 / 76.520 ms/frame). Complete nearby reports show completion
latency falling from roughly 62–65 ms to 38–39 ms and returning to about 63 ms,
while draw HLE stays near 8.5–8.6 ms/frame. These overlapping intervals are not
an additive CPU/GPU budget. The pixel-work reduction does not materially change
the frame rate here, unlike the older resolution-sensitive view. CPU preparation
and other resolution-independent work therefore take priority for this view;
compatible fragment simplification remains a diagnostic for the rendering path.
The resolution controller restored 360p after the comparison. Broader visual and
combat/driving acceptance remains required before enabling the depth change.
Private validation receipts are in `validation/engine-restructure-20260914T2300Z/depth-store-validation/`
and `depth-store-tail-validation/` under the worker-sizing backup directory.
