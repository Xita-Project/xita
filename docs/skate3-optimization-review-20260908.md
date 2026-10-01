# Skate3-Mobile review and Vita optimization plan

The useful lesson is to remove redundant translated work at a well-understood
engine boundary. Copying Android settings or adding threads does not provide
that boundary. Xita already has asynchronous submission, owned frame slots,
native math helpers and bounded workers; the remaining work needs measurement
and stronger knowledge of Halo's render and resource lifetimes.

The [September 8 combined build is installed](hardware-20260908-weapon-menu.md).
Separate [exact draw scans](draw-scan-20260908.md) and
[fused vertex copies](vertex-copy-20260908.md) pass local native, host, ARM and
emulator checks. Both remain opt-in and uninstalled, with no measured Vita FPS
gain. The emulator also reports pre-existing shader varying-link errors; these
are covered by a separate [rendering candidate](shader-varyings-20260908.md). No GitHub push or publication was performed.

## Reference and scope

Reviewed [Skate3-Mobile at commit `91e4b747`](https://github.com/Buku313/Skate3-Mobile/tree/91e4b747ae4fdafa11997d20d20ad23843ba3d3c),
dated September 7, 2026, using its repository tree and 68 selected source/document
files. This is an Xbox 360 PowerPC-to-ARM64 Android project with a custom Vulkan
scene renderer. Its target CPU/GPU, graphics API and memory budget differ from
Vita's ARMv7/GXM implementation. Its frame caps and target-device claims do not
establish a Vita performance estimate. See its [build and architecture overview](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/README.md).

The inspected tree does not provide a root license covering the whole project.
Its [third-party notice](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/docs/THIRD_PARTY_NOTICES.md)
identifies particular bundled components; it does not establish permission to
copy all renderer code. This review uses architecture as inspiration. No upstream
implementation or shader was incorporated or executed.

## What transfers

| Observed technique | Application to Xita |
| --- | --- |
| Capture scene meshes/materials before guest packet construction | Potential larger project: replace a proven Halo render helper while preserving its outputs and side effects. Current draw calls still supply necessary state. |
| Cache stable mesh descriptors with invalidation | Cache identity must include resource generation, backing data and layout. Retain exact validation until all relevant writes are understood. |
| Prioritize visible work over background prewarming | Useful for a future bounded preparation queue. Start with immutable job inputs; the render thread retains GXM ownership. |
| Separate dynamic palettes from static mesh metadata | Bone palettes, visibility indices, UI attributes and effect constants remain current-frame snapshots. |
| Native math and compact data preparation | Extend the existing native helpers one measured routine at a time, with differential tests for guest-visible behavior. |
| Reduced resolution/detail profile | Already available. It cannot remove the serial CPU work and dependencies visible in our logs. |

The Android static-dispatch setting defaults to eight frames, but the native
capture still receives the complete scene each frame; dynamic/skater packet
dispatch remains live. It suppresses work discarded by that separate renderer.
Halo's current GXM bridge uses the translated dispatch, so skipping it would lose
draws or state. Ambient simulation throttling is a separate quality tradeoff;
it is not a suitable baseline fix for Halo AI or physics. [Dispatch implementation](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/src/skate3_native_render.cpp#L35).

The mesh cache validates object and backing-buffer fields, periodically refreshes
payload fingerprints and materials, and invalidates on registration/loading
events. Android static fingerprint refresh can be 16 frames apart. Xita has
already seen dynamic indices and reused guest addresses cause spikes; periodic
or sampled checks cannot replace exact ownership here. The reference's 1,280 MiB
texture budget is also unsuitable for this Vita runtime. [Cache implementation](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/src/skate3_native_scene.cpp#L2611).

Decode workers prioritize dynamic data, then visible misses, then background
prewarming. Render-thread commitment orders the GPU work. Its optional opaque
sort is front-to-back, not a blanket shader/texture reorder, and the Android
profile disables that sort. Its stale occlusion decisions also need movement
and identity guards. These are useful design constraints, not evidence that
arbitrary Halo draw sorting is safe. [Workers](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/src/skate3_native_scene_gpu.cpp#L5100)
and [draw ordering](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/src/skate3_native_scene_gpu.cpp#L10601).

Palette hooks capture data after the guest finishes updating/uploading it and
associate the data with the relevant entity. The transferable principle is to
observe finalized state, including identity, before delegating work. Moving
mutable guest pointers to a worker would undermine that guarantee.
[Palette capture](https://github.com/Buku313/Skate3-Mobile/blob/91e4b747ae4fdafa11997d20d20ad23843ba3d3c/src/native/skate3_native_palette.cpp).

## What the Vita measurements support

The [previous hardware session](hardware-20260907-frame-constants.md) includes
driving, shooting and looking around. It averages **11.00 FPS at 640×360** across
6,780 sampled frames. These phase values overlap and must not be added together:

| Measurement | Mean per frame | Implication |
| --- | ---: | --- |
| Frame duration | 90.88 ms | Reaching 20 FPS needs a 50 ms frame, about 40.9 ms less elapsed time. |
| Draw preparation | 10.18 ms | Worth reducing, but eliminating it entirely would not meet the target. |
| Raw stream preparation, included above | 3.93 ms | Retention and exact comparison cost CPU time; removing snapshots would reintroduce ownership bugs. |
| Submission | 3.83 ms | Some state redundancy remains, but this is not the largest measured interval. |
| Display queue | 0.030 ms | Removing another routine display wait is not supported as the main answer. |
| Exact visibility wait | 23.44 ms | The installed deferred-result candidate moves independent work ahead of the dependency. Measure the result before changing this further. |
| GPU completion notification latency | 53.42 ms | Includes overlap and scheduling; it is not exclusive GPU execution time. |

Gameplay averages roughly 146 GXM draws per frame and reaches 254 in that
capture. A 500–800-call threshold is not the explanation for those frames.
Alpha blending, stencil, render targets and visibility queries constrain order.
Already cached program/state bindings can be improved without reordering passes.

## Current pipeline audit

- **Synchronization:** normal frame completion uses fragment notifications and
  slot retirement. Remaining full waits serve shutdown, resource changes,
  explicit diagnostic fallback or error recovery. The hardware log records no
  ordinary Finish calls. UI batch recording and upload accounting are separate;
  UI does not acquire a display slot to flush each batch.
- **Memory:** three frame slots own indices, immediate attributes, vertex uploads
  and constants until their GPU completion. The default queue is intentionally
  single-flight while crash validation continues; Graphics can enable experimental
  triple buffering. Having three allocations does not by itself guarantee useful
  CPU/GPU overlap.
- **Uploads:** raw vertex snapshots use GPU-mapped `USER_RW_UNCACHE`; the cached
  mirror is for exact CPU comparison. A write barrier precedes submission.
  Cached GPU memory would require a matching cache-clean protocol. Dropping the
  barrier or mapping live guest storage directly is not a safe zero-copy change.
- **Shaders:** vertex/UV transformations already execute in GXM programs. Guest
  matrix, clipping and palette work also serves engine decisions; moving all of
  it to shaders would change the CPU-visible results.
- **Textures:** native compressed layouts already use Vita swizzling. The decoded
  RGBA swizzle experiment remains separate and uninstalled. It should be compared
  after the pending visibility test, without changing texture cap or resolution.
- **Workers:** texture conversion can split onto core 0; large geometry sorts can
  use cores 0 and 1, with the core-1 render pump ahead of auxiliary work. These
  jobs are bounded. Low aggregate use on another core is not proof that the next
  serial guest task can execute there concurrently.

The runtime uses its installed [VitaSDK GXM declarations](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/gxm.h)
and [ARMv7 NEON operations](https://arm-software.github.io/acle/neon_intrinsics/advsimd.html).
Android FP16/dot-product flags and Vulkan synchronization APIs are not interchangeable
with this target.

## Additional Vita upload findings

The CPU mirror is copied into GPU memory after it has been filled from guest
data. The [validated local copy experiment](vertex-copy-20260908.md) loads each source
chunk once and writes both
destinations. It retains both allocations and all existing ownership/barriers.
For a new N-byte snapshot, this removes N bytes of explicit mirror reads; the
two N-byte destination writes remain. Cache/bus traffic and elapsed savings need
hardware measurement. This is separate from the draw-scan comparison.

The installed SDK declares cache-clean range functions under `psp2kern`, not
the ordinary application CPU header. There is no basis here for changing cached
GPU memory and substituting a plain DSB as cache maintenance.

Also inspected [vitaGL at `cd3791e2`](https://github.com/Rinnegatamante/vitaGL/tree/cd3791e29ff7f1c0ab349f12c7231f4871ce6a75).
Its copy helper selects DMA for eligible transfers of at least 8 KiB and uses
`sceClibMemcpy` otherwise. Xita's linked `memcpy` already imports `sceClibMemcpy`;
renaming that call is not a new optimization. DMA needs a separate size/alignment,
error and hardware-cost comparison. No vitaGL source was incorporated.
[Copy helper](https://github.com/Rinnegatamante/vitaGL/blob/cd3791e29ff7f1c0ab349f12c7231f4871ce6a75/source/utils/mem_utils.h#L112),
[VitaSDK DMA interface](https://docs.vitasdk.org/group__SceDmacMgrUser.html).

vitaGL's `BUFFERS_SPEEDHACK` also bypasses replacement storage when updating a
recently used buffer. Applying that shortcut would conflict with Xita's proven
need to preserve earlier submitted geometry. Keep ownership protection and
optimize the copying inside it.
[Buffer update path](https://github.com/Rinnegatamante/vitaGL/blob/cd3791e29ff7f1c0ab349f12c7231f4871ce6a75/source/buffers.c#L456).

## Next work, in order

1. Collect the installed **L + R + Square** eager/deferred/eager result and the
   driving/death stability report. Compare total frame time and exact-result
   wait together; retain deferred scheduling only on correctness and measured merit.
2. Test the opt-in exact draw scans at unchanged settings. The targeted index
   and constant-check stages total roughly 2 ms/frame in the previous log, so
   even a substantial local speedup should produce an incremental FPS change.
   Compare the separate fused vertex-copy candidate afterward, keeping scan and
   residency settings fixed. It targets part of the 3.93 ms stream-preparation
   interval, not that entire interval.
3. Compare the existing decoded-RGBA swizzle candidate separately. Avoid another
   combined settings change that obscures which path affected the result.
4. Audit higher-level guest helpers for native replacement. Current sampled
   candidates include `0x51E90`, `0x8DDF0`, `0x88B80`, `0x54010`, `0x8BD50` and
   `0xB77C0`. Their names/roles are not assumed from addresses. Initial inspection
   finds plane/distance calculations, recursive traversal and polygon-edge work;
   these may serve collision as well as rendering. First collect bounded inputs,
   outputs, side effects and caller lifetimes, then differential-test one helper.
5. Revisit static BSP/material preparation only after identifying its full
   invalidation boundary. A reusable mesh/material description with current-frame
   indices and palettes is a safer direction than reusing an entire old draw list.

A further CPU-sharing design to investigate is publishing the immutable cached
vertex snapshot to the core-1 render pump, which then performs the uncached GPU
copy before submission. That could move copying off core 2 and combine small
uploads. It is **not implemented**: it also delays GPU submission, and the
diagnostic/mesh-capture paths currently read the GPU destination while recording.
It would need explicit copy completion, correct snapshot-based diagnostics and
tests for every slot reuse/error path before it could replace the current copies.
The two small experiments above keep the existing thread ownership unchanged.

The 1 kHz function profiler samples elapsed time, including waits/preemption;
its percentages are a shortlist, not proof of exclusive CPU cost. New helper
probes should measure a bounded sample and avoid per-call log I/O.

Acceptance remains representative hardware play at a stable 20 FPS, with camera
turning, firing, vehicles, death and campaign transitions. A capped emulator run,
fewer ARM instructions or a ground-facing hardware peak does not meet that gate.

Private evidence, source hashes, deployment records and candidate validation are
under `xita-backups/2026-09-08-065547-skate3-vita-optimization/`.
