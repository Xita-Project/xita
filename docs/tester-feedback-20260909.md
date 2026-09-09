# Tester feedback and performance review — September 9

A tester playing through the campaign reports reaching roughly halfway through The Pillar of Autumn, a
camera jump when moving the stick away from center, a flashlight rendering
problem and one stretched shadow. They report roughly 5 FPS in a later scene.
The exact build, scene, settings and input configuration are not supplied.
These are open hardware reports, not a controlled comparison or confirmation
that the September 9 profile update is installed.

The user confirms this is ordinary campaign play. No benchmark is requested
from the tester: collect the normal session log, settings and approximate
locations/actions around each issue first.

## Performance findings

The suggestion to investigate rendering workload and serial dependencies is
supported by existing measurements. It does not establish that code generation
is fully optimized or that the GPU alone limits the frame.

| Observation | Interpretation |
| --- | --- |
| The older audit reports about 68–74 ms in final `sceGxmFinish`. | This is the **pre-change September 7 baseline**. Ordinary frames now use asynchronous notification retirement. Full drains remain for resource changes and exceptional cleanup. |
| The controlled 544p/360p/544p test measures 7.255 / 8.660 / 7.219 FPS. | Resolution-dependent rendering and visibility dependencies affect throughput. CPU preparation barely changes, and 360p still takes about 115.5 ms/frame. More than one bottleneck remains. |
| Lower texture quality has little visible effect for the tester. | Smaller textures need not reduce the costly work in that scene. This does not isolate shader cost, overdraw, geometry processing or bandwidth. |
| Looking away from Blood Gulch's valley improves FPS. | Visible objects, preparation, pixels and effect passes change together. This does not isolate polygon count. |

See the [original synchronization audit](frame-sync-vertex-audit-20260907.md),
[slot implementation and hardware recovery](slot-pipeline-20260907.md), and
[controlled resolution results](hardware-20260907-vertex-compare.md).
The controlled comparator run averaged about 235 GXM calls/frame. A different
[Blood Gulch session](hardware-20260908-evening.md) included a heavy window with
517 recorded guest draws and 28.91 ms of preparation per frame. These are
different counters and scenes; a fixed draw-count threshold cannot identify
which work is expensive.

The primary Vita log collected earlier on September 9 contains **23 complete
render-time windows**, all with `finish-calls 0/0/0`. It contains periodic vertex
comparison statistics, but no controlled benchmark phases. This is our earlier
collection, not the tester's log. Its SHA-256 is
`d215de06ae244a5798a0e9d9302faf3b8f6b31d5662de15c6348e1f5be90a85b`.
Zero ordinary Finish calls does not mean zero waiting: occupied slots, exact
visibility results, the selected queue policy and GXM backpressure still matter.

## Overlay and CPU interpretation

The blue/rightmost number measures **Present-path milliseconds**, not GPU
frequency or utilization. It covers frame finalization, publication, slot
acquisition and conditional maintenance. It does not time the asynchronous
GPU's entire execution. The amber interval includes guest work, HLE, draw
recording and waits outside that block. See [overlay units](development.md#overlay-units)
and the [measurement code](../runtime/xv_ui_gxm.c).

The collected startup log reports CPU **444 MHz** and GPU **222 MHz**, even
though 500 MHz CPU was requested. These are startup clock readings for our
device, not measurements of the tester's device or GPU utilization.

| Work | Current implementation |
| --- | --- |
| Guest game logic and draw recording | Kernel-backed fibers use a cooperative baton: one guest context executes at a time. The bootstrap requests core 0; guest fibers use default affinity, so effective placement must be checked. |
| GXM submission and completion polling | Dedicated render pump requests core 1 and runs alongside the guest. |
| Large texture conversions | A core-0 worker processes a disjoint range while the guest handles the other range. Small conversions stay serial. |
| Large geometry sorts | Core 0 assists lists of at least 512 entries; core 1 can also assist lists of at least 2,048. Only sorting is parallelized. |
| Audio and sampling helpers | Native worker threads may use the three application cores, separately from serialized guest simulation. |

Sources: [guest fibers and native workers](../recomp/kernel/xk_os_vita.c),
[render pump](../runtime/main.c), [texture worker](../runtime/xv_texture_worker.c)
and [geometry workers](../runtime/xv_geometry_worker.c).

Low helper-core activity can mean insufficient eligible work or a dependency
on the guest/GPU. More fibers do not make the simulation parallel by themselves.
The scheduler, handles and guest state rely on serialized access. Additional
jobs should take immutable inputs, produce separate outputs and join at a
defined consumer. AI and physics need a shared-state audit before concurrency.

## Next work

1. **Review the normal campaign log with a known build.** Correlate slow sections
   and visual bugs with the tester's locations/actions. Compare preparation,
   submission, slot waits, visibility waits and notification latency without
   adding overlapping times. If a specific hypothesis needs a controlled test,
   then use a fixed camera/settings comparison. Deferred-query and
   indexed-validation candidates already exist; identify the enabled test.
2. **Identify expensive passes and preparation.** Compare pass families and
   their draw, index/vertex and preparation counts. Test pixel/shader pressure
   with controlled changes while preserving transparent ordering and visibility
   results. Avoid adding a full GPU wait after each draw to measure it.
3. **Investigate existing model LODs.** Check which tags have usable lower-detail
   geometry and where Halo selects it. Earlier visual LOD transitions could
   reduce preparation and vertex work while retaining collision and gameplay.
   World BSP geometry may need a different approach; arbitrary triangle removal
   creates holes. No LOD option is implemented by this review.
4. **Expand measured independent CPU jobs.** Choose work large enough to repay
   dispatch, copying and joins. Keep code-generation improvements available
   when measurements justify them.

## Open rendering and input reports

| Report | Next reproduction |
| --- | --- |
| Camera jumps when leaving stick center | Record deadzone, response curve, sensitivity and any input plugin. Replay small stick deflections in both directions at a stable capped frame rate; compare raw stick, mapped Xbox input and camera movement. |
| Flashlight remains bugged | Record map/location, weapon and exactly what changes when toggled. Earlier pod-disappearance fixes passed emulator checks; this report does not establish which failure returned. |
| One stretched shadow | Capture the surface, light/weapon state and viewing direction. Distinguish an effect projection from the older geometry spike before applying a fix. |

Xita's radial deadzone rescales input outside the threshold rather than jumping
directly to the threshold magnitude. That source check does not clear the full
input-to-camera path. Low FPS can make movement coarse; higher FPS must not be
assumed to fix this report. Use the [log collection steps](development.md#sending-tester-logs)
to preserve evidence. No runtime or hardware settings change accompanies this review.
