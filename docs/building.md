# Building and installing Xita

[README](../README.md) · [Windows setup](windows.md) · [Release audit](release-audit.md)

These steps describe the **development checkout**. The source review export
omits game-derived shaders, metadata and unverified symbol inputs. Its dashboard
can be built separately; it is not yet a complete from-scratch Halo build.

## Requirements

- Linux, macOS with a suitable VitaSDK toolchain, or Windows through WSL2.
- [VitaSDK](https://vitasdk.org/), Python 3, Make, Git and a host C compiler.
- Your own supported Halo Xbox executable and maps; other revisions are unvalidated.
- A homebrew-enabled Vita and VitaShell. Vita3K is optional for development.

Install VitaSDK using its [official instructions](https://vitasdk.org/). Keep
it outside the repository and set its actual installation path:

```sh
export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$PATH"
arm-vita-eabi-gcc --version

python3 -m venv .venv
. .venv/bin/activate
python -m pip install iced-x86
```

## Prepare your game

Use files from your own supported game copy:

```text
haloce/
  default.xbe
  maps/
    ui.map
    a10.map
    bloodgulch.map
    ...
```

With [extract-xiso](https://github.com/XboxDev/extract-xiso), an explicit
extraction directory is selected with `-d`:

```sh
extract-xiso -x /path/to/your/halo.iso -d haloce
```

The current port also needs the corresponding `halo_symbols.json` and the
translated shader inputs used by the development build. Their clean-source
generation/provenance is an outstanding release gate. Do not substitute inputs
from another game or executable version.

## Build the development port

From the repository root, with the virtual environment active:

```sh
python xbe_parse.py haloce/default.xbe --json > game_manifest.json
tools/recomp.sh haloce/default.xbe
python xbe_image.py haloce/default.xbe game_manifest.json recomp/halo_image.bin
make -j2 RECOMP=1
```

The result is `xita.vpk`. The Makefile already links the touch library; no extra
override is needed. Generated C, game images, shader translations and the
Halo-linked VPK are local build products, not public release inputs.

Changed shaders must be compiled before packaging the VPK. `make shaders`
uses an available authorized `psp2cgc`; `make shadercomp` builds the on-device
helper. The helper requires the console's shader compiler module, which Xita
does not provide. Do not bundle or obtain unauthorized SDK or firmware copies.
The Makefile documents the developer shader upload/download targets.

## Install over USB

1. Open VitaShell in USB mode and connect the Vita.
2. Copy your locally built VPK to the card and copy the game data below.
3. Safely eject, leave USB mode, and install the VPK with VitaShell.
4. Open Xita, choose settings, and select **Launch Game**.

```text
ux0:data/xita/halo_image.bin          from recomp/halo_image.bin
ux0:data/xita/haloce/maps/ui.map      required for Halo's menu
ux0:data/xita/haloce/maps/a10.map     first campaign level
ux0:data/xita/haloce/maps/bloodgulch.map
ux0:data/xita/xita.cfg               dashboard settings
```

Keep other required game files and desired maps in the matching `haloce/`
layout. Packaged shaders load from the application; device overrides are a
developer feature. Back up `ux0:data/xita/save/` before replacing an installation.

## Build just the dashboard

This does not require game content:

```sh
python3 tools/embed_license.py
make -C dashboard host-test
make -C dashboard
```

The standalone dashboard is a UI development tool. It does not include a
recompiled game engine. The Halo runtime uses the embedded dashboard.
