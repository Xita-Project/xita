# Native/360p hardware comparison, September 18

This follows the [packet timing capture](hardware-packet-timing-20260918.md)
on the same confirmed `0.2.0-perf.1` executable. Resolution was changed through
the real in-game graphics overlay, with no benchmark mode or restart. The log
confirms 640×360 allocation and application, then 960×544 restoration. Screenshots
show the changed image resolution and both saved settings. No other option was
edited; native resolution is restored and the overlay is closed.

## Matched outdoor view

The selected windows report the same camera position `75.98 -138.82 2.24`,
forward vector `0.74 -0.67 0.07`, and 144 draws/frame. Camera logs are rounded;
screenshots also show matching landmark placement. Each selected window has
60 valid packets, no failed/invalid records, a prior negative poll for every
packet, and no new texture decodes. Overlay and resolution-transition windows
are excluded. This is a stationary scene comparison, not heavy-gameplay acceptance.

| Resolution | Selected packet tickets | Frame rate | Submit mean | Completion from submit start |
| --- | --- | --- | --- | --- |
| Native before | 8161–8340 | 17.1–17.4 FPS | 3.37–3.80 ms | 55.14–56.91 ms lower; 56.66–58.94 ms upper |
| 360p | 9841–10020 | 17.2–17.4 FPS | 3.71–3.75 ms | 33.96–34.23 ms lower; 34.98–35.54 ms upper |
| Native restored | 10741–10860 | 16.5–16.8 FPS | 3.58–3.96 ms | 54.21–58.02 ms lower; 56.44–59.97 ms upper |

Ranges describe the per-window means, not individual frame percentiles. The
return arm is shorter because gameplay resumed; subsequent movement, firing
and vehicle views are excluded. The result does not imply identical CPU load
or pixel content across every frame.

Graphics completion visibly responds to resolution, but the roughly 58 ms
frame period does not fall with it. This rules out a simple fill-rate-only
explanation for this view. It also confirms that the resolution setting changes
the actual rendering path. Completion latency includes driver/dependency effects
and must not be renamed GPU execution time.

## CPU work that remains at 360p

A 60-frame window records these inclusive elapsed intervals:

- `FA920`: 1,212,306 us, or 20.205 ms/display frame.
- `BCB30`: 2,096,997 us, or 34.950 ms/display frame.
- Accepted object passes within FA920: 1,025,001 us across 103 passes;
  same-pass batches account for 1,013,447 us. Worker intervals overlap.
- Scene bucket 0: 1,065,308 us. Its model-preparation interval containing
  `5B760` accounts for 432,921 us, or 7.215 ms/display frame.
- Flare result waits: zero in this window.

These are inclusive wall times, not CPU self times or additive savings. Model
preparation, draw-HLE and vertex-worker timers overlap with the scene intervals.
Do not sum them. They identify the next restructuring targets: the object-pass
batch critical path and model preparation, while preserving simulation results,
ordered rendering and frame ownership.

The later Warthog capture is a separate workload: roughly 7 FPS, 253–254 object
passes per 60 display frames, versus 103 in the stationary window. This suggests
multiple update passes compound the cost as frames slow, but does not prove the
cause of the initial slowdown. Cutting simulation ticks or dropping object work
would change game behavior and is not a demonstrated fix.

Evidence: private `resolution-followup/` beside the earlier `valley-followup/`
capture. It contains native-before, 360p and native-restored logs and screenshots,
including overlay screenshots proving the selected setting. The later vehicle
screen is deliberately not labeled as the stationary native return view.
