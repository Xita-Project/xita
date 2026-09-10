# Installing Xita

[README](../README.md) · [Download VPK](https://github.com/Xita-Project/xita/releases/tag/dev-20260909-gameplay) · [Game data](game-data.md) · [Compatibility](../COMPATIBILITY.md)

Install the app from a VPK, then add your own Halo data. You need a
homebrew-enabled PS Vita, VitaShell and a USB cable. Windows, macOS and Linux
can all copy the files; VitaSDK and a source build are unnecessary for installation.

## First installation: create the game image

The VPK does not contain `halo_image.bin`. You generate it from **`default.xbe`**
from your own supported original Xbox Halo CE copy; do not rename the XBE.
If Halo already launches on your Vita, keep your existing image and skip this step.

Download [xbe_parse.py](../recompiler/xbe_parse.py) and [xbe_image.py](../recompiler/xbe_image.py)
using **Download raw file**, install Python 3, and place the scripts beside your
extracted `haloce/` folder. In that folder, run these commands in **Windows Command Prompt**:

```bat
py -3 xbe_parse.py haloce\default.xbe --json > game_manifest.json
py -3 xbe_image.py haloce\default.xbe game_manifest.json halo_image.bin
```

On **Linux/macOS**, use `python3` in place of `py -3` and
`haloce/default.xbe` in place of `haloce\default.xbe`.
The resulting `halo_image.bin` appears beside the scripts. Copy it to
`ux0:data/xita/halo_image.bin`, along with the `haloce/` folder as shown below.
The [full game-data guide](game-data.md) includes the supported executable
fingerprint and folder layout.

## 1. Download a VPK

Open the [current Xita game release](https://github.com/Xita-Project/xita/releases/tag/dev-20260909-gameplay),
expand **Assets**, and download **`xita-gameplay-20260909.vpk`**.
Each release contains one VPK, `SHA256SUMS.txt` and `BUILD-INFO.json`. Sign in
with an account that has repository access while releases are private.
GitHub's automatic **Source code** downloads are not installable apps.

This development package removes function tracing from normal gameplay and
keeps the built-in **Default** and **Inverted** profile fix. It also revises the
LiveArea artwork format after a blank-background report. Host and linked-ARM
checks pass; hardware FPS and LiveArea confirmation are pending.
[Build and test details](gameplay-build-20260909.md).

It also includes the in-game graphics panel and the previous optional
[vertex-validation comparison](vertex-references-20260908.md), which remains off
by default. This release has no measured hardware performance improvement.

For an older build, use the [release guide](releases.md). Older releases are
marked **Superseded**. The separate **Xita AdHoc Test** VPK tests networking on two
consoles and does not install or update Halo.

## 2. Copy over USB

1. Close Xita and open **VitaShell**.
2. In VitaShell's Start menu, set the SELECT-button action to **USB** and choose
   the USB device that contains your `ux0:` storage. Press Select and connect the cable.
3. Open the Vita drive on your computer. On Windows, use **File Explorer → This PC**.
4. Copy the VPK to a convenient folder such as `VPK/` on that drive.
5. On first setup, copy the files from [Game data setup](game-data.md) as well.
   If Halo already works in Xita, keep the existing data.

When the drive represents `ux0:`, its `data/xita/` folder corresponds to
`ux0:data/xita/` in VitaShell. Do not create a folder literally named `ux0:`. Open `data/xita/` on the drive
and paste your `haloce` folder there once. The result must contain
`data/xita/haloce/maps/ui.map`; `data/xita/haloce/haloce/maps/` is one folder too deep.

```text
Vita drive/
├── VPK/
│   └── xita-gameplay-20260909.vpk
└── data/
    └── xita/
        ├── halo_image.bin
        └── haloce/
            └── maps/
                ├── ui.map
                ├── a10.map
                ├── bloodgulch.map
                └── ...other maps from your copy
```

## 3. Install and launch

1. Safely eject the Vita drive on your computer, then leave USB mode in VitaShell.
2. Browse to the copied VPK, open it and follow VitaShell's installation prompts.
3. Open the **Xita** bubble from the Vita home screen.
4. Choose settings in **Graphics**, then select **Launch Game**.
5. Use Halo's campaign menu, or **Split Screen** for a solo multiplayer map.

During gameplay, **Select + Circle** opens the [graphics overlay](in-game-settings.md).
It labels which changes apply during play and which need a relaunch.

Use **Up/Down** to navigate Xita, **Cross** to select, **Left/Right** to change a
setting, and **Circle** to go back. Settings save automatically.
**About / License** contains the GPL for offline reading.

## Updating Xita

Close the game and back up `ux0:data/xita/save/` and `ux0:data/xita/xita.cfg` to
your computer. Install the newer VPK over the existing Xita app using the steps
above. You do not need to uninstall the bubble or recopy unchanged game data.

Use the build filename when reporting results. Returning to the recovery build
uses the same VPK installation process. Existing private development shader
overrides are outside the VPK; if a release appears to show old rendering,
include that detail when reporting it.

## Upgrading an older XboxVita installation

The older **XboxVita** bubble uses app ID `XVIT00001` and may store Halo under
`ux0:data/xboxvita/`. The current **Xita** bubble uses `XITA00001` and reads
`ux0:data/xita/`.

Install the current VPK in VitaShell, then copy your existing `halo_image.bin`,
`haloce/` and `save/` from the old data folder into `ux0:data/xita/`. If the new
folder already contains settings or saves, back up both versions before choosing
which to keep. Keep the older installation until Xita launches successfully.
Do not replace the old app's executable directly: install the current VPK to
register the new bubble and app ID.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Releases gives a 404 or no downloads appear | Sign in with an account that can access this private repository. |
| Download contains source files rather than an app | Choose the `.vpk` under **Assets**, not **Source code**. |
| Vita drive does not appear | Enable VitaShell USB mode; use a data-capable cable and the correct USB device. |
| Installation fails | Record the exact error code; confirm free space and that the VPK download completed. |
| Xita opens but Halo does not | Check `halo_image.bin`, the supported Xbox revision and `haloce/maps/ui.map`. |
| A campaign level or multiplayer map cannot load | Copy that map from your game data; the VPK contains no maps. |
| Low FPS | Performance work is ongoing. 640×360 has helped on hardware; sustained 20 FPS is still the goal. |

See [Compatibility](../COMPATIBILITY.md) for known issues and what to include in a report.
