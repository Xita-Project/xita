# Xita

Original Xbox games running natively on the PlayStation Vita, one static recompilation at
a time. The first title is Halo: Combat Evolved; Halo 2 is next.

Xita is not an emulator. A game's x86 executable is **statically recompiled** to C by
`xita-recomp` and linked against a runtime (`xita`) that re-implements the Xbox kernel,
Direct3D 8 and DirectSound surface the game expects on top of the Vita's own libraries
(GXM, sceCtrl, sceAudio, sceIo). The result is a normal homebrew VPK. A dashboard that
lists and launches the installed games is on the roadmap.

**You need your own copy of the game.** Xita ships no game data. The executable and the
maps are extracted from your own Xbox disc image and stay on your memory card.
No prebuilt VPKs are published either: the runtime binary contains the recompiled game
code, so every user builds it from their own executable (see Build below).

## Status

| Area | State |
| --- | --- |
| Boot, main menu, profiles, saves | Works on hardware |
| Blood Gulch (solo) | Playable on hardware, 11 to 20 fps on foot |
| Campaign (Pillar of Autumn) | Intro cinematic and cryo tutorial play; camera bug after the cryo-tube exit |
| Rendering | Terrain, models, lightmaps, bump lighting, reflections, sky, fog correct; no alpha test yet |
| Audio | Streams and effects through a software mixer |
| Multiplayer | Single Vita only; ad-hoc between Vitas is the next goal |

See [ROADMAP.md](ROADMAP.md) for the plan, [COMPATIBILITY.md](COMPATIBILITY.md) for the
per-game list, and [docs/halo-ce.md](docs/halo-ce.md) for Halo's per-level status. The
frame rate is CPU-bound and the current target is 25 fps on foot.

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
printf 'LIBS += -lSceTouch_stub\n' | make -f Makefile -f - RECOMP=1  # -> xita.vpk

# 4. build the game image the runtime loads (the XBE's sections, laid out for the guest)
python3 xbe_image.py haloce/default.xbe game_manifest.json halo_image.bin
```

The build command adds the Vita SDK touch import library to the Makefile’s libraries.

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
| Rear touch left / right half | Black / White | switch grenade / flashlight |
| Front touch bottom-left / bottom-right (300×200 each) | L3 / R3 | crouch / zoom |
| Select + Start (hold) | | frame-time overlay |

Touch coordinates use the 1920×1088 panel space. D-pad substitutions apply only during
gameplay; touch inputs are ORed in independently.

## Settings and debug knobs

`ux0:data/xita/xita.cfg` (on the emulator also `env.txt`), one `KEY=VALUE` per line:

| Key | Effect |
| --- | --- |
| `XV_FPS=1` | frame-time overlay |
| `XV_BC_MIPS=1` | upload mip chains for compressed textures (verified on the emulator, being verified on hardware) |
| `XV_VBLANK_HZ=60` | diagnostic only: rate of the vblank counter. Halo derives game time from it, so anything but 60 changes the game's speed |
| `XV_PROF=1` | sample the running guest function and log the top entries |
| `XV_TOUCH=0/1` | enable touch zones (default 1); unsupported panels behave as untouched |
| `XV_TOUCH_SWAP=1` | swap rear Black/White halves (default 0) |
| `XV_DEADZONE=0..99` | radial deadzone percent on both sticks, rescaled outside the deadzone (default 0) |
| `XV_LOOK_SENS=0..400` | right-stick sensitivity percent (default 100), clamped to Xbox range |
| `XV_LOOK_CURVE=0/1/2` | right-stick response: linear / legacy (also linear) / squared (default 0) |
| `XV_INVERT_Y=0/1` | invert right-stick Y (default 0) |
| `XV_PAD_REC=1` | record pad input to `pad_rec.txt`; copy it to `pad_play.txt` to replay it |
| `XV_FORCE_START=1` | let a one-player multiplayer lobby start (temporary until ad-hoc play exists) |

Stick settings are read once at the first pad poll; restart after editing. With defaults,
the original integer stick mapping is preserved exactly. The startup pad log prints
effective settings and a default-axis self-check. Deadzone precedes look curve,
sensitivity and Y inversion. Numeric settings are clamped to the ranges above.

Logs go to `ux0:data/xita/xita.log` (the previous three runs are kept). Touching
`ux0:data/xita/hist.now` dumps the next frame's draw calls.

## Testing on Vita3K

```sh
tools/vita3k.sh install xita.vpk XITA00001
tools/vita3k.sh run XITA00001 120 frames/      # runs for 120 s and captures frames
tools/vita3k.sh shaders shaders/                # compiles shaders/*.cg inside the emulator
```

`ux0:data/xita/pad.txt` scripts input by frame (`500:a,900:lup*100,...`) for unattended runs.
It also accepts `black`, `white`, `l3`, and `r3` (for example `500:black*30`).
Recordings use `frame lx ly rx ry buttons` with optional trailing `black white l3 r3`
names; releases are recorded too. Existing six-column recordings and scripts still
work. Replay adds to live input and uses the current stick settings, so keep those
settings the same when reproducing a recording. `XV_TOUCH=0` disables panel reads,
not scripted or replayed touch buttons.

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

## How this was built

Most of the code, tooling and documentation in this repository was written with Claude
(Fable 5.1, Anthropic) working in Claude Code, with the project's author directing the
work, testing every build on real hardware and on Vita3K, and deciding what to fix and in
what order. The reverse-engineering findings in the notes and commit messages were
established the same way: by measurement against the running game, not by memory. Treat
the code as you would any other contributor's: read it, test it, report what breaks.

## Legal

Xita is an independent research project. Halo and Xbox are trademarks of their respective
owners. No game content is distributed: the recompiled code, the game image and the maps
are produced from, and only from, the user's own copy.
