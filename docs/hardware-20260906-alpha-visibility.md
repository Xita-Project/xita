# Alpha specialization and visibility polling: hardware results

The new executable runs on Vita, but sustained 20 FPS remains unmet. Blood Gulch
records median 9.75 FPS across 44 world windows; Battle Creek records 8.8 FPS
across 22. The new submission timers identify EndScene as the main intermittent
submission stall. Final graphics-completion waits remain the largest measured
pump stage. These are elapsed CPU-side API timings, not GPU execution timers.

## Collection and identity

Read-only USB collection, September 6 at 12:21 CDT:
`/home/birchwoodgod/xita-backups/2026-09-06-122124-alpha-visibility-hardware/`.
All 65 copied files, totaling 489,051,941 bytes, were hash-verified. The installed
32,918,474-byte executable matches the tested candidate:
`f2f480ccf8e590f2e02bb77cecb0f0e87cb70faf65772903d40104a6b6a5c752`.
Configuration is byte-identical to the previous collection: 480p, textures 128,
mip smoothing off, filtering 1, rear touch off. Clocks are 444/222/222/166 MHz.
The two archived screenshots are unchanged from the earlier collection; there
are no new screenshots from this play session. Saves and rotated logs were
preserved. No device files were changed, and the card was safely unmounted.

The 804,937-byte current log has 93 matched frame, render-stage and submission
reports, each covering 60 frames. The last CPU timestamp is 540.776 seconds.
Map names come from map-file opens; `beavercreek.map` is the Battle Creek session.
World windows require the corresponding map and nonzero BSP draws. All selected
windows report active gameplay. Loading/menu windows with no BSP draws are
excluded, though transition windows can still include loading work. Profiles and
CPU samples are included only when bracketed by world windows of the same map.

## Gameplay measurements

Values are medians of 60-frame world windows unless otherwise labeled. Separate
stage medians must not be added to reconstruct the median total. Guest interval,
draw HLE and render-pump time overlap and are not exclusive costs.

| Measurement | Blood Gulch | Battle Creek |
| --- | ---: | ---: |
| World windows / frames | 44 / 2,640 | 22 / 1,320 |
| FPS | 9.75 | 8.8 |
| FPS range | 4.4–22.2 | 5.4–14.2 |
| Windows averaging at least 20 FPS | 1 | 0 |
| Draws per frame | 125 | 161 |
| Guest interval | 97.4 ms | 95.75 ms |
| Present-side wait | 6.9 ms | 1.2 ms |
| Draw HLE | 7.35 ms | 7.6 ms |
| Whole measured render pump | 66.357 ms | 73.855 ms |
| Submission, including API stalls | 5.176 ms | 9.404 ms |
| Final graphics-completion wait | 58.337 ms | 58.469 ms |
| Nearby C0 / C1 / C2 utilization | 7 / 21 / 59% | 9 / 21.5 / 45% |

The previous queued-pass Blood Gulch run had median 9.0 FPS, 130 draws/frame,
79.968 ms whole pump and 69.089 ms final completion. The new run is encouraging,
but route, view and action differences prevent attributing the change to these
optimizations or measuring their individual effects. There is no same-session
Battle Creek baseline. Instrumentation is enabled in both builds.

The 22.2-FPS window has 49 draws/frame and a 38.978-ms pump. Its ending camera
direction is almost straight down (`z=-0.99`); it does not demonstrate normal
gameplay at 20 FPS. Battle Creek's fastest window averages 14.2 FPS, with 75
draws/frame and a 49.527-ms pump. Camera direction is sampled at each window's
end, rather than on every frame.

## What the new instrumentation resolves

EndScene takes a median 2.381 ms/frame in Blood Gulch and 6.722 ms in Battle
Creek, reaching 25.930 and 45.070 ms respectively in individual 60-frame windows.
The 45.070-ms case is Battle Creek's 5.4-FPS window: submission totals 50.796 ms,
final completion waits 72.463 ms and the whole pump takes 123.358 ms. In another
Battle Creek window, EndScene costs 44.040 ms but final completion falls to
31.354 ms. Waiting can move between the two calls, so reducing one timer alone
would not establish a frame-time improvement.

Scene acquisition, uniform reservation and shader lookup are generally small:
median BeginScene is about 0.120 ms/frame on both maps, individual vertex/fragment
uniform reservation categories stay below 0.24 ms even at their measured maxima,
and median shader lookup is below 0.17 ms. One Blood Gulch window has 5.127 ms of
shader lookup, consistent with an occasional cost rather than the steady-state
dominant stage. Texture decoding has a median equivalent of zero ms/frame.

All 68 specialized fragment-program loads report `discard-used 0`. Both original
and specialized shader paths occur in the workload records. The most submitted
indices belong to shader keys `154066FD`, `A01D09CF` and `8ED40330` on both maps.
For example, Blood Gulch records 14.45 million indices under `154066FD`, with
only 1,504 of its 42,156 recorded draws taking the alpha-disabled variant. This
is a useful follow-up target, but index counts do not rank GPU cost. The report
contains only the top eight keys per window, making aggregates lower bounds.
Enabled alpha tests must retain correct cutout behavior.

The 100-microsecond cooperative delay is enabled, and every reported pending
poll is delayed. World-bracketed polling reports have median 43.52 reads per
guest frame in Blood Gulch and 92.68 in Battle Creek, including ready reads.
Mean printed visibility-function shares are 12.21% and 32.93%, compared with
20.63% in the previous Blood Gulch run. These are sampled wall-time shares that
include waiting; they cannot establish CPU cycles saved or a polling-rate
reduction against the old build, which lacked these counters. Nearby Blood
Gulch C2 utilization is lower than the preceding run's 71%, with the same
uncontrolled-workload limitation.

All 93 windows retain zero intermediate target Finish calls, 60 final Finish
calls and 60 display-queue calls. Stage/submission sums pass rounding checks.
No explicit render-target scene errors, specialized-shader failure messages or
work-table overflows appear. Device geometry-buffer capacity logging was not
enabled, so this collection does not establish zero geometry drops or visual
correctness.

## Next investigation

1. Investigate why EndScene blocks, including pass/target reuse and graphics
   resource capacity. The current targets use `scenesPerFrame=1`; this is a
   candidate to investigate, not an established cause. Compare whole-frame
   timing so a wait moved to final completion is not mistaken for saved work.
2. Attribute GPU cost of the high-volume shader/pass combinations and optimize
   their real shading/coverage cost while preserving alpha, lighting and depth.
   Current workload counts do not provide GPU timing.
3. Use the same repeatable route and settings for comparisons, with the existing
   shader/polling switches separating their effects. Confirm useful gains in an
   ordinary build with diagnostic instrumentation removed.

The current installed build is unchanged by this collection. The archive holds
the manifest, complete parsed windows, checks, analyzer and this report. See
[implementation and emulator validation](render-alpha-visibility-20260906.md).
