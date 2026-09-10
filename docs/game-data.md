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
2. Download [xbe_parse.py](../recompiler/xbe_parse.py) and [xbe_image.py](../recompiler/xbe_image.py)
   using each file page's **Download raw file** button, or use them from a source checkout.
3. Put the two scripts beside your extracted `haloce/` folder:

```text
setup/                 ← open your terminal here
├── xbe_parse.py
├── xbe_image.py
└── haloce/
    ├── default.xbe
    └── maps/
        ├── ui.map
        ├── a10.map
        └── ...
```

On **Windows**, open **Command Prompt or PowerShell in `setup/`**, the folder
containing both scripts and the `haloce` folder. Run `dir` to check that layout.
The prompt should end in `\setup>`, and `dir haloce\default.xbe` should find
your executable. `setup` is an example folder name; your parent folder can have
a different name. Check the fingerprint against the value above, then run:

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
Continue only after both commands succeed. Successful preparation prints
`wrote halo_image.bin`. The current supported
image is **3,819,432 bytes**, including its eight-byte header.

## Copy the data

Use VitaShell USB mode to copy:

| Computer file/folder | Vita destination |
| --- | --- |
| `halo_image.bin` | `ux0:data/xita/halo_image.bin` |
| `haloce/` game folder | Open `ux0:data/xita/` and paste the folder there once |

The final layout must be:

```text
ux0:data/xita/
├── halo_image.bin
└── haloce/
    ├── default.xbe
    └── maps/
        ├── ui.map
        ├── a10.map
        ├── bloodgulch.map
        └── ...
```

There is **one `haloce` folder**. If you already created `xita/haloce/`, copy the
contents of the computer's `haloce/` into it. A path such as
`xita/haloce/haloce/maps/ui.map` is one level too deep.

`ui.map` supplies the Halo menu, `a10.map` is the first campaign level, and
`bloodgulch.map` is Blood Gulch. Copy the remaining campaign maps to continue
beyond the opening mission. The VPK does not supply these files.

Return to [Install and launch](installing.md#3-install-and-launch).

## File not found while creating the image

`haloce\default.xbe` means “open the `haloce` folder inside the current folder.”
If your prompt already ends in `\haloce>`, that path would look for
`haloce\haloce\default.xbe`, causing `No such file or directory`.

If `dir` shows **`xbe_parse.py`, `xbe_image.py` and `default.xbe` together** in
your current folder, use these commands in Command Prompt or PowerShell:

```powershell
certutil -hashfile .\default.xbe SHA256
py -3 xbe_parse.py .\default.xbe --json > game_manifest.json
py -3 xbe_image.py .\default.xbe game_manifest.json halo_image.bin
```

If the scripts are in the parent folder as shown in the setup diagram, run
`cd ..` to return there and use the original `haloce\default.xbe` commands.
Do not create another `haloce` folder to fix this error.

After correcting the path, rerun **both** Python commands. A failed first
command can leave an empty `game_manifest.json`; its presence alone does not
mean the manifest was created successfully. Copy the resulting image separately
to `ux0:data/xita/halo_image.bin`, even if you generated it inside `haloce`.

## Manifest encoding error on Windows

If `xbe_image.py` reports `UnicodeDecodeError` with byte `0xff`, download the
updated [xbe_image.py](../recompiler/xbe_image.py) and repeat the image command.
It accepts UTF-8 and UTF-16 manifests, including the UTF-16 output produced by
Windows PowerShell redirection. Existing manifests work without resaving them.
With an older script, saving `game_manifest.json` as UTF-8 in Notepad is a
workaround. This affects preparation on the computer; no app update is required.
