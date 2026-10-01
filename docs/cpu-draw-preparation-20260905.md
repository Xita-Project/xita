# Draw preparation follow-up, September 5

The latest hardware run verified the geometry workers, but their sorting work
was too small to shift much load from core 2. See the
[hardware measurements](hardware-20260905-geometry-worker.md).

The next local candidate keeps those workers and changes indexed-draw recording.
Previously it copied the guest index list into uncached GPU scratch, then read
that scratch again to find the highest referenced vertex. It now snapshots up to
256 indices at a time in cached stack memory, finds the range there, and copies
those exact values to the existing GPU buffer. This removes the uncached readback
for guest index lists. It adds a small cached copy; hardware measurements must
establish the net benefit. ARM disassembly confirms the bounds loop reads the
stack snapshot, not the GPU destination.

The existing two-frame ownership, index order, alignment and overflow handling
remain in place. Static sequential indices need no scan; frame-owned generated
quad indices still use their existing bounds scan. No guest gameplay or GXM work
has been moved to another thread in this step.

`XV_INDEX_SCAN_CACHED=0` selects the old copy/readback algorithm for a same-build
comparison. `XV_DRAW_PROFILE=0` disables the new phase timers (enabled by default).
Every 60 frames, `[draw-prep]` reports setup, state synchronization, index
retention/bounds, program selection, stream setup, constant snapshots, textures
and diagnostics in milliseconds per frame. These are elapsed caller times and
include preemption and timer overhead, not CPU cycles or GPU timings. Rejected
draws can leave their final partial phase uncounted. The pre-change baseline's
bounds scan was in `streams`; compare `indices + streams` across that boundary.

Validation:

- `make -C recomp/host test-draw-prep test-frames` passes. The 192 copy cases cover
  chunk boundaries, full pools, unaligned input, 16-bit maxima and output guards.
  Actual draw-retention tests cover source overwrites, two frames, owned buffers
  and exhaustion with both algorithms. Timing tests cover accounting, reset,
  disabled profiling, unavailable clocks and a clock starting at zero.
- Copy, retention and timer tests also pass AddressSanitizer/UBSan.
- `make RECOMP=1 -j8` succeeds. The resulting executable boots and starts Blood
  Gulch through the normal solo Split Screen menu in Vita3K. A camera-turn trace
  checks 109 recorded draws with zero data changes before GPU completion. The
  inspected spin captures retain the world, sky, weapon and reticle.
- Baseline and candidate both run near the game's 30 fps limit in this emulator.
  They spawned at different positions and draw counts, so this is functional
  coverage, not a controlled speed comparison or a Vita FPS claim.

Candidate and evidence are backed up under
`/home/birchwoodgod/xita-backups/2026-09-05-223451-draw-prep-local/`.
Native executable SHA-256:
`5e9b4a9a76d6da639ef74b5f2a8270b1e1c5af1a88f229db5656b23017f5e6d0`.
This candidate has not yet been deployed to hardware. Keep texture size, 480p,
filtering and mip smoothing identical when collecting its hardware comparison.
