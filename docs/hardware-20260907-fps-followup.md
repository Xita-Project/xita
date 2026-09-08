# September 7, 22:19: Blood Gulch at 360p

The fresh USB collection is preserved at
`/home/birchwoodgod/xita-backups/2026-09-07-222251-fps-followup-hardware`.
All 27 copied files were hash verified. The executable is still the installed
September 7 vertex-comparison build (`4663b025…`), with single-flight submission.
The frame-owned constant candidate has **not** produced these measurements.

The user changed `XV_RENDER_HEIGHT` from 544 to 360. This is the sole config
change, and startup confirms **640×360**, upscaled to 960×544. Textures remain
256, with the other standard settings unchanged. No controlled benchmark was
run in this session; this is free gameplay, with different views and activity.

After the first mixed loading/gameplay window, mesh frames 1200–3419 cover
37 sixty-frame windows: 2,220 frames in approximately 204.7 seconds. The
aggregate is **10.85 FPS**, median window **11.2 FPS**, range **4.8–16.3 FPS**.
Six windows reach at least 15 FPS; 24 are between 9 and 14 FPS. These are
sixty-frame averages, not instantaneous peaks or a 1% low measurement. Including
the initial loading window gives 10.54 FPS over 2,280 frames. Menu and final
map-exit windows are excluded.

The 4.8 FPS interval is mesh 2100–2159. It records 219.9 GXM draws/frame,
20.2 ms/frame of measured draw preparation, and 7.24 ms/frame of submission.
Only one texture decoded in that entire interval (0.3 ms); this particular
slowdown is not a large texture-loading stall. Native math reports 174,745
matrix fast paths/9,698 page fallbacks and 101,164 quaternion fast paths/6,966
fallbacks over those 60 frames. These are work counts, not exclusive timing or
proof of the exact gameplay action. GPU notification latency is 82.53 ms/frame
and overlaps guest/submission work; it must not be subtracted from the full
frame time to derive CPU time.

No new or changed crash dump was present relative to the rocket-crash
collection. There were no vertex-upload failures or frame-fence errors;
maximum pending GPU frames remains one. Upload high-water is 654/8192 KiB;
maximum GXM draw count is 512 in a frame. This does not clear the prior crash.

The previous same-view 544/360/544 benchmark was 7.255/8.660/7.219 FPS: about
19.7% gain at 360p. The fresh moving-play average cannot be compared directly
with that fixed camera benchmark or credited to uninstalled optimizations.

Next: finish validating the candidate that moves mesh shader constants out of
the vertex ring, then retest rocket self-damage/death on hardware before asking
for more performance benchmarks. Preserve the user's current 360p setting for
that stability test; use matching settings and views for future comparisons.

At 22:33 CDT the frame-constant candidate was installed and USB verified,
preserving the 360p configuration. The FPS figures above remain measurements
of the earlier executable. Hardware retesting is pending.
