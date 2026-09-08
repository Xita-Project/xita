# Blood Gulch: first verified 480p hardware run

Evidence was collected read-only over USB into
`/home/birchwoodgod/xita-backups/2026-09-05-171207-bloodgulch-480p/`.
The 341-file backup includes the executable, log, config, all save/cache files and
the screenshot collection. USB was unmounted after collection.

The installed executable matches the earlier 480p deployment:
`5f2b5fdb5e464decba9e21ced302fb8b08f59168c352257807c6ad6bfe6d853d`.
The log confirms 848x480 rendering and successful solo multiplayer start (state
2 -> 3). Settings are Low textures (128), Point filtering and mip smoothing Off.
The user reports that 480p made a noticeable difference. This run does not contain
the later offscreen-rendering, audio-state or ad-hoc branch integrations.

Of 70 logged sixty-frame windows, 49 include BSP draws. These span approximately
267 seconds and 2,940 frames, including the first mixed loading/gameplay window:

| Measure | Result |
| --- | --- |
| Gameplay window FPS range | 4.6–18.1 |
| Aggregate throughput across selected windows | 11.01 fps |
| Mean game-side time | 88.8 ms/frame |
| Mean pump time | 10.3 ms/frame |
| Windows reaching 25 fps | 0 |

Restricting selection to at least ten BSP draws per frame gives 40 windows,
4.6–15.3 fps and 11.19 fps aggregate. Many earlier windows appear stationary
(nearly identical draw counts), around 12–13 fps. Later movement/effects include
both faster and slower sections. No histogram dump lines occur within these
selected windows. Game and pump phases overlap and must not be summed.
Neither this route nor the graphics settings match a controlled native-resolution
baseline, so the log does not establish a percentage improvement from resolution
alone. Game-side CPU work remains the larger measured cost.

The cached dashboard records 19.9 and 19.7 fps, with roughly 38–39 ms drawing,
9–10 ms copying and 2–3 ms waiting per frame. This is the first hardware measurement
of that change; the prior roughly 3 fps figure came from the user's observation.

Two newest screenshots are `hf/2026-09-05-170537.png` (lobby) and
`hc/2026-09-05-170823.png` (plasma pistol outdoors). The gameplay capture shows
sky, the plasma reticle and partially drawn HUD, while the runtime overlay reads
7 fps in that instant. Rendering problems remain; this is not a completed HUD or
effects validation.
