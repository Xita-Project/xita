# September 7, 23:12: first frame-constant hardware capture

The installed frame-owned vertex-constant candidate ran on the Vita. On
September 8 the user clarified that this session included **driving, shooting
and looking around**. Treat it as mixed gameplay, not a stationary-camera run.
Its log
reports 937,344 direct constant-buffer draw bindings with zero ring fallbacks
or rejected bindings. No new or changed crash dump was found. This supports
continued testing; the log alone does not establish that the user repeated
rocket self-damage, death and respawn, or that the previous GPU fault is fixed.

The read-only USB collection is archived at
`/home/birchwoodgod/xita-backups/2026-09-07-231242-frame-constants-hardware`.
All 27 copied files (47,846,625 bytes) were SHA-256 verified. The card was
safely unmounted; this collection made no hardware file changes.

- Installed executable: `82b21c788717ed663d7cfb691e7b7f1985f16ac85720261322e7475c215d17d0`.
- Log, modified at 23:07:57 CDT: `ca491ee61e08c5a4009ae264c87783f5994eb885a195c63544c4a6f0dec6e11f`.
- Configuration: `01e811aae82e2d2ff468b0982f7c8bc24f1ca436b8c55721e743796d6e3a9643`, unchanged since the previous collection.

Startup confirms **640×360**, textures capped at 256, a 20 FPS cap, and
`frame-owned vertex constants: 1`. All three constant slots allocated
successfully. The 135 constant reports cover 8,100 uploads, including menu
and loading; their largest used prefix is 232 KiB of the 1,024 KiB capacity.
The final incomplete counter interval is not included in these totals.

## Performance and sampling limits

Blood Gulch mesh frames 1320–8099, after the first mixed loading window, cover
113 sixty-frame windows: 6,780 frames in approximately 616.14 seconds.
Aggregate throughput is **11.00 FPS**, median window **11.1 FPS**, with
window averages of **8.1–13.0 FPS**. Including the initial mixed window gives
10.93 FPS over 6,840 frames; that first window is 6.1 FPS. These ranges are
sixty-frame averages, not instantaneous extrema or 1% lows.

This is not a controlled benchmark. The user's clarification establishes that
the session included driving, shooting and looking around. The aggregate
remains 11.00 FPS across the selected logged intervals; it cannot isolate the
FPS of each activity or establish an improvement over the earlier session's
10.85 FPS without matching the route and workload.

Correction to the initial interpretation: 102 of 113 camera samples do repeat
the same rounded values, but that does not justify classifying the whole run
as stationary. The installed diagnostic derives these values from cached
vertex-shader matrix rows captured at the first depth-tested draw, logs them
only every 60 frames, and does not check their capture age when printing. The
capture condition does not validate which render pass supplied the matrix.
Controller samples and camera values also change elsewhere in the log. The
cause of the repeated values is not established; validate the camera sample's
freshness and render-pass identity before using it to classify play activity.
The separate benchmark view helper has a freshness check; these repeated
diagnostic values alone do not establish a bug in that helper.

| Measurement in the selected world-rendering intervals | Result |
| --- | ---: |
| Average full frame time | 90.88 ms |
| Draw preparation | 10.18 ms/frame |
| Vertex stream preparation, included above | 3.93 ms/frame |
| Render submission | 3.83 ms/frame |
| Display queue API elapsed time | 0.030 ms/frame |
| Visibility event waits | 23.44 ms/frame |
| GPU notification completion latency | 53.42 ms/frame, overlapping work |
| GXM draw calls | 146.10/frame average, 254 maximum |

Visibility measurements use their own report ranges, frames 1343–8064:
6,497 event waits, all reporting ready, over 6,721 frames. Average resume
delay after completion is 76 microseconds. These are elapsed wait measurements;
they overlap other threads and GPU activity. GPU completion latency is also
overlapping and must not be added to the other rows or subtracted from frame
time to derive CPU time. Concurrent logs can straddle report boundaries.

Core busy counters average approximately C0 4.9%, C1 16.7%, C2 59.2%; these
are system-wide sampled counters, not exclusive game function timings.
No selected gameplay frame exceeds 500 GXM draws. The whole session's maximum
is 401, including menu/loading. High draw-call count alone is not the evidence
for the next optimization in this capture.

There are no nonzero ordinary Finish windows, vertex-upload failures or
frame-fence errors. Maximum pending GPU frames remains **one**; three slots
exist but this candidate still uses the conservative single-flight mode.
Vertex upload high-water is 467/8,192 KiB for the whole session.

## Next step

Keep the frame-owned constants while checking whether the exact rocket/death
reproduction passed. The lack of a fresh dump alone does not close the crash.
The next performance candidate is the already investigated
[deferred exact flare-result read](flare-defer-20260907.md), applied on top of
this installed stage so the constant-buffer change is retained. It moves
useful CPU work before the result dependency; it does not fabricate visibility
or promise to recover the entire 23.44 ms wait. Validate the combined build,
then compare eager/deferred/eager at a fixed camera and unchanged resolution.
No new executable was built or installed during this log collection.
