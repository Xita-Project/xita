# Compatibility

Per-level status on real hardware and on Vita3K, build `xita-2026-09-04`. Report your own
results in the pinned "Compatibility reports" issue with the build date, the map, and
whether you ran on a Vita, a PS TV or the emulator.

Ratings: **Playable** (start to finish, normal play), **Ingame** (loads and renders, known
problems listed), **Loads** (reaches the level, not played through), **Untested**,
**Broken** (does not reach gameplay).

## Menus and system

| Item | Vita | Vita3K | Notes |
| --- | --- | --- | --- |
| Main menu, profiles, settings | Playable | Playable | Intro movies are skipped (Bink is stubbed) |
| Saved games, checkpoints | Playable | Playable | Files under `ux0:data/xita/save/` |
| Multiplayer lobby, one Vita | Ingame | Ingame | Needs `XV_FORCE_START=1` to start with a single player |

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

| Map | Vita | Vita3K | Notes |
| --- | --- | --- | --- |
| Blood Gulch | Playable | Playable | 11 to 20 fps on foot, about 8 driving. Warthog, all weapons, sky, fog. Ghost and Scorpion presence unverified. |
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

- Frame rate is CPU-bound: 11 to 20 fps on foot in Blood Gulch on hardware. Target is 25.
- No alpha test yet, so cut-out textures (foliage, decals) show their full quad.
- Render-to-texture passes are skipped; surfaces that display them show stale content.
- Intro and attract movies are skipped.
- Only a single Vita can play; ad-hoc multiplayer between Vitas is the next milestone.

## How to report

Open a comment on the pinned issue with:

1. Build (the date in the VPK name) and whether it's a Vita, PS TV or Vita3K.
2. Map and how far you got.
3. What went wrong, with a screenshot if it's visual, and `ux0:data/xita/xita.log`
   attached (it contains no game data, only the runtime's own log).
