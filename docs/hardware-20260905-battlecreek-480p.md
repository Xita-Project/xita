# Battle Creek hardware run — September 5, 2026

The user played Battle Creek and reported intermittent geometry spikes. USB
collection was read-only. The installed executable is still the earlier 480p
build, SHA-256 `5f2b5fdb5e464decba9e21ced302fb8b08f59168c352257807c6ad6bfe6d853d`;
it does not contain the three-branch integration or subsequent menu/cache/camera
corrections.

## Settings and timing

The dashboard configuration now selects 848x480, Medium textures (`256`), Linear
filtering (`2`) and mip smoothing on (`1`). These differ from the preceding Low/
Point/mip-off Blood Gulch run, so this is not a controlled performance comparison.

The log contains 114 sixty-frame timing windows. The 82 windows with BSP draws
cover 4,920 frames and approximately 358 seconds:

- Aggregate Present throughput: **13.74 fps**.
- Window range: **7.1–20.6 fps**; no window reaches 25 fps.
- Mean game phase: 62.57 ms; mean pump phase: 45.35 ms. Phases overlap and must
  not be added to derive total frame time.

The newest screenshots show the plasma pistol and reticle at 16–17 fps. The
17:42:18 capture clearly shows long, incorrectly textured triangles crossing the
map. This establishes hardware corruption independent of the new branch work.
Sky/effect rendering is also incomplete in this build.

## Evidence and preservation

Backup directory:
`/home/birchwoodgod/xita-backups/2026-09-05-174801-battlecreek-480p/`.

The log, config, executable, all saves/cache files and readable screenshots were
copied: 345 files. All 594 installed app files were hashed into `app-before.json`.
Five new screenshots are dated 17:39:44, 17:41:12, 17:42:18, 17:44:14 and 17:44:27.

One older screenshot, `picture/SCREENSHOT/ch/2026-09-05-161238.png`, returned an
I/O error on both initial read and retry. `read-errors.json` records this; its
previous verified backup was preserved separately under `previous-backup-copy/`.
No file was written on the Vita, and the USB volume was unmounted after collection.
