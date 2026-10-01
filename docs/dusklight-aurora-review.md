# Dusklight / Aurora ideas for Xita

Reviewed September 5, 2026, following the user's Twilight Princess Vita suggestion.

## What is available

The [Dusklight Vita author](https://capitanbarbacoa.itch.io/duskgxm) identifies
Aurora/GXM as the rendering layer and says the Vita-specific code is not currently
shared. The page documents selectable resolution, optional dynamic resolution,
costly effects and a removed HUD. These are author reports, not measurements of
Xita or evidence of a sustained 25 fps baseline.

The public source reviewed is:

- [Dusklight](https://github.com/TwilitRealm/dusklight/tree/58c31d304c70effe9454aa0200c578f7ac86144f),
  revision `58c31d304c70effe9454aa0200c578f7ac86144f`.
- Its pinned [Aurora](https://github.com/encounter/aurora/tree/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca),
  revision `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`.

This is not a review of the private Vita backend. Upstream Aurora implements
GameCube/Wii graphics over WebGPU; Xita implements Xbox/NV2A graphics over GXM.
Dusklight builds reconstructed game source, while Xita's generated C still models
x86 instructions and register/stack state. Their renderer and threading code
cannot replace ours directly. No external implementation was copied into Xita.

## Useful techniques, in priority order

### 1. Give each submitted frame ownership of its draw data

Aurora's [FramePacket](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gfx/frame_packet.hpp)
carries vertex, index, uniform and texture-upload ranges. The
[recorder](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gfx/recording.cpp#L1099)
copies supplied vertex/index bytes into those ranges. Its
[frame code](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gfx/frame.cpp#L216)
separates command-packet retirement from the asynchronous remapping that makes
upload storage reusable.

**Application to Xita:** `record_draw` in `xv_d3d.c` retains pointers into guest
vertex/index memory. `main.c` allows recording the next frame while the pump
submits the previous one. Its weak cache-clean hook is unresolved in the current
ELF. These are concrete lifetime/visibility concerns, but are not a proven cause
of geometry spikes or flashlight disappearance.

Prototype GPU-visible snapshots for mutable draw ranges with bounded storage and
GPU completion before reuse. Measure bytes copied and ring high-water usage;
deduplicate unchanged ranges without assuming guest pointers imply immutable
data. Do not make the whole guest arena uncached or copy all guest RAM every frame.
Test mutation after recording, index reuse and delayed GPU completion locally,
then compare the same wall/camera/flashlight sequence on hardware.

### 2. Avoid repeating draw preparation when state is unchanged

Aurora's [command processor](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gx/command_processor.cpp#L395)
tracks pipeline, texture and uniform dirtiness separately. Its
[pipeline cache](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gfx/pipeline_cache.cpp#L436)
has a repeated-key fast path and a hash lookup.

**Application to Xita:** linked fragment programs and constant snapshots already
have caches. Remaining work includes the per-draw shader-pair table scan,
full constant comparisons and repeated texture/sampler preparation. Add counters
for actual state changes and cache hits first, then use complete state keys or
generation counters to reuse work. A texture's contents can change without its
header address changing, so streamed updates and palette changes must invalidate
reuse. Preserve the selected-mip texture-cache correction already in the CPU
monitor candidate.

### 3. Merge only compatible adjacent draws

Aurora's [draw merging](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gx/command_processor.cpp#L497)
checks unchanged state, compatible primitives and contiguous vertex/index ranges.
It extends the preceding draw and records a merged-draw count.

**Application to Xita:** count potential merges in actual hardware draw traces.
Then consider adjacent HUD or BSP draws with identical shader, texture, constants,
depth, blend, cull and render-target state. Preserve draw order and enforce index
limits. Flashlight and transparency passes must remain correct across state changes.
The Vita author's HUD removal motivates measuring UI cost; it does not establish
that Halo's HUD should be removed.

### 4. Use resolution scaling when GPU cost warrants it

The Vita port's published configuration includes width choices of 960, 720 and
640 and optional dynamic resolution. For Xita, compare CPU utilization, frame
timers and a controlled reduced-resolution experiment before implementing an
automatic controller. Lower resolution will not remove expensive x86 game logic.
Keep aspect ratio, viewport/scissor, depth targets and readable HUD output correct.

## Threading and the next hardware test

Aurora uses a [bounded render-worker queue](https://github.com/encounter/aurora/blob/749d6ee7a22bdfab78c8ece9047bca5d79aa72ca/lib/gfx/render_worker.cpp).
Xita already overlaps game recording and rendering on separate threads. Safe data
ownership should precede moving further conversion/preparation work to workers.
The [first CPU-monitor hardware run](hardware-20260905-cpu.md) shows core 2 carrying
most of the CPU load, so its availability must be measured rather than assumed.
The private Vita port's core affinities are unknown; public desktop thread code
does not establish its hardware allocation.

Keep `xita-20260905-cpu-monitor.vpk` as the next hardware baseline. Use that run
to evaluate the cache correction and core load. The reference review adds research
and implementation priorities; it does not claim an additional shipped rendering
fix or measured FPS gain.
