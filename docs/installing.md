# Installing Xita

[README](../README.md) · [Download VPK](https://github.com/BirchWoodGod/xita/releases) · [Game data](game-data.md) · [Compatibility](../COMPATIBILITY.md)

Install the app from a VPK, then add your own Halo data. You need a
homebrew-enabled PS Vita, VitaShell and a USB cable. Windows, macOS and Linux
can all copy the files; VitaSDK and a source build are unnecessary for installation.

## 1. Download a VPK

Open [Releases](https://github.com/BirchWoodGod/xita/releases), expand **Assets**,
and download a file ending in `.vpk`. Sign in with a GitHub account that has
repository access while releases are private.

The [September 8 development release](https://github.com/BirchWoodGod/xita/releases/tag/dev-20260908)
has two choices:

| File | Use |
| --- | --- |
| `xita-installed-20260908.vpk` | Recovery package matching the executable already installed on the test Vita. |
| `xita-local-rendering-modular-20260908.vpk` | Newer experimental rendering/library build for testing; physical Vita validation is pending. |

The recovery executable was verified after USB installation. Both VPK archives
were checked locally, but these newly packaged VPKs have not yet been installed
through VitaShell. Neither is a stable release. `BUILD-INFO.json` identifies each
build; `SHA256SUMS.txt` contains download checksums. GitHub's automatic **Source code**
downloads are not installable apps.

## 2. Copy over USB

1. Close Xita and open **VitaShell**.
2. In VitaShell's Start menu, set the SELECT-button action to **USB** and choose
   the USB device that contains your `ux0:` storage. Press Select and connect the cable.
3. Open the Vita drive on your computer. On Windows, use **File Explorer → This PC**.
4. Copy the VPK to a convenient folder such as `VPK/` on that drive.
5. On first setup, copy the files from [Game data setup](game-data.md) as well.
   If Halo already works in Xita, keep the existing data.

When the drive represents `ux0:`, its `data/xita/` folder corresponds to
`ux0:data/xita/` in VitaShell. Do not create a folder literally named `ux0:`.

```text
Vita drive/
├── VPK/
│   └── xita-installed-20260908.vpk
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
