# September 8: Blood Gulch hardware follow-up

The USB collection at 18:20 CDT contains a new gameplay log and eight new
screenshots. The installed executable matches the earlier verified rendering
and modular build (`3d7b1dad…` compressed USB SELF). No files on the Vita were
changed during this collection; the card was mounted read-only and unmounted.

Mesh frames 1140–10859 cover 162 sixty-frame gameplay windows after loading:
**9,720 frames over approximately 763.7 seconds**, averaging **12.73 FPS**.
The median window is **12.9 FPS**, with a **5.6–17.8 FPS** window range.
Thirty-four windows reach at least 15 FPS. Screenshot `2026-09-08-181455.png`
shows the hardware counter at **19 FPS** during shotgun gameplay. A screenshot
counter and a sixty-frame log window need not report the same value.

Startup confirms 640×360, texture cap 128, linear filtering, mip smoothing on,
original materials/effects, triple buffering off, extended compression off, and
a 20 FPS cap. The requested 500 MHz clock is rejected (`802B0000`); the effective
clock is **444 MHz**. The earlier ~11 FPS session used a texture cap of 256 and
different gameplay, so this is encouraging free-play feedback rather than a
controlled attribution of the gain to one code change.

The sampled windows average 158 recorded draws and 11.15 ms of draw preparation
per frame. The last heavy interval averages 517 draws and 28.91 ms of preparation,
with 7.4 FPS. That motivates further work on expensive preparation and scene
work; neither a faster menu nor ground-facing peaks establishes sustained 20 FPS.
Normal render-time records continue to report zero per-frame full-GPU finish
calls. GPU notification latency overlaps guest and submission work and must not
be added to those timings as independent time.

The new in-game graphics panel is a later local candidate. It did not produce
these results. Next: validate that panel and preserve the current hardware
settings for another comparable Blood Gulch run, then compare the existing
draw-scan and vertex-copy candidates under matching conditions.

Private raw collection and hashes:
`/home/birchwoodgod/xita-backups/2026-09-08-182005-hardware-followup/`.
