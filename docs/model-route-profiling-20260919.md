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

## Perf.27 hardware result

The updater verified runtime SHA-256
`6f6f93fcb224fdd60a7ec53d9c2d32f73a013cd125ca77a1c64d201ab3283385`
and booted `0.2.0-perf.27 / 852c695`. Only `game-a.self` and boot metadata
changed in the package. Compilation changed the two observer-owned objects plus
four existing version-bearing objects. No scheduling or rendering policy changed.

| Primary 5B760 interval | Initial checkpoint ms/frame | Crowded corridor ms/frame |
| --- | ---: | ---: |
| List generation | 0.079 | 0.297 |
| Secondary path | 2.088 | 2.141 |
| Per-model loop | 8.756 | 28.418 |
| Return tail | 0.003 | 0.003 |
| Enclosing early-model interval | 10.930 | 30.864 |
| Entire frame | 78.217 | 179.767 |

Each column uses six closed, valid 60-frame windows, with one primary call per
frame. These are different views, not an optimization A/B. The checkpoint camera
is `(-28.66,32.52,0.62)`, forward `(0.56,0.82,-0.15)`. The crowded camera is
`(-28.94,37.14,0.62)`, forward `(-1.00,0.01,-0.02)`; one reported z rounds to
0.61. The crowded screenshot shows multiple marines and enemies. The device was
already in that position when observed; no movement sequence was sent for this
capture. It also differs from the earlier perf.26 crowded position.

The loop accounts for approximately 92% of the crowded early-model interval.
Optimizing list construction or replacing 5B760 with a generic visibility test
would miss that expense. The first targeted candidate boundary remains the
shared per-model preparation under 5B4A0, not the outer loop itself.

## Existing cache inside that loop

Further inspection finds `5B4A0 -> 5AE10 -> 5ACB0 -> 5AA10` alongside the
`5B190 -> A26B0` packet route. `5ACB0` checks the object's field at offset 0x120,
validates an entry's object identity, and searches/selects an entry when needed.
Both the reuse and selected-entry paths invoke `5AA10` with different flags.
`5AA10` compares shared counters at 2FEB80/2FEB84 with entry fields and checks
object flags. Its branches conditionally invoke 93420 and 92120 and update
entry metadata. Thus calling 5AA10 does not prove a full rebuild took place.
Exact semantic names for every entry field and helper are not established.

Before adding a whole-object cache, distinguish time in this existing lookup/
refresh path from traversal and material packet construction. Do not bypass
cache refresh based only on unchanged world position, or assume every repeated
child call is duplicated work. The next child attribution must preserve normal
parallel object scheduling; historical serial phase traces are not matched
measurements for this runtime. Original extracted functions remain private.
