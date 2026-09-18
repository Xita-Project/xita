# Captured vertex preparation

Status: opt-in trial installed and smoke-tested on physical Vita. No measured
frame-rate improvement is claimed.

The range audit mapped more than 99.8% of requested source bytes in two
Blood Gulch views to loaded BSP/model resources. That does not prove those
bytes cannot change. The previous preparation worker also joined each draw
before guest execution resumed; ordinary captured draws were below its
64 KiB dispatch threshold.

`XV_VERTEX_CAPTURE_DEFAULT=1` builds a startup trial that copies vertex inputs
and index-reference masks into a bounded, cached 2 MiB arena. A core 0 worker
checks, packs and prepares those private inputs while the recording owner
continues later commands. The original guest pointer is only a cache identity:
the worker never dereferences it. A rewritten source can therefore produce a
new version without changing an earlier draw.

The FIFO has 32 jobs. Arena or queue exhaustion drains CPU work before reuse;
unsupported requests or worker startup failures retain synchronous preparation.
The owner publishes successful stream pointers and marks failed commands for
skipping, preserving later UI/query positions. Partial success never publishes
half a draw. Preparation drains before frame publication, slot reset, settings
transitions, reporting and shutdown. Existing GPU-copy tickets and GPU slot
retirement still protect uploaded data; a CPU drain is not GPU completion.

`XV_VERTEX_CAPTURE=0` disables the trial at startup. Full geometry diagnostics
use synchronous preparation. No per-frame full-GPU finish was added, and the
trial changes no resolution, shaders, visibility rules or graphics settings.

Validation uses the production uploader and copy worker with pthread-backed
Vita services. Raw and packed layouts cover source rewrites/unmapping, copied
reference masks, arena/queue pressure, counter wrap, partial upload failure,
allocation/thread/notification failures, fallback drains, disabling, and three
GPU-copy slots. Address/undefined and thread sanitizer runs passed. GCC cannot
instrument device fences; host semaphore/event synchronization and queue
release/acquire counters are instrumented. This does not validate GXM itself.

Hardware acceptance requires a fresh, hash-verified launch, ordinary gameplay,
unchanged settings and checks for failed jobs, rendering corruption and frame
time. Capture, worker and join log values are overlapping window totals, not
additive frame phases. Extra copies and synchronization can outweigh overlap;
retain the previous build for comparison and rollback.

## First hardware run

Runtime `f4fc1cd90db565ebd447f4a9acfe604b96f0e6bf7ab9162027da880eae697ae0`
booted in slot 1; the preceding range-capture build remains in slot 0. Package
comparison found only the game runtime and boot record changed. Native
960×544, triple buffering, decals, cosmetic effects, reflections and shadows
remained enabled. The built-in benchmark stayed off.

The main menu and ordinary solo Blood Gulch run completed 411,968 reported
preparation jobs across 113 reporting windows, with zero failed jobs. Camera
movement, walking and assault-rifle firing were exercised; sampled screenshots
showed the expected world, weapon and HUD. This is a short smoke test, not
campaign or long-session qualification.

One stationary gameplay window recorded 8,640 jobs over 60 frames: capture
287,073 µs, worker 397,554 µs, join 2,535 µs. That is about 4.8 ms of recording
copy work, 6.6 ms of overlapping preparation and 0.04 ms of joining per frame.
It proves queued work is active, not a frame-time saving: the earlier build was
observed at a different camera position with a different weapon. The user's
12 FPS valley report preceded installation of this trial.

Next: prove loaded BSP/model data lifetimes and writers before avoiding copies
of unchanged inputs. The private staging copy is now a measurable cost; simply
passing mutable guest pointers to the worker would undo the lifetime guarantee.
