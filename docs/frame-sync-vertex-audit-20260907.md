# Frame synchronization and vertex submission audit — September 7

Follow-up: the [per-slot retirement implementation](slot-pipeline-20260907.md)
addresses the ownership/cache prerequisites below. This document records the
pre-change audit and its hardware baseline.

The largest measured renderer interval is the final completion wait. The mesh
vertex path is already mostly zero-copy, and the measured draw count is below
the proposed sorting threshold. There is also an unresolved cache-visibility
contract that must be addressed before increasing frames in flight.

This is an audit and instrumentation change, not a triple-buffer conversion or
a measured performance improvement. The Vita retains the verified opaque-material
build (`e2f70270…`). The local cutout shader candidate is not deployed.

## Evidence and limits

Use the September 7, 14:27 USB collection:
`/home/birchwoodgod/xita-backups/2026-09-07-142628-opaque-material-hardware/`.
The source log is `data/xita/xita.log`; exact benchmark phases and supporting
report boundaries are in `material-analysis.json`. Standard settings and the
camera match in all three phases. See the [hardware report](hardware-20260907-opaque-material.md).

| Supporting measurement | Off before | Opaque optimization on | Off after |
| --- | ---: | ---: | ---: |
| Final `sceGxmFinish` elapsed, ms/frame | 73.654 | 67.941 | 73.650 |
| Command submission elapsed, ms/frame | 6.163 | 6.183 | 6.113 |
| Display queue elapsed, ms/frame | 0.036 | 0.037 | 0.036 |
| Previous-frame Finish elapsed, ms/frame | 0.006 | 0.006 | 0.006 |
| Guest visibility wait, ms/guest frame | 35.375 | 32.084 | 36.574 |
| Completion-to-guest-resume, microseconds/result | 91.4 | 91.3 | 97.0 |
| Guest draw preparation elapsed, ms/frame | 12.123 | 11.673 | 9.909 |
| Actual GXM draw invocations/frame, window average | 231 | 231 | 231 |

The six supporting render windows are mesh frames 1620–1739, 1800–1919,
and 1980–2099. Each logs 13,860 GXM draws over 60 frames. The older
`draws/frame 226` field counts guest draw requests; it excludes additional
renderer work such as clear/overlay draws. These averages do not reveal the
maximum in an individual frame.
Across all 39 render reports in this collection, window averages range from
3.88 to 335.45 calls/frame; no reported window average exceeds 500. Individual
frame spikes remain unknown until the new minimum/maximum counters run.

The exact 120-frame FPS phases are 6.730 / 7.079 / 6.877. Supporting 60-frame
reports have different boundaries. Guest, pump, and query intervals overlap:
do not add their times or interpret Finish elapsed as exclusive GPU execution.
The display queue is not the dominant measured wait. Lower completion waiting
after the shader change supports investigating GPU work and its dependencies,
but does not isolate shader ALU, bandwidth, overdraw, or texture cache misses.

## Synchronization and ownership

| Location | Current behavior and consequence |
| --- | --- |
| `main.c:xv_pump_thread` | Calls `sceGxmFinish` after every submitted frame. Only then publishes visibility results and `g_frame_completed`. The pump cannot submit the next frame while waiting. |
| `xv_ui_gxm.c:xd3d_r_present_inner` | Drains all previously published work **before** `EndFrame` and UI rotation reset the other slot. The guest can record the next frame during rendering, but cannot publish another frame until the previous one completes. |
| `main.c:xv_present` | Release/acquire ticket counters and sticky event notifications, with a defensive completion check. No renderer mutex here. |
| `xv_d3d.c:xv_d3d_render_targets` | Previous-frame Finish before reusing shared targets. Intermediate target Finish is disabled by default (`XV_RT_QUEUE=1`); retained for synchronous fallback and errors. All six measured windows have zero intermediate target Finish calls. |
| `main.c:xv_gfx_render_frame` | Conditional Finish for previous-frame feedback, used by loading blur. |
| `xv_ui_gxm.c:ui_tex_purge_if_needed` | Drains the pump and calls `sceGxmDisplayQueueFinish` before recycling the texture pool. Conditional maintenance; no measured purges in the benchmark. |
| Shutdown, resolution/target destruction | Explicit drains protect resources being freed. These are not ordinary per-draw waits. |
| Display callback | Waits for vblank; `DisplayQueueAddEntry` is timed separately. The callback also has an opt-in framebuffer dump that must stay disabled for performance comparisons. |

**The application is double buffered, not triple buffered.**
`XV_DISPLAY_BUFFER_COUNT=2`, `XV_DISPLAY_MAX_PENDING=2`, `XV_NUM_LISTS=2`,
and `UI_FRAMES=2`. The four ticket entries in `g_mesh_ring` do not create four
copies of the referenced command/vertex/index storage. GXM's VDM, vertex,
fragment, and USSE command rings are separately allocated driver rings, not
additional application vertex-buffer slots.

Changing these constants alone would not remove the drain or final Finish.
Deleting Finish would release indices, immediate vertices, clear/UI buffers,
and visibility storage while the GPU could still use them. Existing tests and
past geometry failures specifically protect against this reuse error.

A safe deeper queue needs per-slot ownership and GPU completion, separate
submitted/completed tickets, frame-specific UI publication instead of only
`g.pub`, and a lifetime audit of borrowed guest streams, texture refreshes,
shared render/depth targets, previous-frame feedback, and visibility generations.
CPU preparation, CPU submission, GPU completion, and display release are four
different milestones. Wait when acquiring a still-owned slot or consuming an
unfinished result; retain drains on resource destruction. Triple buffering can
increase overlap but cannot guarantee that a CPU never waits for a slower GPU.

## Micro-kernel scheduling

The recompiled game uses `recomp/kernel/xk_thread.c`, not the mock scheduler
inside `main.c`. Guest fibers are real Vita kernel threads with semaphore baton
handoffs in `xk_os_vita.c`; only one executes guest code at a time. The bootstrap
requests core 0, actual guest-fiber affinity is left to the kernel, and the
render pump explicitly requests core 1. The hardware trace shows most guest
utilization on core 2; bootstrap placement does not pin all guest work to core 0.

Visibility-result waits park the requesting guest fiber through `xk_wait_u32`,
allowing other runnable guest fibers to execute. The scheduler wakes on a
completion event. The measured roughly 0.09 ms resume delay is small relative
to the 32–37 ms spent awaiting results. Making notifications more frequent
cannot make an unfinished GPU result available sooner. Present's event wait
blocks its host fiber without returning the guest baton; measured Present
waiting is only about 1.5–1.65 ms/frame in these windows, but cooperative
backpressure should be considered in a future queue implementation.

Texture conversions can split with a core-0 worker; large geometry sorts can
split with core-0/core-1 workers. Those jobs join on semaphores before using the
results. These are bounded independent jobs, not parallel AI/physics execution.
The benchmark has no texture reuploads and no parallel geometry-sort jobs,
explaining why these workers do little in this view. The audio mutex is outside
the rendering submission loop. Releasing the guest baton to run arbitrary
gameplay simultaneously would violate existing shared-state assumptions.

## Vertex CPU overhead and cache visibility

`xv_d3d.c:record_draw` binds vertex streams directly from translated physical
guest addresses, with base-vertex offsets. It does not unpack every mesh vertex
or multiply every position by matrices on the CPU. Guest RAM is GPU-mapped
`USER_RW`. `shaders/halo_vs_16.cg`, for example, unpacks NORMPACKED3 inputs and
performs position/UV math in the vertex program. `halo_vs_03.cg` performs the
UI position/UV transforms. GXM vertex attributes describe the stored formats.

CPU work that remains has specific purposes:

- `retain_indices` copies changing guest index lists and computes bounds. The
  default uses cached chunks before writing uncached GPU scratch; it avoids
  scanning that uncached destination. Keeping raw pointers to Halo's rebuilt
  visible-world indices previously mixed frames when the camera moved.
- Constant windows and constant attributes are captured per draw. Uniform
  normalization for scene-copy/composite UVs adjusts a few constants on the CPU;
  the vertex program still applies them to vertices.
- Immediate UI, HUD, and flare paths pack attributes and colors into retained
  storage. Offscreen flare rejection also performs CPU clip tests. Calling
  this entire path “raw zero-copy” would be inaccurate.
- Native matrix/quaternion and clipping routines in `xk_math.c`/`xk_clip.c`
  produce guest-visible results. They are distinct from the GXM vertex shader;
  moving all of them to a vertex program would require preserving their CPU
  consumers and would potentially introduce readback dependencies.

In the controlled windows, index preparation is about **1.67–1.68 ms/frame**,
stream preparation **0.334–0.339 ms**, and constant preparation **0.94 ms**.
Those timers include elapsed/preemption costs, and stream timing excludes the
later flush pass. They do not support a theory that this bridge is spending
tens of milliseconds transforming all mesh vertices before `sceGxmDraw`.

**Unresolved coherency issue:** `arm-vita-eabi-nm` on the archived installed
build's `xita.elf` reports `w xv_kmod_dcache_clean`, an undefined weak symbol.
The checks in `main.c` and `xv_texture_worker.c` therefore bypass the intended
clean operation. A memory type and `sceGxmMapMemory` call alone are not evidence
that this intended cache-maintenance path is active. This was also observed in
the [September 5 audit](hardware-20260905-followup.md).

There are latent ordering/ownership issues in that path as well: Present calls
`xv_gpu_flush_pending` before `xv_ui_gxm_frame_flip` queues the current UI range;
the pump queues its freshly written overlay/clear ranges into the same global
`g_flush` table while the guest can also record ranges. Merely wiring in the
missing helper would expose a shared-queue race and late cleans. Resolve the
platform coherency mechanism, separate recording/pump ownership, and ensure
writes are visible before submitting their draws. Current evidence does not
prove this is the cause of an observed rendering defect or the FPS ceiling.

## Texture layout and state reduction

The recompilation path resolves `.map` resources through
`xv_ui_gxm.c:ui_texture_for_pal` on first use and on detected changes; it does
not preconvert every bitmap in one map-initialization pass.

| Texture path | Stored GPU layout |
| --- | --- |
| Eligible square power-of-two DXT | BC blocks reordered to PowerVR order, `sceGxmTextureInitSwizzled`; no full RGBA decompression |
| Other ordinary 2D textures, including default rectangular DXT fallback | Decoded RGBA, padded rows and applicable mip chains, `sceGxmTextureInitLinear` |
| Cube maps | Six faces in cube layout; extended compressed cube/rectangular BC support exists behind `XV_EXTENDED_BC` |
| Render targets, previous-frame images, upscale source | Linear/strided surfaces; these are GPU-produced images, not immutable map uploads |

The upload pool is 32 MiB of GPU-mapped cached `USER_RW`, not CDRAM VRAM.
The measured live pool is 8,489 KiB, with no measured refreshes or purges. This
rules out repeated conversion in those windows, but says nothing about sampling
cache efficiency. There are no texture-cache miss counters in this capture.

Do not relabel linear bytes as swizzled. A future RGBA swizzle comparison must
reorder every supported mip, preserve cube/NPOT and render-target distinctions,
retain streaming invalidation and uploaded-opacity proofs, and measure the same
hardware view. First-use preparation fits the existing streamed-resource model
better than assuming all map textures are permanently available at map startup.

`xv_draw_state.h` already skips repeated vertex/fragment program, depth, write,
and cull bindings within uninterrupted mesh ranges. Shader links and sampler
descriptors are cached. Texture bindings and uniforms remain per draw.
Global shader/texture sorting would cross blended, multipass, UI, clear,
render-target, and visibility-query boundaries. At 231 GXM calls/frame this
capture does not meet the user's 500–800 threshold. If other views do, first
measure state rebinds and restrict any sorting to proven order-independent
ranges. Adjacent identical texture-binding suppression can preserve order.

## Changes and validation

Added `[render-draws]` to the existing render profiler: actual GXM API invocations
per 60-frame window, average, minimum, maximum, and counts of frames exceeding
500 and 800 calls. Mesh, UI, clear, overlay, and upscale call sites already use
the shared wrapper. Failed API calls count as invocations. This adds counters
and one line per report, no new clock reads, synchronization, or per-frame I/O.
It follows `XV_RENDER_PROFILE` enable/disable behavior.

Host checks pass for draw thresholds/reset/disabled profiling, frame ownership,
completion notifications, index snapshots, cached state, texture layouts and
worker lifetime. The production pump test verifies GPU completion precedes
scratch release. The modified profiler compiles with the Vita ARM toolchain.
No new emulator/hardware run or full application deployment was performed for
this audit. The in-progress cutout tests also pass after correcting the test's
handling of the compiler's zero padding; that candidate still needs native
application and emulator validation.

Next: resolve the cache-visibility/ownership contract, then prototype per-slot
GPU retirement with delayed-GPU tests before enabling a deeper queue. Preserve
visibility correctness and index snapshots. Keep the cutout shader comparison
separate so any hardware gain can be attributed. No repeat of the unchanged
hardware benchmark is needed for this audit.
