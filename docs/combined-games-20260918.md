# Xita 0.2.0-test.2: one installation, two game runtimes

Install `xita-0.2.0-test.2.vpk` through VitaShell. The launcher and packaged assets
changed, so an executable-only remote update from test.1 is insufficient.
The Vita bubble remains **XITA00001** and opens the Xita dashboard.

Choose **Select Game → Halo: Combat Evolved** or **Halo 2 / Experimental**,
then **Launch Game**. Hold **Start + Select** for one second in Halo 2 to return
to the dashboard. [Halo 2 data paths and limitations](halo2-hardware.md).

Halo CE retains the cumulative performance configuration from test.1. This is
an installation/launch/update change, not a new FPS optimization. Halo 2 is still
experimental; combining the installation does not establish hardware playability.
CE data and saves remain under `ux0:data/xita/`; Halo 2 keeps its separate
`ux0:data/xita-halo2/` tree.

## Updating either game

The stable launcher selects separate executable pairs: `game-a/b.self` for CE
and `halo2-a/b.self` for Halo 2. Each has its own transfer metadata, attempt state,
confirmation and rollback. Different game contracts reject cross-game uploads.
Asset or launcher changes still require installing a complete VPK.

While Xita's paired dashboard service is online:

```sh
python3 tools/vita_remote.py --config /path/to/remote-client.json update xita-0.2.0-test.2.vpk --game haloce --apply
python3 tools/vita_remote.py --config /path/to/remote-client.json update xita-0.2.0-test.2.vpk --game halo2 --apply
```

An H2 apply starts Halo 2 and disconnects the dashboard service. After returning
to Xita, use `update-status --game halo2` to inspect `installed_sha256` and
`installed_slot`. These are persisted confirmation records, not a claim that H2
is currently running. The candidate confirms after three successful frame
presentations. An unconfirmed candidate falls back on the next Halo 2 launch.
Use `rollback --game halo2` for its previous confirmed executable.

## Building a combined private package

Build the Halo 2 target with its required image, generated code and shader inputs
and **`BUNDLED=1`**. Also set `MENU_RUNTIME_DEFAULTS=1 MENU_GXM_DEFAULT=1 MENU_SHADERS=<h2menu gxp dir>`.
Without them, the menu waits forever for a vblank unless `ux0:data/xita-halo2/env.txt` sets
`XV_MENU_VBLANK=1` ([halo2-vita3k-menu-20260924.md](halo2-vita3k-menu-20260924.md)). Then build CE with
`HALO2_PACKAGE=/absolute/path/to/halo2-boot.vpk`, preserving the intended CE
build flags. The packager rejects an old standalone Halo 2 VPK. H2 assets live
under `app0:halo2/`; each engine runs in its own process and only one runs at a time.
The private package contains locally prepared game-derived inputs and stays private.

## Validation

Host tests cover package path validation, separate update contracts, interrupted
uploads, failed-boot fallback, confirmation, independent rollback, HTTP framing
and simultaneous-upload exclusion. Dashboard tests cover selection, controls and
settings persistence. Both Vita targets are cross-compiled. Device launch,
return-to-dashboard and updated-runtime confirmation still need physical validation;
no emulator or hardware FPS claim is made for this packaging change.
