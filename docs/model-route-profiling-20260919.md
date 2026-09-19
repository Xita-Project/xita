# Targeted model-route profiling — September 19

The first requested change subdivides the measured 24.96 ms `5B760` interval.
It uses the existing `XV_SCENE_BUCKET0_DETAIL=1` diagnostic gate and owner
admission policy. It does not enable serial phase tracing or alter worker policy.

| Sub-interval | Responsibility established from retained code |
| --- | --- |
| Entry through return from `5A7B0` | Builds model-entry list using two `52D50` calls; includes setup |
| Through return from `D8C40` | Secondary model path, including calls to `D6F70`, `5AE10`, `A26B0` |
| Through `5B7CA` | Iterates list through `5B4A0`; includes loop/branch overhead |
| Return tail | Register/stack restoration and observer cleanup |

`[model-route-detail]` reports four elapsed totals, completed calls, open state,
and invalid observations. Divide microseconds by reported frame count and 1000
for ms/frame. These are nested within early scene detail bucket 3. Do not add
them to that enclosing measurement. They include callbacks, drawing and waits;
they do not identify CPU self time or GPU service time.

The instrumentation uses five additional clock observations per normal call:
entry, three boundaries, and cleanup.
Reports reuse the existing shared scene timestamp. No per-model clock is added.
Original emitted instructions and callbacks are preserved byte for byte after
stripping observer blocks; emitted-body drift rejects generation. Rebinding the
presenting owner abandons the old model token. Disabled instrumentation performs
no clock reads. Only the primary entry is instrumented; interior roots are not.

## Qualification

`tools/tests/model_route_profile.c` exercises all four intervals, disabled mode,
recursive admission rejection, live report splitting, worker rejection, invalid
step indices, backwards clocks, and owner-generation rebinding. ASan/UBSan pass.
The existing bucket1 observer tests also pass enabled and disabled, preserving
scene accounting. The retained generated body passes exact strip/reconstruction
and changed-callback rejection. ARM object compilation is recorded privately in
`../model-route-profile/compile-result.json`. This is not hardware validation.

## Native replacement boundary

`5B760` is not established as a BSP traversal or a lightmap lookup. Its short
wrapper builds and walks model lists and calls other routines that publish
render state. Replacing it with a generic native spatial-visibility test would
omit required behavior. A native candidate must preserve these three child
boundaries, descriptor writes, live list reads and guest preemption. Select the
expensive child from the new timing before implementing its native replacement.
Previous register-local traversal prototypes and their negative instruction
results remain documented in `model-routing-audit-20260919.md`.

## Transform reuse and material order

Perf.26 already caches exact matrix-prefix products and reuses around 54% of
eligible batches in the crowded capture. It compares actual matrix inputs and
floating-point state, recomputes the final matrix to preserve scratch/register
side effects, and retains ownership guards. Stationary object position alone
cannot justify reuse: animation can change the skeletal palette independently.
The cache has not established a whole-frame speed improvement.

Both early and late passes read the model list at `2D1FDC`, but use different
descriptors and shared render state. A model can have multiple materials and
ordered subpasses. Sorting the whole list by a single material handle is not
currently justified. A sorting candidate needs a prepared packet boundary with
proven opaque, order-independent draws, explicit pass barriers, and transparent
order retained. The 36.8 ms draw-wrapper total does not isolate state rebinding
cost; it also contains capture, uploads and waits. No sorting is enabled here.

## Game-update concurrency audit

`xk_object_jobs.c` already creates `xv_objects_c0` and `xv_objects_c1` with
`sceKernelCreateThread`, explicitly pinned to user cores 0 and 1. Adding another
Core 1 thread does not create extra CPU capacity. `xk_audio.c` already runs its
mixer through `xk_os_audio_thread_start`. Guest sound-state updates and callbacks
are distinct from that mixer and retain audited owner service boundaries.

In the final crowded 60-frame window, the two object lanes record about
1.026/1.138 seconds of lock waits. These overlap and are not additive frame
savings. Symbol lookup against the exact perf.26 ELF maps major sampled wait
sites to `clip_fp_only`, `f_0004CD60`, and `f_00096960`. The next concurrency
candidate is reducing the shared region around these existing worker tasks,
after checking private inputs, outputs and callback ownership. Whole AI or
particle ticks are not proven independent by these observations. No thread or
lock policy changes are made in this profiling patch.

## Requested double-buffered worker follow-up

The installed VitaSDK `psp2/kernel/threadmgr/thread.h` uses priority `0x10000100`
and native stack size `0x10000` in its `sceKernelCreateThread` example. The
priority is therefore not an invented value; suitability still depends on the
existing scheduler priorities and the tasks admitted. Use the named Core 1 mask
rather than an unexplained numeric affinity literal.

The current mixer already alternates two aligned output buffers, preserving a
submitted grain while preparing the next, and calls the blocking audio sink from
its own thread. `xk_os_audio_thread_start` currently uses priority 64, a 64 KB
native stack, and all user cores. That helper is also used by the profiler, so
changing it globally would affect more than sound. A dedicated mixer affinity
option should not silently pin the profiler or move guest DirectSound callbacks.

Object workers use a 512 KB native thread stack and a separate guest stack.
The logged approximately 99 KB peak is a **guest** stack probe against a 256 KB
guest allocation. It does not measure native stack consumption and does not by
itself prove that a 64 KB native stack overflows. A new native stack bound needs
its own evidence. Existing game ownership also places the main guest work on
Core 2, not the Core 0 owner assumed in the proposed queue API.

After the model timing result, the requested queue work must distinguish task
buffer reuse from world-state reuse. The producer may fill one task buffer while
the worker consumes the other, but the pointed-to inputs and outputs need owned
lifetimes through completion. `wait_idle` must join before dependent state is
published. AI perception, particles and script evaluation remain requested
routing targets, not demonstrated independent callbacks. First inspect their
writes and dependencies against the existing object-job boundary. Preserve
render submission ownership rather than relocating it based on a core number.
