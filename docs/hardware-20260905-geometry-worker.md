# September 5: geometry worker confirmed on Vita

The user reported that core 2 still did most of the work after the 22:06 update.
At 22:14, USB was mounted read-only, current logs/config were copied to the PC,
and the installed executable was hashed. USB was then safely unmounted. No Vita
files were changed during this collection.

The installed SHA-256 remains
`6ad07ed12c73d5943cd32c038a547072821d025e5857a300238eaee3a9a3ed18`.
The current log was modified at 22:13:30 and contains the new geometry-worker
messages. This confirms both installation and execution of the updated path.

## What ran

- Core-0 geometry worker started at 144.764 seconds, logged core 0 and affinity
  0x00010000, and processed 163 two-way jobs across nine reporting windows.
- Those windows contain 480 native serial jobs and zero three-way jobs.
- Parallel jobs contain 122,611 triangles in total. Core-0 sorting took 19.97 ms
  across all reported windows; caller sorting took 19.24 ms and joining took
  15.49 ms. These are overlapping window totals, not per-frame costs or savings.
- No core-1 geometry helper was started. Its 2,048-triangle threshold was not
  reached in the observed calls. The existing core-1 render pump is separate.
- The texture worker also ran. This session used a 256-pixel texture limit,
  480-line rendering and mip smoothing enabled. Previous collected slow campaign
  windows used a 64-pixel limit and smoothing disabled, so the runs are not a
  controlled before/after performance comparison.

For 41 CPU samples timestamped 145–190 seconds, weighted by each sample's duration,
system-wide busy time averages C0 5.6%, C1 10.0%, C2 89.1%. The user's impression
of an uneven workload is correct. The helper receives too little sorting work
to materially increase core-0 utilization in this session.

The current log opens Blood Gulch. After the initial world-loading window, its
recorded world frame windows span approximately 5.9–12.5 fps, with draw HLE
10.9–24.4 ms/frame. The final 7.2 fps window includes a save reload and texture
purge and should not be treated as steady-state gameplay. Early 20–26 fps windows
have very few world draws and are not evidence of sustained gameplay at 20 fps.

## Conclusion

The native sort and core-0 helper work on hardware, but the helper work is a small
part of the frame. No overall FPS gain has been established. Serial sort timing,
guest-copy/index-expansion cost, and the remaining per-draw preparation stages
need separate measurement. Gameplay, AI and physics are still serialized; this
update did not distribute them across cores. Raising worker utilization alone is
not a performance target; reduce total frame time with matched scenes/settings.

Evidence, configuration, checksums and computed totals:
`/home/birchwoodgod/xita-backups/2026-09-05-221454-geometry-hardware/`.
