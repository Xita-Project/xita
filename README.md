# Xita

**A static recompiler and runtime for bringing original Xbox games to the PlayStation Vita.**

[Roadmap](ROADMAP.md) · [Compatibility](COMPATIBILITY.md) · [Build guide](docs/building.md) · [Windows guide](docs/windows.md) · [GPL license](LICENSE)

Xita translates an Xbox game's x86 executable into C, builds it as ARM code,
and supplies Xbox kernel, graphics and audio interfaces through Vita homebrew
libraries. Each title needs its own port and testing. **Halo: Combat Evolved
is the only game tested so far.**

## Current status

This is a development project, not a finished Halo port. Rendering has improved
substantially, but performance and GPU stability still need work. The first
target is **sustained 20 FPS on real hardware**; it has not been met.

| Area | Current position |
| --- | --- |
| Dashboard | Launch Game and settings; triple buffering and About / License installed September 8, hardware testing pending |
| Blood Gulch | Latest sampled hardware session: about **11 FPS at 640×360**, including driving, shooting and looking around |
| Campaign | The Pillar of Autumn reaches gameplay; camera recovery, AI, resume and completion need further hardware testing |
| Rendering | Major improvements to loading, lighting, sky, decals, foliage and active camouflage; remaining regressions are tracked |
| Stability | Earlier driving and rocket/death tests crashed the GPU; one follow-up with the constant-buffer candidate produced no new dump |
| Multiplayer | Solo matches through Split Screen work; networking between Vitas is unverified |
| Other games | Future work; installing another XBE does not make it supported |

See [Compatibility](COMPATIBILITY.md) and the
[latest hardware report](docs/hardware-20260907-frame-constants.md) for test
limits. Emulator FPS does not predict Vita performance.

The [September 8 USB update](docs/hardware-20260908-weapon-menu.md) installs
weapon, lobby and audio corrections plus the deferred visibility comparison.
Installation is verified; gameplay and performance results for this build are pending.

## Getting started

You need a homebrew-enabled Vita, VitaShell, your own supported Xbox game copy,
and a computer with VitaSDK and Python. Windows users can build through WSL2.

1. Follow the [build guide](docs/building.md), or start with [Windows setup](docs/windows.md).
2. Install your locally built VPK and copy the required game files to the Vita.
3. Open Xita, adjust settings, then select **Launch Game**.

**Release preparation is in progress.** The development checkout contains
game-derived shader inputs that are excluded from the source review export.
A clean-source Halo build still needs the regeneration and provenance work in
[the release audit](docs/release-audit.md). A locally built Halo VPK contains
translated game code; do not assume it is redistributable.

## Dashboard and controls

Graphics includes textures, filtering, mip smoothing, resolution, materials,
glow, particles, decals, a frame limit, compressed textures and experimental
**triple buffering**. Triple buffering defaults off and applies on launch.
It may increase input delay and is not a guaranteed FPS boost.

Use **Up/Down** to navigate, **Cross** to select, **Left/Right** to change a
setting, and **Circle** to go back. Changes save automatically. **About / License**
includes the full GPL for offline reading. See the [dashboard guide](dashboard/README.md).

| Vita control | Halo action |
| --- | --- |
| Left / right stick | Move / look |
| Cross / Circle | Jump / melee |
| Square / Triangle | Action or reload / switch weapon |
| L / R | Grenade / fire |
| Start | Pause |
| D-pad down / up | Crouch / zoom |
| D-pad right / left | Flashlight / switch grenade |

Rear touch shortcuts default off. Profiling controls are in [Developer notes](docs/development.md).

## Roadmap

The full plan is **[ROADMAP.md](ROADMAP.md)**. Near-term work focuses on GPU
stability, visibility waits and draw preparation. A future **game selector**
will discover separately recompiled, supported titles and launch each with its
own settings and saves. Halo 2 is an untested future target, not a promised port.

## Reporting problems

Include the build, Vita or Vita3K, map, graphics settings, actions taken and FPS.
Review logs before sharing: diagnostics can contain game shader definitions,
memory data and local paths. Do not post executables, maps, firmware, SDK files
or unfiltered crash dumps. See [Compatibility](COMPATIBILITY.md#reporting-a-test).

## Contributing and license

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
PRs need a focused change, relevant validation and clear source/license provenance.

Xita's original code and documentation are **GPL-3.0-only**. See [LICENSE](LICENSE),
[NOTICE](NOTICE) and [third-party notices](THIRD_PARTY.md). This does not grant
rights to game content or proprietary platform components.

Development uses AI-assisted tools, including Claude Code and Codex, with the
author directing the work and testing on Vita and Vita3K. Contributions still
need review, tests and clear provenance.

Xita is independent of Microsoft, Xbox, Bungie, Halo Studios and Sony. Game and
platform names identify compatibility; trademarks belong to their owners.
