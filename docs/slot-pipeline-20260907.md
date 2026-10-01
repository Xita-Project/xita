# Per-slot GPU retirement — September 7, 2026

Implemented asynchronous frame submission for the recompiled Halo renderer.
Normal frames no longer call `sceGxmFinish`. Installed over USB at 16:32 CDT and verified on a fresh read-only remount.
The subsequent [hardware result and recovery](hardware-20260907-slot-pipeline.md)
show no throughput gain and a later driving GPU crash. Single-flight is now the
default. The implementation and initial validation below describe the original
16:32 candidate; removing a wait does not remove GPU workload or visibility dependencies.

## Ownership and submission

`main.c` owns the pump/display queue, so this change also updates that file and
`xv_d3d.c`, beyond the immediate-mode bridge in `xv_ui_gxm.c`.

- Three slots each hold command lists, index/immediate/attribute storage, raw
  vertex snapshots, UI batches, and visibility results. Three display buffers
  retain GXM sync objects; the display queue permits two pending callbacks.
- A published packet captures explicit mesh and UI slots. Final `sceGxmEndScene`
  receives a fragment notification from `sceGxmGetNotificationRegion`. For
  reduced resolution, that notification belongs to the final upscale scene.
- The pump distinguishes CPU submission from GPU completion. It polls completed
  notification words in FIFO order, publishes query results, checks recorded
  geometry, then releases slot ownership. Display callbacks separately release
  scanout buffers; a display callback alone cannot retire vertex storage.
- Present seals and publishes the current frame before acquiring the next slot.
  Only an occupied next slot parks the recording fiber, using a cooperative
  100-microsecond sleep. Other guest fibers remain runnable. Recording buffers
  reset only after acquisition succeeds.
- The pump can submit another frame while a prior notification is pending.
  Display capacity and the FPS cap defer submission without delaying retirement
  until the next capped frame. Ticket wrap is tested independently of slot index.

There is no `sceGxmSyncObjectWait` in the installed user-mode SDK. Fragment
notifications supply the supported completion fence; existing display sync
objects retain GXM's render/display dependency handling.

Full drains remain for resource replacement, texture-pool reclamation,
resolution changes, shutdown, the explicit synchronous render-target diagnostic,
and exceptional failed submissions whose notifications may never arrive.
GXM's internal command/uniform ring allocation can still apply backpressure;
triple buffering is not a guarantee that every GXM call is nonblocking.

## Cache visibility and immutable uploads

The previously weak cache-clean helper had no linked implementation. The
installed user-mode SDK does not expose a general buffer-range cache-clean API;
its kernel cache API is not callable directly by this application.

GPU upload storage now uses `SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE`, including
UI vertices, indices, texture uploads, and per-slot mesh snapshots. An ARM
`dsb sy` completes those stores before publication/submission. This is a store
barrier for uncached memory, **not a replacement cache-clean operation for
cached memory**. The general guest arena stays cached for CPU execution.

Raw guest vertex bytes are copied into the acquired slot. Repeated passes reuse
an upload only after comparing the source against a cached mirror; a changed
source receives a new version. Vertex positions and UV/matrix transforms remain
in the existing shaders. This adds CPU copying/comparison work; it is not a
zero-copy mesh path. The bounded capacity is 8 MiB per slot plus an 8 MiB cached
comparison mirror, or 48 MiB when all three slots have been allocated.

UI and scene publication bookkeeping are separate and recording-thread-owned.
The present hook seals late UI writes **before** the final publication barrier.
Pump-created clear/overlay/fallback data and texture-worker output have their
own store barriers. They do not append to the recording thread's queues.
Texture refreshes preserve older uploads until a drained pool purge, protecting
draws that already captured the old texture descriptor.

## Validation

Candidate archive:
`/home/birchwoodgod/xita-backups/2026-09-07-154720-slot-pipeline/`.

- Native Vita build/link and decoded SELF segment verification pass. Generated
  guest units are preserved from the verified installed baseline.
- Production pump/acquisition host fixtures cover delayed GPU notifications,
  concurrent submissions, display ownership, pacing, ticket wrap, and failed
  submission cleanup. Upload tests cover mutation, retention, alignment,
  exhausted capacity, and allocation failures. Eight ASan/UBSan runs pass.
- Frame/UI handoff, render-target ordering, texture worker/cache/preparation,
  shader selection, input, completion/scheduler, and profiling regressions pass.
- Private Vita3K renders the dashboard, menu, Blood Gulch, campaign loading,
  opening cinematic, and first-person view after skipping the cinematic.
  Pause/leave, the 544/360/544 resolution transition, and benchmark cancellation
  with restoration complete. The private instance was stopped afterward and
  its previous executable/configuration restored.
- Emulator logs show no normal-frame Finish calls, submission-fence failures,
  upload shortages, or draw-storage drops. Six geometry captures report zero
  changed draws. Two submissions pending at once were observed. Snapshot high
  water is 1,582 KiB/slot in this run, below the 8,192 KiB capacity.
- The stationary Blood Gulch single-flight/triple/single-flight comparison is
  19.968 / 19.955 / 19.963 FPS at the 20 FPS cap. It validates phase switching,
  matching camera, and restoration; it establishes no Vita speedup.

Campaign frames reached 663 GXM draw calls in this emulator check. Any subsequent
batching must preserve render-target, transparency, visibility-query, and
depth/stencil ordering; a global texture/shader sort would not be safe.

## Hardware comparison

Installed executable SHA-256:
`76687f93a827e79c303e5bfc3f23ea7b7589f4f63921835004f7d5000d98155b`.
The existing executable allocation was preserved; settings and 657 other
checked files are unchanged. The USB volume is safely unmounted.

Keep the user's standard graphics settings fixed. In a stationary first-person
Blood Gulch view, press **L + R + Square**, release the buttons, and leave the
camera still until the test overlay disappears. Each of three phases settles
for 60 frames and measures 120; allow roughly 90 seconds at the previous speed.
The test restores the configured default automatically. In the recovery build
that default is single-flight. A second press cancels and restores it.

This comparison changes only the number of published frames permitted in flight.
It keeps uncached uploads, shaders, effects, resolution, and frame cap fixed;
it does not reproduce the previous executable's entire synchronous path. The
unvalidated cutout-shader experiment is forced off for all phases.

Next compare actual FPS, occupied-slot waiting, submission time, notification
latency, visibility dependencies, and vertex copy/comparison cost from the Vita.
Notification latency overlaps CPU/GPU work and must not be added to guest time
or described as exclusive GPU execution time. Compare a matched old/new route
as well if upload overhead offsets the queue gain.
