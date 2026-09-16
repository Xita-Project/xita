# Hardware visibility boundary and resolution attribution

A physical Vita capture finds an existing scene end after every visibility-query
writer, with substantial recorded work remaining. A native/360p/native comparison
also confirms that resolution-sensitive rendering latency affects this view.
Neither result establishes an optimization gain or stable 20 FPS.

## Build and scene

- Source: `138c434dd2861e9a66610df3b2352c32075b74b5`.
- Runtime SHA256: `9910a0db1b33437270de5d69713937a55f70372b97a8906bf308af78bd6b52b0`.
- ELF SHA256: `22d5c0592b55ba669cd9b972465df599c5c3d209af1888691c26524c5eae9d04`.
- VPK SHA256: `02720a33b442801e510a2fdac4f2198b7c35f5c3a01586632cebaea3c0cc91e9`.
- Diagnostic flags: `XV_GPU_PACKET_TIMING=1`, `XV_VISIBILITY_PLACEMENT=1`.

The updater verified the runtime in slot B, retaining the existing slot A. Only
the executable and boot selection changed in the package; its asset contract
remained unchanged. The preceding runtime completed its checked logger drain
and updater handoff. No USB storage or emulator operation was involved.

The view faces Blood Gulch's red base at position
`(85.2646, -157.7749, 0.6005)`, direction `(0.93712, -0.34907, 0)`.
This is a different view from the earlier blue-base clipping comparison.
The initial rendering resolution is 960×544, with frame cap 0, texture limit
256 and glow/particle/material/model detail 2. Configured workers, private math,
lightweight locking and background reports remain enabled; clipping fusion and
the owner lighting census remain off. Effective clocks are CPU 444, bus 222,
GPU 222 and crossbar 166 MHz; the requested CPU 500 setting was unavailable.

## Existing boundary

After independently excluding the first potentially straddling report from
each series, the ordinary capture contains 660 supported placement packets:

- All 660 have a qualifying boundary at the first existing RTT scene end.
- Each has three issued slots and two potential writer records. The remaining
  issued slot must still receive its exact zero result.
- About 77.1 recorded draws, 15,137.6 indices and eight scenes remain after the
  boundary. The eight include seven later RTT ends and the final open scene.
- Unsupported metadata, RTT errors, missing buffers, abandoned slots and
  orphan completions are all zero in this capture.

These are command counts, not GPU time or successful-draw counts. The observer
neither adds a scene nor publishes a query result. The next experiment can put
an exact fragment notification at this existing boundary, while retaining all
geometry, UI, texture and query storage until the original final completion.
Later writers, multiple views, repeated query IDs and error paths still require
the whole-packet eligibility check and original fallback.

## Completion and CPU waits

The independent packet series also retains 660 valid packets, with no failed,
invalid or first-read-already-ready samples:

| Measurement | Per-packet mean |
| --- | ---: |
| CPU submission region | 4.040 ms |
| Final completion lower–upper bound, from submission begin | 80.080–80.503 ms |
| Remaining completion bound after submission returns | 76.040–76.463 ms |
| Observation uncertainty | 0.423 ms |

The largest retained individual uncertainty bracket is 24.586 ms. A tight mean
does not imply every observation was tight. The independent flare series waits
19.201 ms per frame at brightness barriers. Maximum pending packets is one;
the acquisition series records no busy-slot wait.

These bounds describe CPU visibility of the final notification. They include
driver and queue dependencies, scheduling and memory effects. They do not
measure pure GPU execution or its minimum sustained frame interval. Completion,
guest work, pump work and flare waits overlap and must not be added together.

## Same-view resolution check

One completed comparison holds the view and other graphics settings while
temporarily changing resolution. Each arm settles for 60 frames and measures
120 frames; all camera checks pass.

| Arm | Frame mean | FPS |
| --- | ---: | ---: |
| Native before | 86.587 ms | 11.549 |
| 640×360 plus upscale | 58.959 ms | 16.961 |
| Native after | 86.606 ms | 11.546 |

The native endpoints differ by 0.019 ms. The lower resolution removes about
27.64 ms from the measured frame interval in this diagnostic view. Native
resolution is restored afterward, and saved settings are unchanged.

Packet report boundaries differ from the benchmark's frame boundaries. Taking
only the second complete 60-packet report inside each measured arm avoids
claiming an exact mapping of all 120 benchmark frames:

| Retained subset | Native before | 360p | Native after |
| --- | ---: | ---: | ---: |
| Completion lower–upper mean | 80.420–80.799 ms | 47.214–48.433 ms | 80.366–80.760 ms |
| CPU submission mean | 4.408 ms | 4.120 ms | 4.120 ms |
| Flare wait per frame, separate report series | 19.603 ms | 2.223 ms | 19.835 ms |

Draw-HLE stays near 7.6 ms; placement remains eligible in every retained packet.
This supports resolution-sensitive rendering cost on the critical path. It
does not distinguish shader arithmetic, fill, bandwidth, driver dependencies
or their scheduling effects. Live simulation continues throughout the test.

## Diagnostic cost and evidence limits

Placement scanning and aggregation average about 66.5 microseconds per packet.
Its six-line pump report averages 50.1 ms, including output and preemption.
That report is outside the guest background-writer batch and introduces
periodic work. Extra notification clock reads also perturb scheduling. The
next performance comparison should omit both observers, retaining only the
candidate's boundary and fallback counters.

No ordinary-build correction is inferred from these measurements. Stationary
diagnostics do not establish driving, campaign, explosive-death stability or
sustained 20 FPS.

Private evidence is under `physical-gpu-placement/` and
`agent-polygon-contract/gpu-placement-analysis/` in the September 14 engine
validation directory. The saved-data analyzer verifies package identity,
report partitions, interval arithmetic, conservative window selection and the
steady log's exact occurrence in the longer same-run log.

- Steady log SHA256: `1f72d34922d53808531c576e7bba4017fb5bf85d0ab3c4a27cab76a678f60abf`.
- Resolution log SHA256: `eaec3680e7c327a9e93705d57c23d7ef9bbedd2997d0008467d6b448ee937650`.

See the [packet timing definitions](gpu-packet-timing-20260916.md) and
[placement observer contract](visibility-placement-census-20260916.md).
