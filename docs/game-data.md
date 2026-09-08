# Halo CE game data setup

[Install Xita](installing.md) · [README](../README.md)

Do this once for a first installation. If Halo already launches in Xita, keep
your existing `ux0:data/xita/halo_image.bin` and `ux0:data/xita/haloce/` files.
App updates use a VPK and do not require preparing the data again.

## Supported game copy

Use your own **original Xbox Halo: Combat Evolved** files. The current build
supports the Xbox 3925 profile; PC Halo CE, Custom Edition, Anniversary and
other Xbox executable revisions are not interchangeable.

The supported `default.xbe` has this SHA-256 fingerprint:

```text
4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae
```

Your extracted game folder should contain `default.xbe` and `maps/`, including
`ui.map`. Keep the original folder layout and all maps you want to play.
See [game extraction](building.md#prepare-your-game) if your own copy is still
in an Xbox disc image.

## Create halo_image.bin

The runtime needs a memory image made from your executable. **Renaming
`default.xbe` to `halo_image.bin` will not work.** This preparation uses Python 3
and two scripts; it does not compile the game or require VitaSDK.

1. Install Python 3 if needed.
2. Download [xbe_parse.py](../xbe_parse.py) and [xbe_image.py](../xbe_image.py)
   using each file page's **Download raw file** button, or use them from a source checkout.
3. Put the two scripts beside your extracted `haloce/` folder:

```text
setup/
├── xbe_parse.py
├── xbe_image.py
└── haloce/
    ├── default.xbe
    └── maps/
        ├── ui.map
        ├── a10.map
        └── ...
```

On **Windows**, open **Command Prompt** in `setup/`. Check the fingerprint
against the value above, then run:

```bat
certutil -hashfile haloce\default.xbe SHA256
py -3 xbe_parse.py haloce\default.xbe --json > game_manifest.json
py -3 xbe_image.py haloce\default.xbe game_manifest.json halo_image.bin
```

On **Linux or macOS**, open a terminal in `setup/`. Use `sha256sum` (Linux) or
`shasum -a 256` (macOS) to check `haloce/default.xbe`, then run:

```sh
python3 xbe_parse.py haloce/default.xbe --json > game_manifest.json
python3 xbe_image.py haloce/default.xbe game_manifest.json halo_image.bin
```

If the fingerprint differs, stop: this VPK does not support that executable.
Successful preparation prints `wrote halo_image.bin`. The current supported
image is **3,819,432 bytes**, including its eight-byte header.

## Copy the data

Use VitaShell USB mode to copy:

| Computer file/folder | Vita destination |
| --- | --- |
| `halo_image.bin` | `ux0:data/xita/halo_image.bin` |
| `haloce/` game folder | `ux0:data/xita/haloce/` |

`ui.map` supplies the Halo menu, `a10.map` is the first campaign level, and
`bloodgulch.map` is Blood Gulch. Copy the remaining campaign maps to continue
beyond the opening mission. The VPK does not supply these files.

Return to [Install and launch](installing.md#3-install-and-launch).
