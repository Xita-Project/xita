# September 5: HUD, effects and 480p trial

USB evidence is backed up at `/home/birchwoodgod/xita-backups/2026-09-05-163259-hud-cutscene/`.
The mounted volume was read-only during collection. The installed executable matched
the previous dashboard update, SHA-256 `b1832e3c5357789cf98636ede95249b7eef88814576601c29265054d2f79f1ce`.
The backup contains the log, config, all 53 save/cache files, seven new screenshots,
and the installed executable. The user identified the black screenshot as the opening
cinematic: radar and part of the shield draw over an otherwise black screen.

The user reports 9–11 fps with low textures. The config confirms `XV_TEX_MAXDIM=128`.
Selecting the log's 114 sixty-frame windows with at least ten BSP draws per frame gives
3.5–14.4 fps, 6.88 fps aggregate throughput. Those windows are from a varied play session;
this is not an A/B comparison against an earlier route. Mean recorded game time is
140.9 ms and render-pump time is 42.0 ms; overlapping phases must not be added together.

In-game Revert to Last Save works, per the user. The profile checkpoint still has a
zeroed first 16 KiB, while the cache checkpoint has a nonzero header and identical
remaining bytes. Main-menu resume remains a separate open issue. No save was repaired
or replaced. Geometry spikes now occur in the emulator too. Plasma/projectile effects,
flashlight rendering, cinematic black frames and HUD meter fills remain under investigation.

## Candidate changes

- The dashboard draws on a cached 2176 KiB CPU canvas, copies the finished frame to
  the inactive display buffer, and frees the canvas before game startup. Host raster
  timing improved from 1.945 to 0.634 ms/frame with identical pixels; emulator dashboard
  timing is about 60 fps. These are not hardware performance measurements.
- Graphics adds 64/128/256/512 texture caps, Game/Point/Linear filtering, Auto/Off mip
  smoothing and native/480p resolution. Defaults preserve the game's filtering.
- `XV_RENDER_HEIGHT=480` renders at 848x480 into two CDRAM surfaces and scales each
  completed frame to the native 960x544 display with a GPU quad. The dashboard remains
  native. This draws about 22% fewer scene pixels and requires about 3.5 MiB additional
  CDRAM for the two color surfaces. Hardware FPS impact is unmeasured. Native is 544.
- HUD quads use the game's combiner constants and blend factors. Recognition compares
  active combiner instructions: unused stage registers change after weapon/effect
  draws, so matching the old complete-state hashes made the crosshair disappear again.
  The HUD programs are embedded for an executable-only USB update.
- Material textures retain black RGB and alpha; the whitening fallback is confined
  to the legacy UI path with a separate cache identity. X1R5G5B5 decode forces opaque
  alpha. An existing compatible fragment shader is paired with vertex shader 17 for
  the observed `D8AABA3A` material instead of its simplified fallback.
- Solo multiplayer's L+R+Triangle shortcut now responds to each new press, and only
  calls the game-start executor in ready-lobby state 2. Pressing before a session
  exists previously consumed the shortcut for the entire process.
- Startup phase timing and map I/O timing (`XV_LOAD_PROFILE`, default on) measure
  loading; no loading-speed improvement is claimed. Set it to 0 to disable map I/O
  instrumentation when measuring final performance.

Native build, dashboard persistence/navigation tests, HUD recognition tests, shader
source/embedded fallback tests and texture cache/alpha tests pass. The USB candidate
also validates all three decompressed SELF segments against the native build. Transfer
and emulator validation records are kept in `480p-candidate/` inside the backup.

## Deployment and follow-up observations

The exact padded USB candidate boots and renders at 848x480 in Vita3K. The user
successfully started solo Blood Gulch using L+R+Triangle; the log records ready
session state 2 transitioning to 3, and a screenshot captures Warthog gameplay.
The user also confirmed the reticle and plant alpha cutouts in this emulator run.
HUD meter fills remain incomplete.

The executable and existing 223-byte config were updated in place over USB with
no size changes or new on-device files. Executable SHA-256:
`5f2b5fdb5e464decba9e21ced302fb8b08f59168c352257807c6ad6bfe6d853d`.
Config SHA-256:
`9ca9eb8042d34d9b524d4a967366920a2a5c1a2b0788fa905de41dd105f2c163`.
The config selects 480p, keeps Low textures (`XV_TEX_MAXDIM=128`), and enables
the solo multiplayer shortcut (`XV_FORCE_START=1`). Direct reads matched both
hashes before and after remounting read-only. All 593 other installed app files
and all 61 other backed-up files, including the 53 save/cache files, were unchanged.
The volume was then unmounted. Hardware launch and performance testing are pending.
The local `480p-candidate/usb-in-place.json` records the checks and timestamps.

The same emulator session shows a black Blood Gulch skybox. At the user's request,
`xita-shotgun-wall.png` captures the first-person shotgun appearing clipped against
a close wall. Both captures and the emulator log are preserved in the candidate
directory. Neither issue is fixed by this deployment. Comparison with the original
Xbox at the same position has not been performed.

Follow-up source/trace findings, not yet proven causes of the reported images:

- `SetViewport` stores the guest MinZ/MaxZ, but `sync_draw_state` does not forward
  them and mesh replay uses the scene's fixed depth range. The translated vertex
  shaders strip the Xbox viewport epilogue. Record the first-person/world depth
  ranges and check this mapping when investigating weapon clipping.
- Offscreen replay is implemented in `xv_d3d.c`, but the kernel's Clear,
  DrawVertices and DrawIndexedVertices hooks still suppress offscreen commands.
  The 17:00:43 draw trace includes eight indexed draw calls in four visits to
  target `03D05880`, with no mesh commands recorded for those visits. The replay
  also has a four-pass limit, while that frame visits nine offscreen targets.
  Restoring effects requires checking these passes and their ordering, rather
  than simply removing the guards.
- The live log contains unrecognized shader pairs that use simplified fallbacks.
  Their relationship to the black sky, smoke and bloom/glow still needs isolation.

At 17:04 the user reported missing first-person arms with the sniper and scope HUD
marks visible without zooming. `xita-sniper-unzoomed.png` and the corresponding
17:04:15 trace are backed up with the other emulator evidence. The scope-side
quads appear at guest x=140 and x=493, y=121..377; additional stage-1 quads use
the legacy UI fallback with PS `FFBC5A4D`. This does not establish the cause or
the intended original visibility. The same screenshot shows blue sky with an
opaque dark disc, so the earlier black-sky observation is view/state dependent.
