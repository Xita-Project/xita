# Vertex preparation while the owner prepares materials

**Physical result:** the 16 KiB comparison increased whole-frame time by
4.394 ms in one native-resolution cryobay view. This worker remains disabled
by default. See the [hardware follow-up](hardware-render-preparation-20260915.md).

`XV_VERTEX_PREPARE=1` is a new opt-in experiment. For a draw with at least
64 KiB of source vertex spans, core 0 performs the existing exact comparisons
and owned vertex snapshots while the recording owner prepares that draw's
shader constants and textures. The owner joins before inspecting the results,
resuming guest execution, or publishing the command list. Small draws and
unavailable workers execute synchronously. This is additional work sharing
within draw preparation; it does not move the entire rendering callback or
establish a hardware frame-rate improvement.

The earlier campaign measurement attributed about 19 ms/frame to vertex
stream preparation after indexed validation. This includes comparisons and
snapshots. The new overlap can hide only the portion concurrent with material
preparation; dispatch and join overhead can erase that saving. Worker elapsed
time must not be added to the overlapping owner timings. `[vertex-prepare]`
reports worker batches, requested span bytes, worker execution, dispatch, and
remaining join time, maximum batch size and dispatch cutoff.
`XV_VERTEX_PREPARE_MIN_BYTES` accepts 1..33554432 bytes; its default is
65536. Lower values are useful for stress tests but can add more wakeup cost. `[draw-prep] streams` now includes descriptor setup and
the remaining join; it excludes the asynchronously executed work.

## Ownership

The recording owner resolves source addresses, lengths, strides and reference
masks. One batch borrows them until completion. The worker is the exclusive
vertex-upload producer during that interval; the owner only works on separate
material state and texture storage. That owner does not run another guest
fiber from the material preparation route. Mutable source memory is never
queued beyond the current draw's HLE return. Every draw retains its original
position and exact referenced vertex values; no geometry or effects are omitted.

The existing upload pools, current-frame version comparison, optional upload
copy worker, and per-slot GPU retirement remain in force. The new worker
issues a DSB on its own core before publishing CPU completion. The optional
second copy worker still has its independent GPU-copy ticket and barrier.
The recorder joins preparation before publishing a list; the pump waits for
queued copies before submitting its draws. Diagnostics read the owned cached
mirror only after the preparation join.

The old GPU range registration counters were unused: publication already
issues an unconditional barrier for uncached uploads. Removing those counters
avoids introducing a shared accounting race when preparation and texture
uploads overlap. The range-registration API remains; GPU publication and
pump-write barriers remain. These APIs do not accept cached guest memory.

Completion uses an atomic predicate with an event as a wake hint. An event
signal failure, timeout, or stale notification cannot return the borrowed
source early. Startup/dispatch failure takes the synchronous path. Shutdown
joins a pending batch and stops this worker before freeing upload storage.

## Validation so far

The host fixture uses the actual preparation worker, actual snapshot allocator
and actual optional upload-copy worker, backed by pthread implementations of
Vita synchronization calls. It exercises 600 slot generations, source rewrites,
same-draw reuse, sparse referenced records, independent material work during a
paused worker, allocation limits, all four worker startup failure points,
failed dispatch, and missed completion notifications. It passes plain,
ASan/UBSan and ThreadSanitizer runs. Host barriers are not hardware GPU tests.

The existing draw-state, shader cache, index-copy, frame retirement, UI
publication and upload-worker checks also pass using the current generated
shader headers from the private build stage. The source checkout's older
generated shader table is incompatible with the current renderer; that initial
host invocation failed at compile time and was repeated against the actual
build inputs. No shader assets were replaced to run these tests.

The native build succeeds and the updater package retains the existing asset
contract: only `game-a.self` and `boot-game.txt` change. Emulator and hardware
validation are recorded below as they become available. The setting defaults
off; physical comparison and representative driving/campaign checks remain
required. No hardware update or FPS gain is claimed here.

## Emulator checkpoint

The package boots through the original main menu and normal Split Screen
flow into Blood Gulch. With the default 64 KiB cutoff, the sampled view stayed
on the inline path. A second package adds size reporting and a configurable
cutoff; the isolated emulator uses 4096 bytes to stress real worker dispatch.
A captured 60-frame window completes 2460 worker batches and 6960 inline
batches, with a maximum batch of 53,200 bytes. The worker logs core 0 affinity.
These are completed preparation jobs; the earlier core 0/1 object workers
continue independently during the simulation phase.

Turning, walking against walls and one charged plasma shot have completed
with the worker active, with no STOP in the log examined at this checkpoint.
The Vita3K frame cap remains a functional test condition. The worker and join
times are emulator observations, not evidence of a physical speedup. Campaign
and sustained driving with this new preparation path are still outstanding.

Private candidate `vertex-prepare2` runtime SHA256:
`6951865c04923098d1a9ef3fb655e1dda79eec96137dc7f0cd4e89c76363c613`.
The package contract is unchanged. Captures, complete logs, build identity and
sanitizer output are retained outside Git under
`2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`.

## Controlled comparison

The authenticated remote command accepts
`benchmark OUTPUT --kind vertex-prepare --runs 1`. It holds resolution and the other graphics/CPU options fixed while
running 60 settling and 120 measured frames each with preparation on the owner,
on core 0, then on the owner again. The middle arm uses a 16 KiB batch cutoff;
this deliberately includes batches missed by the default 64 KiB cutoff without
dispatching every small draw. Source snapshots and GPU-copy semantics are the
same in every arm. Preparation timings overlap material work and must not be
added to whole-frame time.

Worker availability is checked by the recording owner before a comparison
starts. Phase changes drain outstanding frame work, and the original enabled
setting and configured cutoff return on completion, cancellation or loss of the
gameplay view. No saved setting is rewritten. A build without this worker, or a
worker that fails initialization, declines the comparison.

Production worker tests cover an initially disabled configuration as well as
restoring an enabled configuration with a different cutoff. The state machine
covers all three exit paths and failed availability; the real frame-boundary
code and remote HTTP/client tests include this mode. The modified worker passes
ASan/UBSan and ThreadSanitizer, including 600 upload-slot generations with both
actual worker implementations. The benchmark state machine passes ASan/UBSan.
The native package builds and retains the existing asset contract. These checks
establish a runnable comparison, not a measured improvement on physical Vita.


The earlier `vertex-prepare2` candidate has now also completed Warthog driver
entry, forward movement into the cliff, reversing, steering away and driving
along the field, followed by exit. Screenshots and changing world-camera
positions confirm vehicle movement. The final preserved emulator log contains
489 profiling windows, 861,017 completed worker batches and no STOP record.
This includes substantial stationary time and is not a sustained-driving FPS
measurement. The hardware crash remains unresolved; an emulator pass cannot
exclude it. The physical remote status request still timed out at this
checkpoint, and no physical update was performed.


Candidate `vertex-compare` (runtime SHA256
`812d667068ce5cf927593c561f2e10edf3da06914e8656e9d7261da1d0c5ef3d`)
completes the real remote comparison in Blood Gulch. Each measured off arm
reports zero worker batches; the on arm reports 1440 completed worker batches
per 60-frame window with the 16 KiB cutoff. All three camera checks match. The
saved 4 KiB emulator cutoff returns after completion and dispatch resumes.
The 20 FPS emulator cap binds every arm, so its approximately 19.97 FPS results
are only validation of phase switching, ownership and restoration. They do not
measure the benefit or cost on Vita. The existing object-worker options and
all other settings remain the same across these arms; the off arms isolate
this one change within that configuration, not an unmodified-engine baseline.


The same candidate subsequently completes two charged plasma shots in Blood
Gulch and returns through Leave Game to the original main menu. Pillar of
Autumn loads through the campaign menu into the cryo-room first-person view;
turning and walking work. A second real comparison completes there with matching
camera checks, zero worker batches in measured off windows, and 3120 completed
worker batches per 60-frame on window. The largest observed batch is 331,520
bytes. The 4 KiB configured cutoff and normal worker dispatch return afterward.
These are cryo-room functional checks; NPC combat and hardware performance are
still unverified for this candidate. No checkpoint-resume conclusion is drawn
from this load-level path.
