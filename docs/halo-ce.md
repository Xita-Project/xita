# Halo: Combat Evolved (Xbox, build 3925) — level status

Updated September 8, 2026. Results span multiple development builds; the latest
per-game evidence is in [COMPATIBILITY.md](../COMPATIBILITY.md). Include the
specific build, map and device in reports. Historical loads below are not
fresh validation of every mission on the current candidate.

Ratings: **Playable** (start to finish, normal play), **Ingame** (loads and renders, known
problems listed), **Loads** (reaches the level, not played through), **Untested**,
**Broken** (does not reach gameplay).

## Menus and system

| Item | Vita | Vita3K | Notes |
| --- | --- | --- | --- |
| Main menu, profiles, settings | Playable | Playable | Intro movies are skipped (Bink is stubbed) |
| Saved games, checkpoints | Ingame | Ingame | In-game revert is user-confirmed. Menu resume passes a later emulator restart test; hardware resume remains unverified. |
| Multiplayer lobby, one Vita | Ingame | Ingame | Solo Split Screen now starts through the normal button; emulator confirmed. See [solo lobby fix](split-screen-20260905.md) for hardware status. |

## Campaign

| Level | Map | Vita | Vita3K | Notes |
| --- | --- | --- | --- | --- |
| The Pillar of Autumn | a10 | Ingame | Ingame | Intro cinematic and cryo tutorial play. After the cryo-tube exit the camera stays at the cutscene shot on hardware (open bug). Green tint after the intro fixed 2026-09-04. |
| Halo | a30 | Untested | Untested | |
| The Truth and Reconciliation | a50 | Untested | Untested | |
| The Silent Cartographer | b30 | Loads | Loads | Loaded once during testing; not played |
| Assault on the Control Room | b40 | Untested | Untested | |
| 343 Guilty Spark | c10 | Untested | Untested | |
| The Library | c20 | Untested | Untested | |
| Two Betrayals | c40 | Untested | Untested | |
| Keyes | d20 | Untested | Untested | |
| The Maw | d40 | Untested | Untested | |

## Multiplayer maps (solo, one Vita)

Choose **Multiplayer → Split Screen**, enlist a profile, select a map and game
variant, then press **Cross (A / Start Game)** at Enlisted Players. The normal
countdown launches a one-player match. System Link retains its original checks.

| Map | Vita | Vita3K | Notes |
| --- | --- | --- | --- |
| Blood Gulch | Ingame | Ingame | Latest sampled Vita session averages 11 FPS at 360p and includes driving/shooting/look input. Warthog, Ghost and Scorpion operation is user-confirmed. GPU crash and performance gates remain open. |
| Beaver Creek (Battle Creek) | Ingame | Ingame | Renders fully since the visibility fix; not played through |
| Boarding Action | Loads | Loads | Loaded once; not played |
| Hang 'Em High | Loads | Loads | Loaded once; not played |
| Chill Out | Untested | Untested | |
| Damnation | Untested | Untested | |
| Derelict (carousel) | Untested | Untested | |
| Longest | Untested | Untested | |
| Prisoner | Untested | Untested | |
| Rat Race | Untested | Untested | |
| Sidewinder | Untested | Untested | |
| Wizard | Untested | Untested | |
| Chiron TL-34 (putput) | Untested | Untested | |

## Known issues (all maps)

- Sustained 20 FPS on physical hardware is the first target; it is not met.
  CPU preparation and visibility waits remain optimization targets.
- Earlier driving and rocket/death tests crashed the GPU. The latest
  constant-buffer follow-up has no new dump; exact rocket/death reproduction
  still needs confirmation.
- Alpha testing and render-to-texture paths are implemented. The earlier claim
  that these were absent is obsolete. Residual effects, glass, HUD and geometry
  regressions still need broader scene-by-scene validation.
- Campaign AI, cinematic camera recovery and menu resume need further hardware
  testing. Improvements observed in one emulator scene do not close these bugs.
- Intro and attract movies are skipped.
- Only a single Vita can play; ad-hoc multiplayer between Vitas is the next milestone.

## How to report

Include the following in a report:

1. Build (the date in the VPK name) and whether it's a Vita, PS TV or Vita3K.
2. Map and how far you got.
3. What went wrong and steps to reproduce it. Review any shared diagnostic
   excerpt first: `xita.log` can contain game shader definitions and memory data.
   Do not attach raw dumps or game files to public reports.
