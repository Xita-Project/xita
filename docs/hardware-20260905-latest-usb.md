# September 5: latest integrated build over USB

At the user's request, the latest local build was deployed to the connected Vita
at 18:04 CDT. This snapshot contains the three branch integrations, menu routing
correction, shared pixel-shader link cache, and default-off camera recovery
heuristic described in [the integration report](branch-integration-20260905.md).
The opening cutscene freeze remains unresolved and was disclosed before transfer.

This snapshot subsequently crashed in Blood Gulch. It was superseded at 18:31 CDT
by the [frame handoff correction](hardware-20260905-bloodgulch-crash.md). The
artifact and transfer details below describe the earlier snapshot.

## Artifact and validation

The native build is current (`make RECOMP=1 -j8`). The compressed SELF was padded
to the existing installed executable's 32,918,474-byte length. All three
decompressed segments match `build/eboot.bin`. The exact padded candidate boots
the dashboard and reaches Halo's main menu in Vita3K using the physical Vita's
current configuration. Hardware startup and performance remain to be tested.

Installed SHA-256:
`caedaae7c61396c402e5dbc70c833474f7685b1f925230f58e560ee5259bbd1d`.

Previous installed SHA-256:
`5f2b5fdb5e464decba9e21ced302fb8b08f59168c352257807c6ad6bfe6d853d`.

The VPK passes CRC validation. Its 591 already-installed packaged assets match;
the three additional HUD programs are embedded in the executable. Only
`app/XITA00001/eboot.bin` was written, through an exact-length `r+b` overwrite
and `fsync`, without truncation, rename, or allocating a new file on the card.
Direct reads verified the executable before and after an unmount/remount.

All 937 other files in the pre-transfer manifest retained their size and hash,
including all 53 save/cache files, the settings, log, other app files, and the
readable backed-up screenshots. The previously unreadable screenshot described
in [the Battle Creek report](hardware-20260905-battlecreek-480p.md) was excluded;
this verification does not establish filesystem health. The USB volume was
safely unmounted at 18:05 CDT.

Settings remain 848x480, medium/256 textures, linear filtering, mip smoothing on,
with the dashboard and per-core overlay enabled. Diagnostic `XV_SPIN_BT` was
used only in the earlier emulator reproduction and was not added to Vita settings.

## Evidence and rollback

Local artifacts, the previous executable/config/log, before-transfer hashes,
boot screenshots, and the deployment record are under
`/home/birchwoodgod/xita-backups/2026-09-05-180227-latest-usb/`.
Unchanged save/cache and screenshot backups remain in the preceding Battle Creek
backup. The previous executable can be restored by the same exact-length method.

Launch from the existing Xita bubble after exiting VitaShell USB mode; a VPK
installation is not required. This transfer does not resolve the freeze, geometry
spikes, flashlight disappearance, incomplete HUD, or the 25 fps performance target.
