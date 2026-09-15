# Vertex preparation while the owner prepares materials

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
