# Building Xita from source

[README](../README.md) · [Install a VPK](installing.md) · [Windows / WSL setup](windows.md) · [Release audit](release-audit.md)

To install the app, use [VPK releases and USB installation](installing.md).
The instructions below are for developers rebuilding Xita.

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

The current port also needs the corresponding
`local/halo_ce_3925/halo_symbols.json` and the
translated shader inputs used by the development build. Their clean-source
generation/provenance is an outstanding release gate. Do not substitute inputs
from another game or executable version.

## Build the development port

From the repository root, with the virtual environment active:

```sh
mkdir -p local/halo_ce_3925
python recompiler/xbe_parse.py haloce/default.xbe --json > local/halo_ce_3925/game_manifest.json
tools/recomp.sh haloce/default.xbe
python tools/gen_native_clip.py
python recompiler/xbe_image.py haloce/default.xbe local/halo_ce_3925/game_manifest.json recomp/halo_image.bin
make -j2 RECOMP=1
```

The result is `xita.vpk`. The Makefile already links the touch library; no extra
override is needed. Generated C, game images, shader translations and the
Halo-linked VPK are local build products, not public release inputs.

Keep game metadata and local helper binaries under the ignored `local/` folder.
The supported executable stays at `haloce/default.xbe`. Rebuilding shader lookup
tables for a game build requires that executable and its manifest; the build
must retain the shader identities used to recognize the game's programs.

Changed shaders must be compiled before packaging the VPK. `make shaders`
uses an available authorized `psp2cgc`; `make shadercomp` builds the on-device
helper. The helper requires the console's shader compiler module, which Xita
does not provide. Do not bundle or obtain unauthorized SDK or firmware copies.
The Makefile documents the developer shader upload/download targets.

## Install over USB

Follow the [USB installation guide](installing.md#2-copy-over-usb), using your
locally built `xita.vpk` and `recomp/halo_image.bin`. Keep the game folder layout
described there. Packaged shaders load from the application; device overrides
are a developer feature.

## Build just the dashboard

This does not require game content:

```sh
python3 tools/embed_license.py
make -C dashboard host-test
make -C dashboard
```

The standalone dashboard is a UI development tool. It does not include a
recompiled game engine. The Halo runtime uses the embedded dashboard.
