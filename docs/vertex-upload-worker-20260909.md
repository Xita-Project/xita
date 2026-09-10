# Parallel vertex upload candidate

This candidate moves the second vertex snapshot copy from the recording thread
to a persistent worker requesting CPU core 0. It is experimental and off by
default. The [September 10 USB update](hardware-20260910-vertex-worker.md)
installs it with the worker enabled for testing. No physical Vita speedup or
stable 20 FPS result is established.

## Why this work

The previous hardware log contains slow windows with no new texture decoding and
only small, serial geometry sorts. Those existing workers cannot help much in
such frames. Representative late windows spend 2.6–6.0 ms/frame in the complete
stream preparation category. This includes comparison, snapshot allocation and
both copies; it is **not** a measurement of the work moved by this candidate.

The guest still snapshots mutable Xbox vertex memory into its owned cached
mirror immediately. Previously it also copied that mirror into uncached GPU
memory before continuing each draw. The new worker performs that second copy
while the guest records subsequent commands. The render pump continues to own
all GXM calls on core 1. Guest fibers retain their existing serialized scheduler
and default affinity; observed hardware placement is often core 2, not a new
hard affinity assignment.

This adds recurring work for another core. It does not parallelize AI or physics,
and it cannot promise equal utilization or three saturated application cores.
Memory bandwidth and the remaining serialized work still limit the potential gain.

## Ownership and completion

- Each frame slot retains its cached mirror and uncached GPU allocation. Guest
  pointers are never handed to the worker. Mutating a source appends a new owned
  snapshot; existing exact comparisons and draw ordering are preserved.
- Appended snapshots are dispatched in batches of at least 64 KiB. Sealing the
  frame dispatches its remaining tail; tails under 4 KiB stay on the caller.
- The bounded queue holds at most 64 batches, including an executing batch. A
  failed startup/dispatch or full queue copies the disjoint range on the caller.
  Queue saturation adds no producer wait or unsafe dropped copy.
- The worker completes uncached stores with a DSB before publishing its ticket.
  It never accesses guest state, render caches, the flush queue or GXM.
- Before any scene submission, the pump waits only if that frame's final upload
  ticket is incomplete. The old GPU notification still owns the slot until GPU
  completion; an upload ticket never substitutes for a GPU fence.
- Reset joins any queued CPU copies before reusing a mirror. Shutdown finishes
  queued copies before freeing either allocation. Recording-time diagnostics read
  the immutable mirror; post-GPU integrity checks still examine GPU storage.
- The policy is latched per slot generation, so benchmark changes cannot strand
  a partly recorded frame. All normal state, texture, UI and index paths remain.

The worker uses one 32 KiB stack and a small descriptor queue. It reuses existing
vertex allocations. Resident-byte comparisons may avoid an input copy, but the
batch still copies intervening unchanged bytes to keep ranges contiguous.

## Enable and compare

Dashboard **Performance → Parallel vertex uploads** controls `XV_VERTEX_WORKER`:
`0` retains caller copies (default); `1` enables the worker. Close and relaunch
Xita after changing this option. The in-game panel remains the graphics panel.

For a controlled developer comparison, set `XV_BENCHMARK_VERTEX_WORKER=1` in
`ux0:data/xita/xita.cfg`, relaunch, then use **L + R + Square** in a loaded
first-person view. This selector takes precedence over older comparison selectors
such as `XV_BENCHMARK_VERTEX_REFERENCES`. It runs caller/worker/caller phases at the
configured resolution, queue mode, shader quality, clock request and frame cap.
Each phase settles for 60 frames and measures 120. Stand still in the same view;
AI and effects can still change the workload even when the camera is unchanged.
Completion, cancellation and loss of the view restore the configured worker mode.

Compare total frame times, stream preparation, worker throughput and completion
waits. A 20 FPS cap can hide an improvement above 20; it remains unchanged by the
comparison. Do not interpret emulator FPS as Vita throughput.

`[vertex-worker]` reports queued jobs, caller fallbacks, completed worker batches,
bytes, copy time and completion waits as overlapping window totals. Counters are
sampled while the worker runs; completion totals may cross reporting boundaries.
They cannot be summed with guest or pump times to reconstruct a frame. The
existing `[cpu-thread]` line identifies the worker and requested affinity.

## Validation

- Production worker and allocator pass pthread-backed tests with ASan/UBSan and
  ThreadSanitizer: three startup failures, failed dispatch, full queue, a delayed
  consumer, wrapped tickets, immediate guest-source mutation, three concurrent
  slots, 300 mixed generations, reset and shutdown.
- Existing snapshot/retirement tests pass, including 2,000 mixed slot generations,
  changed lengths/padding and referenced/unreferenced vertex mutations.
- The benchmark state machine preserves fixed settings and restores the selected
  override on completion, cancellation and lost view. It takes precedence over
  the older vertex-reference comparison without toggling that optimization.
- Dashboard persistence and graphics-panel tests pass with ASan/UBSan. Native
  build passes and resolves the worker entry points to strong implementations.
- The CPU preparation suite passes against the native stage's generated shader
  tables. The source-only worktree's old generated table is insufficient for that
  suite; no private shader table is added to tracked source for this change.
- An isolated Vita3K run loads the dashboard, Halo menu and `a10`, renders the
  cryo room/technician and responds to left/right camera movement. Selected
  campaign windows complete roughly 14 upload batches per frame with no queue
  fallbacks. The core number reported by the emulator is not hardware utilization.
- The emulator completes the 360p off/on/off comparison and restores the configured
  enabled mode; a second comparison cancels and restores successfully. All phases
  sit at the 20 FPS cap, so their 19.959–19.966 FPS readings establish functionality,
  not speedup. A requested histogram frame checks 542 draws after GPU completion
  with zero changed geometry buffers, exercising the cached diagnostic read path.

The tested executable SHA-256 is
`b4eeb211b06641ed53ddae8d6c9e5078b73bcbf4d30a33710e0e50645f169106`.
The VPK SHA-256 is
`e9950683561984d8efb209c518857cbf216cf0375b2628e601c6cb35bf1f199b`.
Only `eboot.bin` differs from the previous model/material candidate; the other
1,584 package entries are unchanged. No shader assets or game data need replacing.

Physical testing is the next gate before enabling this by default. Shadows and
reflections remain requested graphics follow-ups, deferred while workload sharing
is prioritized.
