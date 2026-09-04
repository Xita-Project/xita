# Xita Roadmap

Xita runs Halo: Combat Evolved's original Xbox executable on the PlayStation Vita. The
offline recompiler (`xita-recomp`, `xita_recomp.py`) lifts the x86 code to C; the runtime
(`xita`, `xita.vpk`) supplies the Xbox kernel, Direct3D and DirectSound surface on top of
the Vita's GXM, sceCtrl, sceAudio and sceIo. This document is the planning reference for
the remaining work. Phases are ordered by technical dependency: each phase relies on the
one before it being stable.

Status legend: `[x]` done, `[~]` in progress or unverified on hardware, `[ ]` not started.

**Priority (2026-09-04).** The goal is local multiplayer between Vitas, so the critical
path is Phase 1 → Phase 3. Phase 2 (the dashboard) is not a prerequisite for ad-hoc play
and can run in parallel or after it; Phase 4 (plugins and modding) is deferred until the
first three are done. Within Phase 1, the items that gate Phase 3 are frame pacing,
stable multiplayer maps and the 25 fps target; the campaign-completion and rendering
polish items can follow. Phase 3 also reuses the existing in-process server and client
path, so its first milestone (two Vitas in a lobby) can start as soon as pacing is in.

## Current state (2026-09-04)

- [x] Boots to the main menu on real hardware; profiles and saves work.
- [x] Blood Gulch (solo) playable on the Vita at 11 to 20 fps on foot: full canyon
      visibility, Warthog, all weapons, sky, fog, audio.
- [x] Pillar of Autumn through the intro cinematic and the cryo tutorial.
- [x] Bump lighting and reflections correct on terrain and models (GXM cube-map layout,
      8-texel linear texture padding).
- [x] Pad recorder and replay for reproducible runs; frame-time and per-phase timers;
      on-demand draw-call histograms.
- [x] Game clock honest: Halo derives frame time from the vblank count, and the runtime now
      delivers exactly 60 vblanks per real second (the earlier 1000 Hz counter made the game
      run fast in heavy scenes and 2x at 60 fps on the emulator).
- [~] Camera stays at the cutscene shot when the Keyes cutscene is skipped (reproduced from a
      recorded emulator run; fix in progress).
- [~] Touch zones for Black, White and the stick clicks, plus deadzone and look-sensitivity
      settings, merged; not yet verified on hardware.
- [~] Dashboard first milestone (software-rendered home screen, standalone VPK) and the
      ad-hoc networking test app exist as separate apps; neither has run on hardware yet.
- [~] Offscreen render-target passes are dropped rather than rendered.

## Phase 1 — Gameplay Core & Campaign Completion

Goal: the campaign and multiplayer maps play correctly end to end at a stable frame rate,
with input, physics, AI and audio behaving as on the Xbox. Everything later builds on
this: the dashboard launches it, ad-hoc play networks it, plugins hook into it.

### 1.1 Technical objectives

- **Frame pacing.** Implement a high-precision frame pacing loop (30 FPS / 60 FPS toggle)
  using `sceKernelGetProcessTimeWide`. Halo's simulation ticks at 30 Hz; the pacing loop
  must keep the tick cadence fixed while rendering runs at whatever the GPU sustains, and
  replace the current vblank-counter wait (`XV_VBLANK_HZ`) with a deterministic schedule.
- **Input mapping.** Finalize dual-analog acceleration curves, deadzone mapping, and
  sticky-reticle aim assist mechanics. Bind front/rear touch zones for contextual inputs
  (Black/White buttons, L3/R3 thumbstick clicks); today the D-pad stands in for those
  four while the player is in control.
- **Physics and AI execution.** Verify the recompiled physics and AI paths across the
  campaign: vehicle physics at low frame rates, AI pathing and encounters, scripted
  sequences (the cryo-tube exit camera is the first open case). Every divergence from the
  Xbox is a recompiler or HLE bug and is tracked as one.
- **Audio desync fixes (dsound).** The DirectSound HLE (voice creation, stream packets,
  `DSoundVoiceIsPlaying`, mixing) must report playback state truthfully so scripted
  dialogue, music transitions and cinematic timing stay in sync. Today "is playing"
  answers a constant, which is enough for Blood Gulch but not for campaign scripts that
  wait on sound completion.
- **Save data.** Serialize checkpoint save states and player profiles to
  `ux0:data/xita/saves/`. The game's own files currently land under `ux0:data/xita/save/`
  through the file HLE; the new layout is a deliberate change and needs a migration.
- **Performance.** 25 fps on foot as the first target, then 30. The order of work is
  dictated by the hardware profile (`XV_PROF=1`) and the frame timers: recompiler code
  quality (flat memory access instead of the per-page table, lazy-flag elision, native
  float for the x87 paths), then texture decode and GXM submit on the second core, then
  per-draw overhead in the D3D translation (constant snapshots, program lookup).

### 1.2 Rendering completion

Kept here because these are engine work, not frontend work:

- [ ] Implement single-pass tangent-space normal/bump mapping for terrain and models
      (replacing Halo's original multi-pass sequence: lightmap × bump, specular,
      base × detail with destination-colour blending).
- [ ] Add dynamic point lights (flashlight) and shadow projections.
- [ ] Optimize transparent particle systems (plasma bolts, smoke, explosions) using
      hardware alpha-to-coverage.
- [ ] Implement screen-space post-processing passes (shield flashes, scope overlays,
      night vision filters). Depends on real offscreen render targets.
- [ ] Alpha test (discard in the generated fragment programs driven by the NV2A
      alpha-test state).
- [ ] Compressed-texture mip chains on by default once verified on hardware.

### 1.3 Hardware-specific considerations

- **Memory.** The runtime runs in extended memory mode (`ATTRIBUTE2=12`, about 109 MB
  extra) and still hosts a 64 MB guest physical space, the recompiled code, the decoded
  texture pool and GXM buffers. Every new feature must budget its memory; the texture
  pool already purges under pressure.
- **CPU.** Single game thread on a 444 MHz Cortex-A9; the second core is free for decode
  and submit work. The GPU submit is a few milliseconds per frame, so the frame budget is
  spent almost entirely on the CPU.
- **GXM rules learned the hard way.** Cube maps reserve a full mip chain per face and
  align faces to 2 KB; linear textures pad rows to 8 texels; there is no fixed-function
  alpha test. New rendering work must respect these or reproduce the class of bugs fixed
  this week.
- **Input.** sceCtrl in wide analog mode; front and rear touch via sceTouch; no Black,
  White or stick-click buttons exist physically.

### 1.4 Milestones

1. Frame pacing loop merged; 30 FPS mode holds the tick cadence on hardware.
2. Input curves, deadzones, aim assist and touch bindings merged; a settings block in
   `xita.cfg` exposes them.
3. Audio: campaign scripts that wait on dialogue advance correctly through a10 and a30.
4. Every campaign level from a10 to d40 loads, plays and reaches its next level on
   hardware; every multiplayer map renders.
5. Saves and profiles under `ux0:data/xita/saves/`, with migration from the current
   layout.
6. 25 fps on foot in Blood Gulch and the a10 cryo bay on hardware.

## Phase 2 — Custom Xita Dashboard & Frontend UI

Goal: a native launcher that replaces "boot straight into the game" with a home screen,
settings and control remapping, styled after the original Xbox dashboard.

### 2.1 Technical objectives

- **Launcher.** A 3D green-matrix, original-Xbox-style front end rendered with GXM: the
  glowing green tubes and animated grid, and a menu of the installed **games** (Halo:
  Combat Evolved first, Halo 2 next, each with its own recompiled engine and data under
  `ux0:data/xita/<game>/`), their content (campaign, maps, saves), and hand-off into the
  selected engine without a process restart. The dashboard is Xita's multi-game home
  screen, not a Halo-only menu.
- **Settings overlays.** In-game and launcher overlays for the runtime's tunables:
  frame pacing mode, look sensitivity and curves, aim assist, texture options, the debug
  knobs that stay useful (frame-time overlay, profiler). These replace hand-editing
  `xita.cfg`.
- **Touch control remapping.** A visual remapper for front and rear touch zones and the
  D-pad substitutions, saved per profile.
- **Content discovery.** Scan `ux0:data/xita/` for the game image and maps, validate
  them, and report clearly when the user's own copy is incomplete. No game content ships
  with Xita.

### 2.2 Hardware-specific considerations

- The dashboard shares the GXM context and memory with the game; it must release its
  resources before the engine starts and re-acquire them on return.
- Overlays drawn during gameplay compete for the same frame budget; keep them to a few
  draws and no per-frame allocations.
- LiveArea and the Vita's own UI conventions (Circle/Cross confirm, touch scrolling) apply
  in the launcher even if the visual style is Xbox.

### 2.3 Milestones

1. Launcher boots to the home screen, lists content and starts the campaign.
2. Settings overlay reads and writes every documented tunable.
3. Touch remapper with per-profile persistence.
4. Return-to-dashboard from the in-game pause menu without a crash or leak.

## Phase 3 — Ad-Hoc Multiplayer & Co-Op

Goal: local versus and campaign co-op between Vitas over ad-hoc Wi-Fi, by mapping Halo's
System Link networking onto `sceNetAdhoc`.

### 3.1 Technical objectives

- **Transport mapping.** Halo's XNET and Winsock calls (the current loopback in
  `xk_net.c` serves the in-process server and client) map onto `sceNetAdhoc` sockets:
  discovery and matchmaking through `sceNetAdhocMatching`, game traffic through PDP/PTP
  sockets. Key-exchange and XNET address translation become ad-hoc MAC addressing.
- **Session flow.** System Link lobby, join and start over ad-hoc, with the existing
  force-start hack retired once real peers can join.
- **Campaign co-op.** Halo's Xbox build carries the co-op path for split-screen; System
  Link co-op is the same simulation with a second player over the transport. Determine
  what the 3925 build supports and where the runtime has to fill in.
- **Bandwidth and latency.** Halo assumes 100 Mbit LAN; ad-hoc Wi-Fi is far below that.
  Measure the per-tick packet budget and apply the game's own bandwidth settings.

### 3.2 Hardware-specific considerations

- `sceNetAdhoc` requires the ad-hoc mode initialisation sequence and a fixed local port
  range; power management can suspend Wi-Fi on the Vita and must be held off during a
  match.
- Two Vitas at 20 fps each will not stay in lockstep; the game tolerates jitter within its
  own prediction, but frame pacing (Phase 1) is a prerequisite for a playable session.
- Memory: the network path must not grow the guest heap; buffers live on the runtime side.

### 3.3 Milestones

1. Two Vitas discover each other and complete a System Link lobby over ad-hoc.
2. Blood Gulch slayer between two Vitas for a full match.
3. Campaign co-op through a10 between two Vitas.
4. Session recovery: a dropped peer returns the host to the lobby cleanly.

## Phase 4 — Plugin Architecture & Community Modding

Goal: let the community extend Xita without rebuilding it: runtime plugins with defined
hooks, and custom map content loaded from the user's data directory.

### 4.1 Technical objectives

- **Plugin hooks.** A stable C ABI for dynamic plugins loaded from `ux0:data/xita/plugins/`
  (Vita `suprx` modules), with hooks at the points the runtime already owns: HLE entry
  and exit, frame begin and end, draw submission, input polling, file open. A Lua
  binding on top for scripted mods, with a bounded interpreter budget per frame.
- **Custom map loading.** Load `.map` cache files from `ux0:data/xita/mods/` alongside
  the user's original maps, with tag validation against the 3925 tag layout, and expose
  them in the dashboard's content list.
- **Safety.** Plugins cannot touch game data outside the mods directory, cannot ship game
  content, and are sandboxed from the runtime's own state except through the hook API.

### 4.2 Hardware-specific considerations

- Dynamic modules on the Vita require the taiHEN/`sceKernelLoadStartModule` path and
  memory for each module; the plugin budget must be visible in the dashboard.
- Lua on the game thread costs frame time; the interpreter runs with a fixed instruction
  budget and yields.
- Custom maps built for the PC release differ from the Xbox cache layout; only Xbox-format
  maps are in scope until a converter exists.

### 4.3 Milestones

1. Plugin ABI documented; a sample plugin logs frame times through the hook API.
2. Lua scripting with the same hooks; a sample script adjusts input curves live.
3. A custom Xbox-format `.map` loads from `ux0:data/xita/mods/` and appears in the
   dashboard.
4. Plugin and mod management in the dashboard: enable, disable, reorder, report errors.
