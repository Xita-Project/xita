# Rendering bottleneck experiments — September 16

The tester/Claude suggestions are now part of the performance work list. They
are hypotheses to test on physical hardware, not measured causes or promised
FPS gains. Keep native resolution and original visual settings as the baseline;
record the exact executable hash, scene, camera, clocks and worker modes.

## Evidence and first decision

One fixed Blood Gulch view measured **11.55 / 16.96 / 11.55 FPS** at native /
360p / native. Final notification latency fell from about 80 ms to 48 ms.
This establishes substantial resolution-sensitive rendering cost in that view.
It does not isolate fragment ALU from texture or framebuffer bandwidth. See
[the capture](hardware-query-placement-20260916.md). These are overlapping
CPU-visible intervals, not GPU active-time counters.

A different view with roughly 307 draws/frame measured about 7 FPS. An exact
earlier visibility-query completion signal worked there, but the original query
wait was already zero and FPS did not improve. Different views have different
limits; do not label the entire game exclusively CPU-bound or GPU-bound.

The [guarded depth-store experiment](backbuffer-depth-store-20260916.md#final-tail-hardware-comparison)
now admits one final backbuffer continuation per frame. Two native red-base
comparisons show small 1.2–1.4 ms paired savings; a different native view along
the valley is flat at 13.19/13.13/13.21 FPS. It remains Off. This narrows the
evidence for that particular store change; it does not rule out other attachment
traffic, fragment work or CPU preparation. Compatible fragment simplification
is the next rendering diagnostic.

In that newer valley view, the immediate native/360p/native comparison is
**13.108 / 13.224 / 13.068 FPS**, with identical camera checks passing and the
depth-store candidate Off. Completion latency falls from about 62–65 ms to
38–39 ms at 360p, then returns to about 63 ms, while frame time remains around
76 ms. This points toward CPU-side or other resolution-independent work limiting
this particular view; it does not negate the older resolution-sensitive result.
Draw HLE is about 8.5–8.6 ms/frame. Object-batch elapsed time is about 28 ms/frame,
including shared-lock waits, and overlaps other reports; it is not an exclusive
CPU budget or a guaranteed saving. The subsequent [bounded phase capture](world-preparation-20260916.md#later-red-base-valley-capture)
again identifies object pose, spatial queries and ordered world preparation;
its substantial instrumentation cost requires treating it as a serial reference.
Prioritize native preparation here, while retaining fragment diagnostics for
pixel-heavy views. The physical receipt is `depth-store-tail-validation/valley-resolution-compare/`
under the existing private engine-restructure validation directory.

| Order | Cheap diagnostic | What a positive result supports | Limits / risk |
|---|---|---|---|
| 1 | Repeat fixed-camera native/480p/360p/native, with identical draws and clocks | Lower pixel workload is on the critical path | Pixel count falls faster than FPS rises when CPU, vertex or fixed costs remain. No change alone does not prove simulation is the bottleneck. |
| 2 | New independent temporary-decal, cosmetic-emitter, reflection and object-shadow controls | Specific visual work costs time in a scene actually exercising it | Reload required; compare identical camera/route, count changed draws/emitters. Empty scenes cannot measure particle cost. |
| 3 | Keep geometry, depth, alpha coverage and blending; replace selected expensive opaque fragment programs with compatible constant-color programs | Fragment texture/ALU cost contributes | Existing vertex-output linkage must match. A literal one-instruction shader is not guaranteed. Replacing alpha/depth behavior would invalidate attribution. Diagnostic only. |
| 4 | Preserve CPU simulation/translation but suppress selected GXM submissions | Submission/rendering and backpressure contribute | A no-draw run also changes visibility queries and feedback. Provide explicit bounded diagnostic query results, retain ownership/retirement, and never report this as a playable optimization or pure GPU-time subtraction. |

Tiny scissor can be an additional pixel-work experiment, but it does not remove
geometry setup/binning, and scissor placement may leave other passes unchanged.
Use frame time in milliseconds and repeated baseline/candidate/baseline trials.
Do not assume a twofold reduction in pixels must produce twice the FPS.

## GPU and tile-renderer checks

1. **Blended overdraw and expensive fragments:** rank shader/target pairs by
   submitted draws, indices and pass coverage. Isolate cosmetic emitters, then
   identified fog/sky/water passes individually. A large effect confined to
   pixel-heavy views supports fragment/overdraw cost. Fog may be integrated into
   material shaders; do not disable every blended draw or shared shader hash.
2. **Target transitions and attachment traffic:** log existing scene begin/end,
   target size, color/depth load/store policy, query/readback requests and CPU
   submission duration. Count full-size backbuffer continuation passes separately
   from 64×64 and 128×128 offscreen targets. Test omitting depth stores only after
   proving a continuation is read-only; preserve loads and later consumers.
3. **Shader ALU versus texture traffic:** compare a compatible no-texture shader
   and a simpler arithmetic shader independently. Audit repeated texture samples,
   precision, alpha tests, mip selection, compressed formats and cache residency.
   Existing extended compression stays opt-in; unsupported formats retain their
   correct fallback. Smaller textures alone do not establish bandwidth savings.
4. **Framebuffer configuration:** verify actual sample count and formats before
   proposing an MSAA switch. The current backbuffer, scaled scene target and
   offscreen pool already select `SCE_GXM_MULTISAMPLE_NONE` in `runtime/main.c`
   and `runtime/xv_d3d.c`, so disabling MSAA is not a pending gain. Likewise
   identify whether Xbox flicker/interlace
   settings create real Vita passes. No benefit can come from disabling a feature
   that is already absent. Pixel format changes require HUD/alpha/depth checks.
5. **Geometry/binning:** use existing model LOD and separately count triangles,
   indices, draw setup and vertex preparation. Improvement insensitive to pixel
   resolution but sensitive to geometry is evidence for this path. Lower draw
   distance requires validated visibility semantics; arbitrary object deletion is
   not a correct optimization.

PowerVR's own [geometry/state guidance](https://docs.imgtec.com/performance-guides/graphics-recommendations/html/topics/sorting-geometry-effectively-on-powervr.html)
supports preserving transparency ordering and reducing unnecessary target/state
changes. Its [architecture guidance](https://docs.imgtec.com/starter-guides/powervr-architecture/html/optimising-for-powervr-index.html)
also advises avoiding unnecessary blending. Apply principles, not newer GPU API
features, to SGX/GXM. A mid-frame scene boundary can increase attachment traffic;
it is not automatically a synchronous CPU stall or necessarily catastrophic.
Confirm which data is stored/reloaded and whether the boundary is required.

## CPU checks and serial floor

- Separate guest simulation, scene preparation, vertex/texture conversion,
  per-draw HLE, GXM submission, explicit waits and resource retirement. Keep nested
  timings distinct; never sum overlapping scopes as a frame-time budget.
- Current captures are about 120–307 draws/frame depending on the view, not an
  established 500–800-draw problem. One view's 7.6 ms / 120 draw HLE calls is about
  63 microseconds per draw, including nested preparation. Count actual GXM draws
  separately because UI and skipped commands change that total.
- Inspect real thread placement and work: guest owner, core-0/core-1 object
  workers, render pump, texture/upload work and asynchronous reporting. Those
  threads share three cores; high utilization is not proof of useful parallelism.
  Shared visibility/light locks can serialize work despite active workers.
- Measure exclusive owner time and dependency waits to estimate a serial floor.
  CPU utilization or clock frequency cannot supply it. Keep the native spatial
  query and read-only snapshot work on the list; the first guarded query adapter
  was slower in ARM instruction tests and remains disabled.

## Acceptance and captured data

First implement/test the visual controls, then tile-store reduction and selected
fragment diagnostics, then submission suppression if the first tests still leave
the bottleneck ambiguous. Continue native CPU work on measured hot paths.

Capture mean/p95/p99 frame time, completed frames, camera validity, per-core
busy/wait time, HLE draws and GXM draws, indices/vertices, shader/target counts,
attachment policy, texture format/mips, GPU notification bounds, queue depth,
slot waits, query waits, logger errors and screenshots outside measured arms.
Use campaign combat and driving after stationary attribution tests.

Expected gains for untested candidates are **unknown**. The measured resolution
tradeoff above is a reference, not a prediction for another scene. Keep useful
changes together only after correctness and repeated combined measurements;
leave regressions and inconclusive experiments disabled. Stable 20 FPS still
requires frames near or below 50 ms across representative play.
