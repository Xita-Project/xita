# Xita

Halo: Combat Evolved, the original Xbox release, running natively on the PlayStation Vita.

Xita is not an emulator. The game's x86 executable is **statically recompiled** to C by
`xita-recomp` and linked against a runtime (`xita`) that re-implements the Xbox kernel,
Direct3D 8 and DirectSound surface the game expects on top of the Vita's own libraries
(GXM, sceCtrl, sceAudio, sceIo). The result is a normal homebrew VPK.

**You need your own copy of the game.** Xita ships no game data. The executable and the
maps are extracted from your own Xbox disc image and stay on your memory card.

## Status

| Area | State |
| --- | --- |
| Boot, main menu, profiles, saves | Works on hardware |
| Blood Gulch (solo) | Playable on hardware, 11 to 20 fps on foot |
| Campaign (Pillar of Autumn) | Intro cinematic and cryo tutorial play; camera bug after the cryo-tube exit |
| Rendering | Terrain, models, lightmaps, bump lighting, reflections, sky, fog correct; no alpha test yet |
| Audio | Streams and effects through a software mixer |
| Multiplayer | Single Vita only; ad-hoc between Vitas is the next goal |

See [ROADMAP.md](ROADMAP.md) for the plan and [COMPATIBILITY.md](COMPATIBILITY.md) for
per-level status. The frame rate is CPU-bound and the current target is 25 fps on foot.

## What you need

- A PlayStation Vita or PS TV with homebrew (VitaShell to install VPKs).
- Your own Halo: Combat Evolved Xbox disc image (the `3925` build is the one tested).
- A Linux or macOS machine with [vitasdk](https://vitasdk.org), Python 3 and
  `pip install iced-x86`, plus `extract-xiso` (or any tool that extracts an Xbox ISO).
- Optional: [Vita3K](https://vita3k.org) for testing without the console.

## Build

```sh
# 1. extract your disc image; you need default.xbe and the maps
extract-xiso -x halo.iso haloce/            # -> haloce/default.xbe, haloce/maps/*.map

# 2. recompile the executable (writes recomp/code_*.c, never committed)
tools/recomp.sh haloce/default.xbe

# 3. build the runtime VPK
export VITASDK=$HOME/vitasdk PATH=$HOME/vitasdk/bin:$PATH
make RECOMP=1                               # -> xita.vpk

# 4. build the game image the runtime loads (the XBE's sections, laid out for the guest)
python3 xbe_image.py haloce/default.xbe game_manifest.json halo_image.bin
```

The vertex programs and register-combiner programs under `shaders/` are compiled on the
console: install `tools/shadercomp/xv_shadercomp.vpk`, put the `.cg` files in
`ux0:data/xita/shaders/`, run it once, and the `.gxp` files land next to them
(`make shaders-usb` / `make shaders-pull-usb` automate the copy over USB).

## Install

```
ux0:/xita.vpk                              install with VitaShell
ux0:data/xita/halo_image.bin               from step 4
ux0:data/xita/haloce/maps/ui.map           the menu
ux0:data/xita/haloce/maps/a10.map          plus whichever levels and multiplayer maps you want
ux0:data/xita/shaders/*.gxp                compiled shaders
ux0:data/xita/xita.cfg                     optional settings, one KEY=VALUE per line
```

`make deploy-usb` stages the VPK and data onto a card mounted at `VITA_MOUNT`;
`make deploy-ftp VITA_IP=...` does the same over VitaShell's FTP.

Upgrading from the pre-rename layout (`ux0:data/xboxvita`) needs nothing: the first
launch renames the directory and `xboxvita.cfg` in place.

## Controls (default)

| Vita | Xbox | Halo |
| --- | --- | --- |
| Left / right stick | sticks | move / look |
| Cross, Circle, Square, Triangle | A, B, X, Y | jump, melee, action and reload, switch weapon |
| L / R | triggers | throw grenade / fire |
| Start / Select | Start / Back | pause |
| D-pad down / up | stick clicks | crouch / zoom |
| D-pad right / left | White / Black | flashlight / switch grenade |
| Select + Start (hold) | | frame-time overlay |

## Settings and debug knobs

`ux0:data/xita/xita.cfg` (on the emulator also `env.txt`), one `KEY=VALUE` per line:

| Key | Effect |
| --- | --- |
| `XV_FPS=1` | frame-time overlay |
| `XV_BC_MIPS=1` | upload mip chains for compressed textures (verified on the emulator, being verified on hardware) |
| `XV_VBLANK_HZ=1000` | rate of the vblank counter the game waits on; higher means less idle time per frame |
| `XV_PROF=1` | sample the running guest function and log the top entries |
| `XV_PAD_REC=1` | record pad input to `pad_rec.txt`; copy it to `pad_play.txt` to replay it |
| `XV_FORCE_START=1` | let a one-player multiplayer lobby start (temporary until ad-hoc play exists) |

Logs go to `ux0:data/xita/xita.log` (the previous three runs are kept). Touching
`ux0:data/xita/hist.now` dumps the next frame's draw calls.

## Testing on Vita3K

```sh
tools/vita3k.sh install xita.vpk XITA00001
tools/vita3k.sh run XITA00001 120 frames/      # runs for 120 s and captures frames
tools/vita3k.sh shaders shaders/                # compiles shaders/*.cg inside the emulator
```

`ux0:data/xita/pad.txt` scripts input by frame (`500:a,900:lup*100,...`) for unattended runs.

## Layout

| Path | Contents |
| --- | --- |
| `xita_recomp.py`, `xbe_*.py`, `dx8_*.py`, `*_recomp_gen.py` | the offline pipeline: XBE parsing, x86 to C, shader and combiner translation |
| `recomp/kernel/` | the Xbox kernel, XAPI, D3D and DirectSound surface, file and thread layers |
| `recomp/xv_x86rt.*` | the x86 runtime the generated code targets: guest memory, lazy flags, x87 |
| `main.c`, `xv_*.c` | the Vita application: GXM bridge, texture cache, shaders, logging |
| `shaders/` | translated vertex programs and generated fragment programs |
| `tools/` | recompile, emulator and on-device shader-compiler helpers |
| `site/` | the project page |

## Legal

Xita is an independent research project. Halo and Xbox are trademarks of their respective
owners. No game content is distributed: the recompiled code, the game image and the maps
are produced from, and only from, the user's own copy.
