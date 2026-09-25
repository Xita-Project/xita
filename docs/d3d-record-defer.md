# D3D draw recording on a worker thread (`XV_REC_DEFER`)

Sept 24 2026. Branch `work/d3d-defer-20260924` (worktree `d3d-defer-wt`, base `19c3d9e`). Not pushed. Earlier rounds:
[d3d-record-speed.md](d3d-record-speed.md), [d3d-record-speed2.md](d3d-record-speed2.md).

## Summary

- **The change.** `XV_REC_DEFER=2` takes the draw recording off the scene helper.
  - The helper queues each `DrawIndexedVertices` / `DrawVertices` with the values the recorder would read at that
    instant.
  - A worker thread on core 0 runs the existing record path on them, in order, into the same command list.
  - State setters stay on the helper as before.
  - Every other recorder entry drains the worker first and then runs inline. Those entries are clear, render target,
    visibility begin/end, immediate-mode End and present.
- **Verify.** `XV_REC_DEFER=1` records inline, as in 0, and that list is rendered. It also queues every draw. At each
  drain point the worker replays the queue into a separate shadow list and shadow pools, with its own recorder-state
  timeline. It then compares every command with the inline one, byte for byte:
  - every field
  - the index values
  - the constant window
  - the attribute snapshot
  - the texture control words
  - after the capture drain, the vertex bytes each stream points at
- **Equivalence.** 0 mismatches:
  - 89,640 frames on x86 (a30 cutscene and pod, a30 with a scripted walk, a10 cinematic): 22.4 M draws, 57.2 M checks
  - 34,020 frames of a30 on the Pi: 15.4 M draws, 42.3 M checks
  - the verify mode caught all 5 deliberately injected faults
  - a queue stress test, run under ThreadSanitizer too
- **Pi 4, paired A/B, a30 pod, final build.** Scene helper **-10.0 +- 0.1 Mcycles/frame (-18.5%)**. The worker costs
  +8.2 Mcycles/frame. On the Pi, where 2 cores run everything, the frame changes by -1.3 +- 0.6 ms.
- **Vita estimate** (not measured): **9-12 ms/frame off the scene helper in the a30 pod.** perf207s: helper about 63
  ms CPU, frame about 70 ms. The worker should add **12-14 ms/frame on core 0** (51% busy now, about 70% after).

## How it works

The code is in `runtime/xv_rec_defer.h`, included at the end of `runtime/xv_d3d.c` because it uses the recorder's
statics, and in `runtime/xv_rec_queue.h`.

**The helper's part** (`xv_rec_defer_draw`, called from `xd3d_r_draw` after the guest-side checks):

- **Kernel-side work stays on the helper.** It does everything that reads or writes the kernel's D3D state or guest
  memory the inline path reads at call time:
  - the vertex program handle lookup
  - `xd3d_ps_sync`
  - the texture-state table (guest memory)
  - the index buffer header
  - the dirty constant range, which it consumes as the inline `SetTrackedConstants` would
- **What goes into the queue record:**
  - the render states `SyncDrawState` consumes
  - the combiner constants and the vertex attributes, only when they changed since the last record. New generation
    counters in `xd3d.c` track this.
  - the dirty constant rows
  - the fog colour and the alpha test
  - the page table the helper translates through: the render view's scene table
  - the index list, when it is outside the map's tag data. D3D copies indices into the push buffer, so the guest may
    rewrite them right after the call.
- **Tag-resident index lists are read in place.** They are 79% of the indices in the pod. The record carries the
  capture system's tag generation, and the worker checks it: a file read into the tag data in between would show up
  as an error. It never did.

**The worker's part** (`rd_apply_draw`):

- **Apply the record.** It binds the record's page table (TPIDRURW on the Vita). It then applies the same sequence
  `xd3d_r_draw` runs inline: `SetVertexShader`, `SetTrackedConstants` on its own copy of the kernel's constants,
  `SyncDrawState`, and the draw.
- **The record path is unchanged**, except that `record_draw_body` takes the fog colour, the alpha test and the index
  bytes from the call-time copy (`g_rec_ovr`). The guest pointer stays the index cache's identity. With `g_rec_ovr`
  NULL (modes 0 and 1), every read is the original one.

**XV_OCCL object tags.** `xk_occlusion.c` calls these for every object on the helper, and they are queued too. The helper
answers the object slot and the model matrix from its own mirrors. The worker recomputes both and compares them: an
`ERROR` line plus a session count in the report.

**Drain points:**

- every inline recorder entry (the list above)
- the render view's leave, before the scene table is unbound
- the scene's end on the helper, before the helper-side present

The visibility result readers (`xk_flare.c` calls them from any thread) stay lock-free and do not drain.

**The queue** (`xv_rec_queue.h`):

- one producer (the scene helper) and one consumer (the worker), variable-size records, release/acquire positions
- a sleeping consumer: event flags, with a state word so the producer only calls `SetEventFlag` when the worker sleeps
- no thread-local state (the Vita's `__thread` is emutls)

**The worker thread:**

- core 0, the helper's priority + 1: below the GXM pump, equal to the upload worker
- 1 MiB queue; the a30 pod's high water is 230-250 KiB. A full queue makes the helper wait.
- The worker's log lines are dropped on the Vita like the helper's (`xv_log.c`). Bounded error and mismatch detail
  lines go through `xv_log_criticalf`.

### What the worker reads later than the inline path

- **The reads.** Vertex buffer data and headers, texture headers, and texture and palette bytes. The worker reads
  them a few records after the call. The Xbox GPU also reads them after the draw call, a frame later.
- **Verify reads them even later**, at the next drain, so a change in between would show as a mismatch. None showed.

## Equivalence evidence

In-game verify (`XV_REC_DEFER=1` on top of the Vita play settings, `XV_OCCL=2`, lockstep), final code:

Frames are the run's reported frames, including the load. Draws are the queued draws, each one compared. Checks also
count the vertex streams and the object events.

| run | machine | scenario | frames | draws | checks | mismatches |
|---|---|---|---|---|---|---|
| `vx86-a30f` | x86 | a30: cutscene, then the pod | 29,880 | 13.43 M | 37.2 M | 0 |
| `vx86-a30w` | x86 | a30, scripted walk out of the pod and around | 29,880 | 1.67 M | 3.70 M | 0 |
| `vx86-a10f` | x86 | a10 cinematic (flares: 750 drains/frame) | 29,880 | 7.33 M | 16.3 M | 0 |
| `vpi2` | Pi 4 | a30 | 34,020 | 15.36 M | 42.3 M | 0 |
| `vpi1` | Pi 4 | a30, an earlier build | 13,260 | 5.83 M | 16.0 M | 0 |

The deferred mode (`XV_REC_DEFER=2`) ran with no crash, hang, object slot or matrix error, or tag change:

- a30, 29,880 frames on x86: 13.43 M draws
- a10, 26,880 frames on x86: 6.55 M draws
- three Pi A/B runs (`ab1`-`ab3`), alternating 0/2 every 120 frames, 16,900-21,800 frames each

Two early findings, both fixed before the runs above:

- **Visibility result readers.** They drained the worker. The flare code calls them from the owner while the helper
  records, so they raced the helper; now they do not drain.
- **Verify's shadow state.** The shadow replay started from the inline recorder's current state, which already held
  later draws. It now keeps its own state timeline.

Both had shown as a10 mismatches.

**Tests:**

- **Injected faults.** Five faults were each caught (3,100 to 672,000 mismatches per run):
  - a changed fog colour in some draws
  - combiner constants not resent
  - one constant row not copied
  - an object slot off by one
  - a `DrawVertices` start vertex off by one (caught by the vertex byte comparison)
- **Unit tests** (`make -C recomp/host test-rec`; `make rec-queue-tsan`): all pass.
  - `rec_queue_test`: one producer and one sleeping consumer.
    - ring sizes: 4 KiB rings (constant wraps and full waits) to 1 MiB
    - random record sizes, verify-only records, random drains
    - it checks the order, the bytes, that every drain is complete, and that no verify record is used without a drain
    - volume: 400 k records per configuration on x86, 150 k under ThreadSanitizer (no report), 200 k on the Pi
  - `rec_quad_test`, `rec_index_test`, `rec_hle_test`, `rec_query_test`: pass as before
  - The six tests that include `xv_d3d.c` build with `-DXV_REC_DEFER_OFF`. `draw_optimization_test` and
    `visibility_test` fail at the base too.
  - Run `test-rec` in a build tree. It needs the generated combiner table header, which a bare checkout does not have.
    `rec-queue-tsan` runs anywhere.
- **Cross-run comparison: not usable.** A per-frame content hash of the list, compared between runs at equal game
  ticks, differs even between two `XV_REC_DEFER=0` runs, with and without scene overlap. The harness is not
  deterministic run to run, so the in-process verify is the evidence.

## Measured on the Pi

Paired A/B: `XV_REC_AB=120 XV_REC_AB_KNOBS=XV_REC_DEFER`, the other knobs at the Vita play settings, frames after 1500.
Pi 4 cores 2-3. Another agent's job used cores 0-1 throughout, so the whole game ran on 2 cores.

| metric (per frame) | inline (0) | deferred (2) | paired change |
|---|---|---|---|
| scene helper Mcycles | 54.09 | 44.06 | **-10.02 +- 0.10 (-18.5%)** |
| scene helper Minstr | 25.37 | 22.91 | -2.46 +- 0.06 |
| scene helper CPU ms | 43.08 | 36.16 | -6.93 +- 0.09 |
| recording worker Mcycles / CPU ms | 0.46 / 2.6 | 8.62 / 8.5 | +8.16 +- 0.03 / +5.9 |
| owner Mcycles | 63.17 | 57.11 | -6.05 +- 0.37 |
| helper wall time inside the draw HLE (ms) | 19.48 | 9.84 | -9.63 +- 0.13 |
| frame (game) ms | 52.76 | 51.51 | -1.25 +- 0.57 |

- **Where the helper's saving came from:**
  - Non-guest cycles fell from 20.74 to 12.67 Mcycles/frame.
  - The new helper work is about 1.0 Mcycles/frame, plus part of `memcpy`:
    - `xv_rec_defer_draw` 0.39
    - `xv_rq_publish` 0.23
    - `xv_rq_reserve` 0.15
    - `xv_rq_notify` 0.09
    - the frame check 0.06
    - 0.09 that the symbolizer puts in `rd_start`
  - The kernel-side draw work that stays is about 0.5 Mcycles: `xd3d_ps_sync`, the texture states, the handle lookup
    and the HLE entry. So the helper keeps about 15-18% of the inline draw path's cycles (about 9.5-10 Mcycles).
  - Guest code also got 1.9 Mcycles faster, because the recorder's working set left the helper's caches.
- **The worker** does the recording in 86-91% of the cycles it took on the helper. The range depends on how much of
  the helper's remaining `memcpy` is queueing.
- **The worker's `busy` figure** in `[rec-defer]` is wall time. On the Pi it reads 13-14 ms/frame against 8.5 ms of
  CPU, because the worker shares 2 cores with everything else.
- **The frame gain is small on the Pi**, because 2 cores run the owner, the helper, the worker and the other workers.
  The Vita gives the helper its own core.
- **An intermediate build** (`ab2`, cores 0-1, 154 pairs): helper -7.6 +- 1.0, worker +5.8, frame -2.5 +- 2.0.

**Queue traffic in the pod:** 490 KiB/frame. Per frame, that is:

- 12,200 constant rows
- 192 combiner and 30 attribute changes
- 35,700 indices copied and 135,800 read in place

**Drains.** 36 per frame in the a30 pod: render targets, the HUD's End calls, present, the render view's leave and the
scene's end. About one per frame found the worker busy. On the Pi's two shared cores they cost 0.77 ms/frame of
helper wall time.

## Expected on the Vita (estimate)

perf207s, the a30 pod with `XV_OCCL=2` and `XV_REC_*=2`:

- helper 61-63 ms CPU/frame, scene wall 64-69 ms
- frame about 70 ms (14 fps)
- 264 draws/frame
- helper wall time inside the draw HLE (`draw-hle`): 14.6-15.9 ms/frame
- cores: C0 51%, C1 86%, C2 68%

Estimate:

- **Helper time kept per draw.** On the Pi the helper keeps 15-18% of the draw path's cycles. That is the queueing,
  plus the kernel-side work it always did. On the Vita that would leave about 2.5 ms of the 15 and save about 12 ms.
- **Costs the Pi does not model:**
  - one wake syscall per draw while the worker sleeps: about 250 x 2-4 us, 0.5-1 ms
  - drain waits: 0.3-1 ms
- **Helper saving: 9-12 ms/frame.** Add up to about 1 ms if the helper's guest code gains from the freed caches as it
  did on the Pi. That puts the scene wall at about 54-57 ms.
- **Worker on core 0: 12-14 ms/frame.**
  - The recording is about 14 of the 15 ms. At the Pi's 86-91% that gives 12-13 ms, plus its wakes.
  - Core 0 has about 35 ms/frame free, so at today's 70 ms frame it goes from 51% busy to about 70%.
  - The `busy` figure in `[rec-defer]` is wall time. The pump and the audio mixer preempt the worker, so it will read
    higher than the CPU time.
- **Frame: about 60 ms** if the helper stays the wall. The owner (about 48 ms of core 2) or the GPU may bind first.

## Knobs

These are runtime environment variables, read once. `env.txt` or the remote `env` command must set them before the
level.

| knob | default | meaning |
|---|---|---|
| `XV_REC_DEFER` | 0 | 0 inline (original), 1 verify, 2 deferred |
| `XV_REC_DEFER_RING_KIB` | 1024 | queue size (power of two); full = the helper waits |
| `XV_REC_DEFER_BATCH` | 1 | wake the worker every n records (more: fewer syscalls, longer drain waits) |
| `XV_REC_DEFER_SPIN_US` | 0 | the worker polls this long before sleeping (fewer wakes, uses core 0) |
| `XV_REC_DEFER_CORE` | 0 | worker core (0, 1, 2) |
| `XV_REC_DEFER_PRIO` | +1 | worker priority relative to the helper |
| `XV_REC_DEFER_TIMING` | 0 | 1: the report adds the helper's queueing time (two clock reads per draw) |
| `XV_REC_WORKER_TIMING` | 0 | 1: measure worker elapsed time using two clock reads per record (per batch in verify mode) |
| `XV_REC_AB_KNOBS` | | e.g. `XV_REC_DEFER`: `XV_REC_AB` alternates only this knob |

The report is printed by the owner every 60 frames. Below are the lines from the a30 pod on the Pi (`ab3`, mode 2)
and from `vx86-a30f` (mode 1):

```
[rec-defer] 60 frames: 27741 draws 41320 events queued (490.1 KiB/frame, high 230 KiB, 0 full waits), 0 inline draws;
  helper 0.00 ms/frame queueing; worker 13.36 ms/frame busy; drains 2160 (69 waited, 0.77 ms/frame); verify draws 0
  streams 0; per frame: 12169 constant rows, 1.0 full syncs, 192.3 combiner and 30.0 attribute sets, 35682 indices
  copied, 135793 read in place (tag data); errors (session): object slot/matrix 0, tag data rewritten 0
[rec-verify] XV_REC_DEFER mode 1: 60 frames 76282 checks 0 mismatches (session 37206714 checks 0 mismatches)
```

`helper ... queueing` stays 0 unless `XV_REC_DEFER_TIMING=1`. Worker time stays 0
unless `XV_REC_WORKER_TIMING=1`; the report explicitly labels disabled timing.
Zero in that case means unmeasured, not free work. Older builds measured worker
time unconditionally. These elapsed times include preemption, not just CPU work.
In verify mode, each mismatch also prints a detail line,
with a bounded count per frame, for example `[rec-verify] XV_REC_DEFER mismatch frame 13 draw 5 (inline cmd 6):
fog_color 0/1`.

## On hardware

1. **Verify.**
   - Set `XV_REC_DEFER=1` and play the a30 cutscene and pod, plus some a10.
   - Every `[rec-verify] XV_REC_DEFER mode 1` line must show 0 mismatches.
   - The `[rec-defer]` report must show `errors (session): object slot/matrix 0, tag data rewritten 0`.
   - Verify mode records inline and then replays at every drain, so it is slower than mode 0.
   - It allocates about 3.7 MB of cached memory for the shadow recorder.
2. **Measure.**
   - Set `XV_REC_AB=120 XV_REC_AB_KNOBS=XV_REC_DEFER` and run
     `python3 tools/rec_ab.py <log> --from <frame>` for the frame and `draw-hle` figures.
   - Or compare `XV_REC_DEFER=2` against 0 over two runs. The worker's `busy ms/frame` in `[rec-defer]` and the `[cpu]`
     line's C0 figure give the core-0 load.
   - Worth trying on hardware: `XV_REC_DEFER_BATCH=2`, and `XV_REC_DEFER_SPIN_US=20..50` to save wake syscalls.
3. **Use.** Set `XV_REC_DEFER=2`.

## Integrating into `overlap-candidate/build-x87`

I did not modify `build-x87`.

- **Files that match the base.** Its `runtime/xv_d3d.c`, `xv_ui_gxm.c`, `xv_log.c`, `xv_vertex_capture.c` and
  `recomp/kernel/xk_render_view.c` equal this branch's base.
- **Files that differ.** Its `recomp/kernel/xd3d.c` differs only in the order of the report calls (line 803). Its
  `recomp/kernel/xk_scene_thread.c` differs only in the host section.
- **Check done.** The patch below applies without conflicts to a copy of `build-x87`. I compiled the seven changed units
  with `build-x87`'s own Vita compile lines, taken from `make -n`. There were no errors, and the warnings were the same
  as without the patch (26 in `xv_d3d.c`, 21 in `xv_ui_gxm.c`, none in the other five).

```sh
WT=/home/birchwoodgod/xita-backups/2026-09-18-unified-games/d3d-defer-wt
cd $WT && git diff 19c3d9e work/d3d-defer-20260924 -- runtime/xv_d3d.c runtime/xv_ui_gxm.c runtime/xv_log.c \
    runtime/xv_vertex_capture.c runtime/xv_rec_defer.h runtime/xv_rec_queue.h recomp/kernel/xd3d.c \
    recomp/kernel/xk_render_view.c recomp/kernel/xk_scene_thread.c > /tmp/rec-defer.patch
cd /home/birchwoodgod/xita-backups/2026-09-18-unified-games/overlap-candidate/build-x87
git apply --check /tmp/rec-defer.patch && git apply /tmp/rec-defer.patch
```

No make variables and no change to `build-command.json`: `xv_rec_defer.h` is included by `xv_d3d.c`, and the new
entry points are weak-referenced from the kernel. Units to rebuild:

- `runtime/xv_d3d.c`, `xv_ui_gxm.c`, `xv_log.c`, `xv_vertex_capture.c`
- `recomp/kernel/xd3d.c`, `xk_render_view.c`, `xk_scene_thread.c`

No generated guest code changes. With `XV_REC_DEFER` unset the build behaves as before: no worker thread, no queue, the
original record path.

## Not done / limits

- **Nothing has run on hardware.** The Vita numbers are estimates.
- **Immediate-mode End, clears, render targets and visibility begin/end stay inline and drain the worker.**
  - In the a30 pod that is 36 drains per frame, and cheap.
  - The a10 cinematic's flares drain about 750 times per frame. The worker is usually idle then (on x86, about 20 of 44,800
    drains per 60 frames waited), but a10 gains less than a30, and I did not measure it on the Pi.
  - Queuing End (the HUD and the flares) needs the immediate vertices and the kernel state its paths read. That is
    the next step if flare scenes matter.
- **One wake syscall per draw while the worker sleeps.** `XV_REC_DEFER_BATCH` and `XV_REC_DEFER_SPIN_US` trade it
  against drain latency and core-0 time. They are untested on hardware.
- **The Pi had 2 cores for everything.** Its frame-time gain (-1.3 ms) says little about a Vita with a free core 0.
  The helper and worker cycle counts are the transferable numbers.

## Files

- **Vita:**
  - `runtime/xv_rec_defer.h` (new): the helper side, the worker, verify, the report
  - `runtime/xv_rec_queue.h` (new): the queue
  - `runtime/xv_d3d.c`: the call-time overrides in the record path, the occlusion entry split, the visibility
    begin/end drains, the include
  - `runtime/xv_ui_gxm.c`: the draw split and the inline-entry drains
  - `runtime/xv_log.c`: the worker's lines dropped like the helper's
  - `runtime/xv_vertex_capture.c`: the tag generation accessor
  - `recomp/kernel/xd3d.c`: the combiner and attribute generation counters
  - `recomp/kernel/xk_render_view.c`: the drain at leave and abandon
  - `recomp/kernel/xk_scene_thread.c`: the drain at the scene's end
- **Host only:**
  - `recomp/host/rec_queue_test.c`
  - `recomp/host/Makefile` (`test-rec`, `rec-queue-tsan`, `-DXV_REC_DEFER_OFF`)
  - `recomp/host/sampler.c`: the rec-worker role
  - `tools/rec_ab.py`: per-role windows
- **Logs, not committed,** in `d3d-defer-work/`:
  - `runs/vx86-a30f.log`, `vx86-a30w.log`, `vx86-a10f.log`, `dx86-a30f.log`, `dx86-a10.log`
  - `pi-runs/vpi2.log`, `ab3.{log,samples}`, `ab2.*`, `ab1.*`
