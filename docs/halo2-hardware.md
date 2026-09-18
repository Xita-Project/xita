# Halo 2 experimental hardware setup

The combined **Xita 0.2.0-test.2** VPK includes Halo CE and the experimental
Halo 2 runtime under one **XITA00001** bubble. Halo 2
physical Vita behavior is not yet validated.
The selector provides a launch path; it does not establish that Halo 2 is playable.

1. Install the privately prepared **combined Xita VPK** through VitaShell.
   This first upgrade requires the full VPK because the launcher and assets changed.
   A separate Halo 2 application is no longer required.
2. Copy the contents of your supported Xbox Halo 2 game folder into
   `ux0:data/xita-halo2/game/`. Keep the complete data set, including maps,
   fonts and movie data. The menu map must be
   `ux0:data/xita-halo2/game/maps/mainmenu.map`.
3. Open Xita, choose **Select Game → Halo 2 / Experimental**, then **Launch Game**.
   A missing package or menu map is reported in the dashboard.
4. Hold **Start + Select** for one second to return to Xita, then select Halo CE.
   The shortcut runs at a guest scheduling boundary; it cannot recover a frozen
   native call. Closing and reopening the Xita bubble also returns to the dashboard.

```text
ux0:data/
├── xita/                         # existing CE data/settings/saves
└── xita-halo2/
    ├── game/
    │   ├── default.xbe
    │   └── maps/
    │       ├── mainmenu.map
    │       ├── shared.map
    │       └── ...               # the remaining original game files
    ├── save/                     # created by the Halo 2 runtime
    └── boot.log
```

The current Halo 2 development package includes locally prepared image and
shader inputs. Keep it private. It requires the supported 5849 profile; do not
reuse `halo_image.bin` or CE maps. Additional cached menu/scene shaders must be
included with the prepared Halo 2 package; a generic startup probe is insufficient.
Initial map-cache creation can take time and extra storage.

Start with boot, intro, menu input and a short session. Report the package's
build information, exact screen reached and `ux0:data/xita-halo2/boot.log`.
CE graphics settings do not control Halo 2. The dashboard can stage and apply
updates for either game, with independent rollback slots. The remote service
runs in the dashboard/CE process; return from Halo 2 before fetching update status
or applying another update. Halo 2 confirmation means three frames were presented,
not that gameplay has been validated. Bundled Halo 2 reads optional development
settings from `ux0:data/xita-halo2/env.txt`, not CE's `env.txt`.
