# Performance next steps — September 14

The strongest new campaign measurement is indexed vertex validation: three
cryo-room trials saved about **13.9 ms/frame**, taking the same scene from
**6.165 to 6.742 FPS**. That is useful progress, but the frame still takes about
148 ms. Neither sustained 20 FPS nor the newer 30 FPS target has been reached.
The [vertex validation report](vertex-references-20260908.md#september-14-hardware-follow-up)
contains the conditions, exact measurements and remaining gameplay checks.

## What the HL2 port suggests

The developer's [project history](https://www.youtube.com/post/UgkxnzngfpUkysMBzRZaRDywoWuuOfmcm9NH)
describes a native Source Engine adaptation, including a CPU/GPU synchronization
bug, excessive preallocation and an allocation-recycling bug. It places physics
adaptation after those earlier milestones. The [later development post](https://www.youtube.com/post/UgkxCTEOJOLco6eZQo8l5QH9fGUiA5BH6oJU)
reports distributing physics/entity work across three application cores and
large peak improvements in test scenes, alongside graphics and loading changes.

The attached photos show the game on a Vita. The posts do not provide an
implementation patch or a reproducible comparison with scene, clocks,
resolution and frame-time traces. They support studying subsystem adaptation
and parallel work; they do not determine Halo's attainable frame rate. A
reported 15-fold peak change also cannot be explained solely by dividing
identical work across three cores: other bottleneck fixes, workload changes or
measurement conditions must contribute. This is an inference, not a finding
about their unpublished implementation.

For Xita, the applicable lesson is to identify costly systems and give their
native replacements explicit data ownership. Replacing selected routines does
not require decompiling every game function first. Source access makes system
boundaries easier to inspect, but it does not eliminate synchronization work.
No code from that project's Source Engine tree was imported for this research.

## What the Vita currently shows

- The guest fibers cooperate one at a time. Their names do not represent
  parallel AI, physics and rendering execution. Corrected CPU diagnostics now
  report default-affinity threads and their real migration counters.
- The render pump is assigned to core 1; the existing vertex copy worker runs
  on core 0. Their low utilization means that much of the current critical work
  still occurs before those workers receive independent inputs.
- In the cryo-room baseline, vertex-stream preparation costs about 35 ms/frame.
  Indexed validation reduces that to about 19 ms, at an additional index-mask
  preparation cost of about 2.4 ms/frame. These are nearby diagnostic windows,
  not precisely aligned hardware counters for the measured arms.
- GPU-completion latency overlaps other work. It cannot all be counted as CPU
  idle time. The observed frame-slot acquisition counters showed no busy waits
  in the sampled campaign windows.
- Exact flare-query overlap helped two Blood Gulch views, had little effect in
  a valley view and did not help this campaign room. It stays disabled by
  default; see the [query results](flare-query-overlap-20260914.md).

## Implementation order

1. Validate indexed vertex checks during ordinary campaign and multiplayer
   play, including firing, driving and death/respawn. Run additional matched
   scene comparisons before changing the default. The benchmark restores the
   configured setting. On September 14 it was enabled through the new
   dashboard control for the current hardware validation session.
2. [Physical work sizing](vertex-work-profile-20260914.md#physical-diagnostic-result)
   now measures about 11.4 ms/frame in indexed comparisons and 7.0 ms/frame in
   initial snapshots in the cryo room. Continue to break down vertex-preparation cost by span size, cache hit or
   miss, index-mask construction and snapshot copy. Keep this instrumentation
   bounded and separate from the ordinary performance build.
3. The first [shared snapshot experiment](snapshot-worker-20260914.md) was
   slightly slower in physical comparisons and remains disabled. Its event
   notification follow-up also showed no pooled gain across three physical trials.
   Continue parallel preparation only
   for inputs whose lifetime is defined.
   `xv_index_copy.h` constructs coverage from the retained index chunks;
   `xv_vertex_upload.c` compares live guest bytes before creating or reusing a
   retained snapshot. Moving a live guest pointer to an asynchronous queue is
   insufficient: the guest can rewrite it as soon as the draw call returns.
   Compare worker designs against their input-copy, wakeup and join costs.
   A bounded parallel comparison must finish before guest mutation resumes;
   a deferred job must own its input. Preserve draw order and slot retirement.
4. Use representative captures to choose the next native game routines. Matrix,
   animation and visibility work are candidates only where measured cost
   warrants replacing the translated routine. Establish read/write boundaries
   before moving simulation or physics updates into simultaneous jobs.
   The latest [campaign observation](campaign-npc-observation-20260914.md),
   [pose job boundary](pose-job-boundary-20260914.md) and
   [light-update audit](light-update-audit-20260914.md) narrow this investigation.

The [presenting-thread affinity experiment](guest-affinity-experiment-20260914.md)
tests whether avoiding migrations helps the existing critical thread. It does
not add parallel execution. Three physical cryo-room trials measured 6.168
versus 6.175 FPS, with mixed individual results; ordinary scheduling remains
unchanged. Core utilization is a diagnostic; the acceptance
criteria remain shorter complete frame times and correct gameplay.

For worker changes, delayed-completion tests must cover queued copies, guest
rewrites, cancellation, slot wrap and GPU retirement. Follow with ARM validation,
emulator rendering checks and the same off/on/off hardware comparison. Reject
changes that merely transfer time into waits or add races.

Private evidence for this session is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z`.
The YouTube post text and attachments are retained there in `hl2-port-research`;
game assets, screenshots, logs and executable artifacts remain outside Git.
