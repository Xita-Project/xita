# Offscreen render targets

Implemented in `recomp/kernel/xd3d.c`, `xv_d3d.c`, `xv_ui_gxm.c` and the
`main.c` render pump. Hardware validation is still pending. Default `XV_DROP_RT=0`
records and renders offscreen clears, mesh draws and supported immediate UI
quads. `XV_DROP_RT=1` restores the diagnostic discard behavior; restart after
changing the setting.

## Surface identity and sampling

Xbox pixel-container headers contain Common at +0, physical Data at +4, Format
at +12 and Size at +16. `CreateTexture` allocates a header and guest physical
pixels through `make_pixel_container`. `GetSurfaceLevel` allocates another header,
sets Data to the owning texture's Data plus the level offset, and retains the
texture in the surface's Parent field at +20. Linear level aliases now describe
the level's dimensions and pitch instead of copying the parent's size verbatim.

This Halo symbol manifest does **not** name `CreateImageSurface`; it previously
had no HLE implementation. An implementation now creates the same pixel-container
layout with SURFACE type, one level and no parent, ready for manifests that name
it. Native guest-created surface headers use the same target lookup.

`SetRenderTarget` reads dimensions from Size (linear) or Format's log2 dimensions
(swizzled). A non-NULL surface other than the device's backbuffer selects an
RTT slot. NULL retains the currently bound color target. Binding the backbuffer
restores target zero. The binding persists across Present.

Identity is the masked physical Data address, including a mip-level offset;
geometry and format must also agree on target registration. Different surface
and texture headers referencing those pixels therefore resolve to the same
storage. The owning allocation's base address is retained separately for release.

Each slot has an independent GPU texture and color surface pointing to the same
allocation. `xv_d3d_render_target_texture` precedes CPU decode/cache lookup in
both mesh and UI paths. Draw recording copies the texture control word, including
its stable GPU pointer. Mesh sampler addressing and filtering are applied to that
copy as before. CPU-decoded cache entries for the same guest address cannot
supersede an active RTT alias. Pool release invalidates the runtime identity;
releasing a surface also relinquishes the reference it held on its parent.

GPU output does not overwrite guest memory. Xbox swizzled layouts, linear guest
pitches, and GXM's padded linear rows are different. Writing the GXM surface into
the guest allocation could corrupt another allocation or be decoded incorrectly.

## Pool and memory

There are at most eight slots. Each owns:

- One `SceGxmRenderTarget` from `sceGxmCreateRenderTarget`.
- One linear `SceGxmColorSurface` and one single-level linear `SceGxmTexture`.
- One private linear S8D24 `SceGxmDepthStencilSurface`.
- One uncached USER_RW block, mapped GXM READ | WRITE, containing color pixels
  and a separately page-aligned depth region.

Color rows pad to eight texels. Depth width and allocation height pad to 32
samples; depth and color allocations round to 4 KiB. Width and height are limited
to 1024. The 16 MiB pool limit includes these allocations **and** the driver memory
reported by `sceGxmGetRenderTargetMemSize`. For example, a 256x256 ARGB8 slot needs
512 KiB of pixel/depth storage plus driver memory; a 640x480 slot needs about
2.35 MiB plus driver memory. All storage is allocated lazily.

Supported Xbox formats and linear equivalents:

| Format | Xbox codes | GXM color / sampling |
| --- | --- | --- |
| ARGB8 | 06, 12 | ARGB8 / ARGB8 |
| XRGB8 | 07, 1E | ARGB8 / XRGB8 with alpha forced to one |
| RGB565 | 05, 11 | RGB565 / RGB565 |
| ARGB1555 | 02, 10 | ARGB1555 / ARGB1555 |
| ARGB4444 | 04, 1D | ARGB4444 / ARGB4444 |
| L8 | 00, 13 | R8 / replicated luminance, alpha one |
| A8 | 19, 1F | A8 / alpha-only |

Storage is retained for live identities, even across frames that do not render
that target. Equal-sized live surfaces get distinct slots: sharing pixels would
lose one surface's contents. On final resource release, slots become candidates
for reuse. Candidates must have gone unused by recording for at least two frame
indices; both recorded lists may contain copied pointers. Before reuse or
allocation, the recording thread drains published pump work and calls
`sceGxmFinish` while the pump is idle. Matching `(width,height,format)` candidates
reuse every GXM object. Other idle candidates are destroyed and rebuilt. No live
identity is evicted. Reassigned storage starts at zero; existing identities retain
contents. Allocation and initialization failures unwind owned objects and memory.

A full pool, unsupported format (including DXT), inconsistent identity geometry,
or oversized target logs a bounded diagnostic and discards that target's work.
It never redirects that pass into the backbuffer. The UI event arrays add 32 KiB
across the two lists. Clear-quad scratch is now 64 slots per frame, adding 6 KiB; frames without RTT
retain the previous 16-clear replay limit.
The existing 32 MiB decoded-texture pool is unchanged. Shutdown drains GPU/display
work before `xv_d3d_shutdown` destroys every target, unmaps every block and frees
it. This bound is deliberate given ROADMAP 1.3's guest RAM and texture-pool costs.

## Command order, scenes and synchronization

Mesh clears/draws carry a target slot. UI events carry the target, UI ring index,
batch index and position between mesh commands, in a separate fixed array so
normal mesh command capacity and snapshots remain unchanged. Core 1 merges these
sequences in their original order. Only a change of target between commands
requires a new GXM scene. Repeated visits to one target are not reordered or
collapsed across intervening work.

For `backbuffer -> A -> backbuffer(samples A) -> A -> backbuffer(samples A)`, the
pump renders five scene segments in that order. It starts outside a scene and
leaves the final backbuffer scene open for the existing EndScene, heartbeat and
display flip. Immediate UI batches render once, in the right scene; only the
debug overlay is appended at the end. Their ring index comes from the recorded
frame rather than a newer published UI frame.

Synchronization intentionally uses a full GPU fence:

1. `sceGxmFinish` before an RTT frame starts ensures the preceding frame has
   finished sampling storage that this frame may overwrite.
2. Every scene transition calls `sceGxmEndScene`, then `sceGxmFinish`, before
   beginning the next scene. This waits for fragment processing and surface
   stores, not just CPU submission or vertex completion.
3. The caller ends the final backbuffer scene and uses its existing display sync
   object for heartbeat/queue/flip. Later RTT frames or pool mutation fence that
   work before overwriting/freeing texture storage.

[VitaSDK documents `sceGxmFinish` as blocking until GPU rendering finishes](https://docs.vitasdk.org/group__SceGxmUser.html).
No offscreen sync objects or notification slots are needed with this stronger
fence. Scene `scenesPerFrame=1` resources are reused only after completion. This
preserves the one-published-frame-in-flight CPU pipeline, but serializes GPU
scene transitions and can increase pump time. Replacing these waits with fragment
notifications/dependency sync requires device measurements and separate work.

Offscreen depth storage is private per slot and force-loaded/stored so revisiting
a target retains it. For frames split by RTT, a copy of the backbuffer depth
surface forces stores and enables loads after its first segment. The caller's
backbuffer descriptor is unchanged. The full-target viewport uses the offscreen
dimensions; backbuffer replay restores 960x544. Begin/EndScene errors are logged
and abort replay of that frame. A draw binding its active color storage as a
texture is conservatively skipped and logged: simultaneous attachment feedback
is not a defined render-to-texture dependency.

Frames with no offscreen commands retain the existing single BeginScene,
mesh replay, UI replay, EndScene and display submission path, with the same
backbuffer surfaces, viewport, shaders, uniforms and textures. RTT memory and
fences are not used in that path. Pixel-for-pixel equivalence still requires
on-device capture comparison; it cannot be established by a cross-build.

## Validation and remaining device work

`python3 tools/test_render_targets.py` compiles the actual pool/mapping/scheduling
functions against asynchronous host GXM mocks. It verifies producer/consumer
order, repeated writes, interleaved UI, NULL target retention, header aliases,
distinct same-size targets, delayed storage reuse, pool and size limits,
allocation/map failure cleanup, scene failure handling, shutdown cleanup and
`XV_DROP_RT=1`. The mock only publishes a scene's pixels at Finish, so omitting a
fence fails the dependency checks. It cannot validate GXM image layouts or pixels.

Build validation uses the requested `tools/recomp.sh` invocation with the supplied
XBE and Python, followed by `make RECOMP=1 -j8`. Generated code/game assets are not
part of the change.

On a Vita, verify all of the following:

- Compare identical recorded Blood Gulch frames with the previous build: terrain,
  multipass lighting, sky, fog, HUD, color and depth must be identical when no
  offscreen pass occurs. Compare the diagnostic `XV_DROP_RT=1` mode as well.
- Exercise zoom/scope overlays, active-camera/monitor screens, shield damage
  flashes, night vision, object shadows and any campaign offscreen effects.
  Check orientation, dimensions, UV edges, alpha, blending and depth occlusion.
- Inspect target logs for actual Halo formats and sizes, failed BeginScene calls,
  unsupported formats, feedback warnings and pool exhaustion. Validate especially
  RGB565, single-channel formats and non-eight-aligned logical widths.
- Revisit the same target multiple times in one frame, render A while sampling B,
  then sample A from the backbuffer. Check for stale frames, flicker and missing
  fragments. Check backbuffer color/depth preservation on each return.
- Run map changes, repeated zoom/effect activation and extended sessions. Check
  that live allocations remain within budget, released objects are reused,
  display flips remain stable and shutdown has no GXM faults.
- Measure pump time/frame rate with and without RTT. Full GPU waits are a known
  performance tradeoff, not a claim that the 25 fps target is met.

Assumptions and limits: Halo's relevant passes clear/draw their initial contents
and sample level-zero 2D targets. CPU LockRect readback, CPU writes to an already
rendered target, automatic mip generation, rendering cube faces and sampling
them as cubes, explicit guest depth-surface sharing, custom sub-viewports,
multisampling and attachment feedback are not implemented here. A nonzero mip
surface can get its own offset-keyed slot, but sampling its parent as a complete
mip chain is not supported. Existing stencil-clear and shader/alpha-test limits
also remain. These need separate implementation if device traces show Halo
requires them; the host checks cannot establish that all named visual effects
are now complete.
