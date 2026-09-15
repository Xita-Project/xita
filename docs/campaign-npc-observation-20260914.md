# Campaign bridge observation — September 14

The user's report that visible NPCs lower frame rate is consistent with an
expensive bridge scene in the latest capture. It does not yet separate animation,
simulation, lighting and drawing costs. The installed runtime was
`7f33dee4f5df113b7d62a00b195acc859dd36ebf84a2a17f85c1ca7c9f514946`, with
indexed vertex checks enabled and the experimental shared snapshot worker off.
Rendering was 640 × 360; requested CPU 500 MHz read back as 444 MHz, with GPU
222 MHz. The ordinary upload and render workers were active.

Two roughly 50-second observations cover the bridge sequence. Screenshots show
different geometry and characters, and the director reports first-person control
unavailable. This is **not a controlled NPC-facing/away comparison**: a camera
input during the sequence cannot establish a fixed, otherwise identical scene.
The first reporting window in each interval is excluded because it straddles
the initial capture.

| Observation | Complete windows / frames | Approximate FPS | Draws/frame | Draw-HLE ms/frame |
| --- | ---: | ---: | ---: | ---: |
| First bridge view | 4 / 240 | 6.01 | 294.0 | 21.30 |
| Later bridge view | 5 / 300 | 6.04 | 288.2 | 22.02 |

FPS uses summed frame durations from the rounded log counters. These values
are observations, not an optimization result. The draw-HLE category overlaps
other preparation measurements and must not be added to them.

Representative complete first-view windows also show:

- No busy render-slot acquisition waits and no vertex-upload failures.
- About 71–72 ms notification-completion latency, overlapping guest/submission
  work; this is neither an additional CPU wait nor a GPU utilization percentage.
- Roughly 123,000 native matrix and 95,400 native quaternion calls per 60 frames,
  alongside about 7,200 accepted native object-basis calls. These are call
  counts, not measured arithmetic cost.
- Eight queued GPU-copy batches per frame; core-0 copy work totals about 61 ms
  over 60 frames. The geometry worker sorts four two-way lists per frame in
  these windows. These jobs are much smaller than the full frame workload.
- Nine to ten vblank callbacks per rendered frame, costing about 0.1 ms/frame
  together. The callback follows the real 60 Hz game clock; these counts do
  not justify reducing simulation frequency.

The next larger target remains object/scene preparation. A useful diagnostic
must rank pose construction and animation, light/spatial updates, and rendering
in a loaded first-person scene with stable camera checks. The existing
[pose ownership audit](guest-workload-audit-20260912.md#ownership-audit) and
[light-update audit](light-update-audit-20260914.md) describe parent-transform
dependencies and shared spatial-list writes that must remain ordered. A few
captured objects touching separate pages is insufficient to parallelize the
whole object loop.

Raw logs, screenshots and the interval-selection receipt remain in the private
`engine-restructure-20260914T2300Z` validation directory. No NPC behavior,
simulation rate or rendering has been changed by this observation.
