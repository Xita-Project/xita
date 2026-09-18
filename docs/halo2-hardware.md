# Halo 2 experimental hardware setup

Halo 2 is a separate development application, **XH2B00001**. It is not included
in the Halo CE tester VPK, and its physical Vita behavior is not yet validated.
The selector provides a launch path; it does not establish that Halo 2 is playable.

1. Install the privately prepared **Halo 2 experimental VPK** through VitaShell.
   Do not install a DSP, graphics or audio probe in its place.
2. Copy the contents of your supported Xbox Halo 2 game folder into
   `ux0:data/xita-halo2/game/`. Keep the complete data set, including maps,
   fonts and movie data. The menu map must be
   `ux0:data/xita-halo2/game/maps/mainmenu.map`.
3. Open Xita, choose **Select Game → Halo 2 / Experimental**, then **Launch Game**.
   A missing package or menu map is reported in the dashboard.
4. To return to CE, close Halo 2, reopen Xita, then select Halo CE.

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
CE's remote updater and graphics settings do not control this separate process.
After leaving Halo 2, reopen Xita to restore the CE remote service.
