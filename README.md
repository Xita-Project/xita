<p align="center">
  <img src="docs/images/xita-logo.png" alt="Xita logo" width="128" height="128">
</p>

<h1 align="center">Xita</h1>

**A static recompiler and runtime for bringing original Xbox games to the PlayStation Vita.**

[Download VPK](https://github.com/Xita-Project/xita/releases/tag/v0.2.0-test.1) · [Install](docs/installing.md) · [Compatibility](COMPATIBILITY.md) · [Roadmap](ROADMAP.md) · [Contribute](CONTRIBUTING.md) · [GPL license](LICENSE)

Xita translates an Xbox game's x86 executable into C, builds it as ARM code,
and supplies Xbox kernel, graphics and audio interfaces through Vita homebrew
libraries. Each title needs its own port and testing. **Halo: Combat Evolved is in hardware gameplay testing. Halo 2 is an
experimental second profile; it is not yet validated on a physical Vita.**

## Getting started

1. **Download the VPK** from the [current Xita release](https://github.com/Xita-Project/xita/releases/tag/v0.2.0-test.1).
   Expand **Assets** and choose its single `.vpk` file. The **Source code** ZIP is for developers.
2. **Copy it over USB.** Open VitaShell's USB mode and copy the VPK to your Vita's
   storage. Windows users can use File Explorer.
3. **Install and add your game data.** Safely eject the drive, leave USB mode, and
   open the VPK in VitaShell. On first setup, copy your own supported Halo image
   and maps using the [installation guide](docs/installing.md).
4. **Launch Xita.** Choose **Select Game → Halo: Combat Evolved**, set your
   graphics options, then select **Launch Game**.
   Start a campaign or a solo match through Halo's **Split Screen** menu.

**Already have Xita working?** Keep your existing game files, settings and saves;
follow [Updating Xita](docs/installing.md#updating-xita). The current
[game release](https://github.com/Xita-Project/xita/releases/tag/v0.2.0-test.1)
contains one VPK, a checksum and build information. Older builds have their own
releases marked **Superseded**; the [ad hoc tester](https://github.com/Xita-Project/xita/releases/tag/adhoc-20260908b)
is a separate download. [Release guide](docs/releases.md). Releases remain private
during development.

**First installation?** You need a homebrew-enabled Vita, VitaShell and your own
supported original Xbox copy of Halo CE. The VPK does not include the game image
or maps. [Prepare your game data](docs/game-data.md) once, then use VPKs for app updates.

### Create halo_image.bin from your .xbe

`halo_image.bin` is generated from your own Xbox Halo CE **`default.xbe`**.
It is not a separate download, and renaming the `.xbe` will not work.
If Halo already runs on your Vita, keep the image you have and skip this step.

Install Python 3, then download [xbe_parse.py](recompiler/xbe_parse.py) and
[xbe_image.py](recompiler/xbe_image.py) with **Download raw file** on each file page.
Arrange the files as shown, then open a terminal in **`setup/`**:

```text
setup/                 ← run the commands here
├── xbe_parse.py
├── xbe_image.py
└── haloce/
    ├── default.xbe
    └── maps/
```

**Windows — Command Prompt or PowerShell:** run `dir` first to confirm that
both scripts and the `haloce` folder are listed.

```bat
py -3 xbe_parse.py haloce\default.xbe --json > game_manifest.json
py -3 xbe_image.py haloce\default.xbe game_manifest.json halo_image.bin
```

**Linux / macOS:**

```sh
python3 xbe_parse.py haloce/default.xbe --json > game_manifest.json
python3 xbe_image.py haloce/default.xbe game_manifest.json halo_image.bin
```

If your prompt already ends in `\haloce>` and the scripts are inside that
folder too, use [these commands instead](docs/game-data.md#file-not-found-while-creating-the-image).

After both commands succeed, **`halo_image.bin` appears beside the scripts**. Copy it to
**`ux0:data/xita/halo_image.bin`**, and copy your `haloce/` folder to
**`ux0:data/xita/haloce/`**. There should be only one `haloce` folder: the menu map
must be at `ux0:data/xita/haloce/maps/ui.map`. Your executable must match the
[supported Xbox revision](docs/game-data.md#supported-game-copy).

## Screenshots

[Explore per-game progress](https://xita.dev/games/) to inspect generated
functions and remaining instruction gaps. Developers can also generate the
[same interactive report locally](docs/game-progress.md).

Actual development captures; select an image to view it at full size.

| Xita dashboard · Vita3K · September 8 | Halo CE: Blood Gulch · Vita3K · September 8 |
| --- | --- |
| [![Xita dashboard with Launch Game and graphics settings](docs/images/dashboard-vita3k.png)](docs/images/dashboard-vita3k.png) | [![Halo CE gameplay at a Blood Gulch base](docs/images/blood-gulch-vita3k.png)](docs/images/blood-gulch-vita3k.png) |
| **Halo CE: campaign · physical Vita · September 5** | **Halo CE: cryo bay · Vita3K · September 8** |
| [![Halo CE campaign gameplay captured on a physical Vita](docs/images/campaign-vita.png)](docs/images/campaign-vita.png) | [![Halo CE cryo bay captured in Vita3K](docs/images/cryo-vita3k.png)](docs/images/cryo-vita3k.png) |

The emulator's **20 FPS** overlay is a test cap, not a Vita measurement.
These captures show different development builds. [Image details](docs/images/README.md).

## Current status

**Tester build: 0.2.0-test.1 · September 18, 2026.** The dashboard and in-game
performance overlay show the version and a short source revision. Include both
in bug reports. [Changes and testing guide](docs/tester-build-20260918.md).

| Area | Current position |
| --- | --- |
| Halo CE | Campaign and solo Split Screen matches run on physical Vita; performance varies substantially by scene |
| Performance | Users report around 12 FPS across Blood Gulch's valley and 20 FPS in some caves, bases and Battle Creek views; these are observations, not a sustained target or a matched comparison |
| CPU and rendering | Worker jobs, native visibility routines and reduced vertex copying are integrated; the newest cumulative changes still need representative hardware testing |
| Stability | Recent user sessions report crashes resolved; long sessions, effects, vehicles and campaign progression still need coverage |
| Dashboard | Game selection, graphics settings, in-game overlay, version display and offline GPL license |
| Halo 2 | Separate experimental application; startup and rendering work exist, but physical hardware support and complete gameplay are unverified |
| Multiplayer networking | Solo play works; communication between two Vitas remains experimental |

Stable **20 FPS** is the next milestone, with **30 FPS** the longer-term goal.
Neither has been established across representative gameplay. Emulator speed does
not predict Vita performance. See [Compatibility](COMPATIBILITY.md).

## Dashboard and controls

Graphics includes textures, filtering, mip smoothing, resolution, materials,
glow, particles, decals, a frame limit, compressed textures and experimental
**triple buffering**. Triple buffering defaults off.
It may increase input delay and is not a guaranteed FPS boost.
Separate switches for **temporary decals**, **cosmetic effects**, **material
reflections** and **object shadows** are available in the current source/private
test build. They default On and require relaunch; see [what each switch changes](docs/visual-switches-20260916.md).

Use **Up/Down** to navigate, **Cross** to select, **Left/Right** to change a
setting, and **Circle** to go back. Changes save automatically. **About / License**
includes the full GPL for offline reading. See the [dashboard guide](dashboard/README.md).

During play, press **Select + Circle** to open the graphics overlay. Resolution,
filtering, mip smoothing, frame limit and triple buffering apply during play.
Other options are labeled **Relaunch Xita to apply**. Changes save automatically;
**Circle** closes the panel. The game continues behind it, so pause Halo with
**Start** first when needed. See [in-game settings](docs/in-game-settings.md).

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
stability, visibility waits and draw preparation. **Select Game** chooses CE or the separately installed experimental Halo 2
application. Additional titles still need their own recompilation and testing.

## Reporting problems

Include the build, Vita or Vita3K, map, graphics settings, actions taken and FPS.
Review logs before sharing: diagnostics can contain game shader definitions,
memory data and local paths. Do not post executables, maps, firmware, SDK files
or unfiltered crash dumps. See [Compatibility](COMPATIBILITY.md#reporting-a-test).

## Contributing and license

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.
PRs need a focused change, relevant validation and clear source/license provenance.

To work on the code, see [Building from source](docs/building.md),
[Windows / WSL developer setup](docs/windows.md) and the
[modular architecture](docs/modular-architecture.md). Source builds currently
require additional development inputs described in the build guide.
Public-release preparation is tracked in the [release audit](docs/release-audit.md).

The source is organized into [runtime/](runtime/README.md) for the Vita renderer
and application, [recompiler/](recompiler/README.md) for the offline pipeline,
and [games/](games/) for title profiles. See the [project layout](docs/project-layout.md)
for the remaining folders and local build inputs.

Xita's original code and documentation are **GPL-3.0-only**. See [LICENSE](LICENSE),
[NOTICE](NOTICE) and [third-party notices](THIRD_PARTY.md). This does not grant
rights to game content or proprietary platform components.

Development uses AI-assisted tools, including Claude Code and Codex, with the
author directing the work and testing on Vita and Vita3K. Contributions still
need review, tests and clear provenance.

Xita is independent of Microsoft, Xbox, Bungie, Halo Studios and Sony. Game and
platform names identify compatibility; trademarks belong to their owners.
