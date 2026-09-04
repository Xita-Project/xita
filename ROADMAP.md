# Xita Roadmap

Xita runs Halo: Combat Evolved's original Xbox executable on the PlayStation Vita: the
offline recompiler (`xita-recomp`, `xita_recomp.py`) lifts the x86 code to C, and the
runtime (`xita`, `xita.vpk`) supplies the Xbox kernel, D3D and DirectSound surface on top
of GXM. This document tracks the work that turns "it runs" into a finished port.

Status legend: `[x]` done, `[~]` in progress or unverified on hardware, `[ ]` not started.

## Phase 0 — Where it stands (2026-09-04)

- [x] Boots to the main menu on real hardware; profiles and saves work.
- [x] Blood Gulch (solo) playable on the Vita at 11 to 20 fps on foot: full canyon
      visibility, Warthog, all weapons, sky, fog, audio.
- [x] Pillar of Autumn through the intro cinematic and the cryo tutorial.
- [x] Bump lighting and reflections correct on terrain and models (GXM cube-map layout,
      8-texel linear texture padding).
- [x] Pad recorder and replay for reproducible runs; frame-time and per-phase timers.
- [~] Camera stays at the cutscene shot after the cryo-tube exit on hardware (walks and
      looks fine in a fresh emulator game; needs a recorded hardware run).
- [~] Offscreen render-target passes are dropped rather than rendered.

## Phase 1 — Core Gameplay & Engine Finalization

- [ ] Implement high-precision frame pacing loop (30 FPS/60 FPS toggle) using
      `sceKernelGetProcessTimeWide`.
- [ ] Finalize dual-analog acceleration curves, deadzone mapping, and sticky-reticle aim
      assist mechanics.
- [ ] Bind front/rear touch zones for contextual inputs (Black/White buttons, L3/R3
      thumbstick clicks). Today the D-pad doubles as those four while the player is in
      control (`xk_os_vita.c`).
- [ ] Serialize checkpoint save states and player profiles to `ux0:data/xita/saves/`.
      Today the game's own files land under `ux0:data/xita/save/` through the file HLE.
- [ ] Frame rate: 25 fps on foot as the first target. Work starts from the hardware
      profile (`XV_PROF=1`) and the frame timers: recompiler code quality (flat memory
      access, flag elision, native float), then texture decode and GPU submit on the
      second core, then per-draw overhead in the D3D translation.
- [ ] Resolve the cryo-tube exit camera on hardware.
- [ ] Multiplayer match start without the force-start hack.

## Phase 2 — Graphics & Rendering Polish

- [ ] Implement single-pass tangent-space normal/bump mapping for terrain and models.
      Halo's environment shader currently runs as its original multi-pass sequence
      (lightmap × bump, specular, base × detail with destination-colour blending).
- [ ] Add dynamic point lights (flashlight) and shadow projections.
- [ ] Optimize transparent particle systems (plasma bolts, smoke, explosions) using
      hardware alpha-to-coverage.
- [ ] Implement screen-space post-processing passes (shield flashes, scope overlays,
      night vision filters). Depends on real offscreen render targets (Phase 0 item).
- [ ] Alpha test (discard in the fragment programs driven by the NV2A alpha-test state).
- [ ] Compressed-texture mip chains on by default once verified on hardware
      (`XV_BC_MIPS=1` today).
- [ ] Verify reflection cube maps and the Warthog hood on hardware after the cube-map fix.

## Phase 3 — Content & Release

- [ ] Every campaign level loads and completes; every multiplayer map renders.
- [ ] Split-screen and system-link paths decided (kept, stubbed, or removed).
- [ ] Packaging: `xita.vpk` plus a documented "bring your own copy" setup; no game data is
      ever distributed.
