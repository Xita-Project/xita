# Overnight hardware session, collected September 6

USB backup: `2026-09-06-074617-overnight-hardware-review` under
`/home/birchwoodgod/xita-backups/`. Seventy-two files (62,386,393 bytes) include
logs, profiles/cache, the installed executable and fourteen new screenshots.
Collection mounted the card read-only. No original save was repaired or replaced.

The installed executable hash was
`6ad07ed12c73d5943cd32c038a547072821d025e5857a300238eaee3a9a3ed18`, the September 5
22:06 build. This is a baseline for the overnight changes, not a measurement of
them. The user selected textures 128, 480p, mip smoothing off, filtering 1 and
rear touch off before launching. Those settings are preserved in the update.

The log's sixty-frame windows with BSP draws, grouped by the latest map open:

| Map | Windows | FPS range | Median FPS | Median draw adapter ms | Median pump ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Blood Gulch | 53 | 4.7–19.0 | 10.8 | 12.7 | 81.5 |
| Battle Creek | 46 | 7.6–21.3 | 11.2 | 11.0 | 85.5 |
| Hang 'Em High | 11 | 3.5–10.3 | 5.9 | 90.2 | 91.6 |
| Prisoner | 6 | 3.3–4.9 | 4.25 | 160.7 | 101.95 |

These windows sample different viewpoints and actions; they are not an A/B
benchmark. Adapter and pump times can overlap and include waits/preemption,
so they must not be added as independent CPU or GPU costs. The newer phase
timers are absent from this older log. Screenshots retain a geometry spike,
the black radar blip, and heavy slowdown in the indoor maps.

The user additionally reported AI not shooting after receiving the campaign
pistol. This collected session contains multiplayer maps, so it does not
diagnose that report. A subsequent local cafeteria capture preserves movement,
melee and weapon effects for separate investigation. The checked cheat flags
are zero; no AI fix or confirmed cause is claimed.

The combined September 6 candidate was subsequently installed by overwriting
only the existing 32,918,474-byte executable allocation. All 664 checked other
files are unchanged. Direct readback and a separate unmount/remount hash check
both match `7067df1e3daff612718c04339b6e45c7ad07541f39c7a24ade9fe3e1156e20c3`.
The card is safely unmounted. Details, native executable, VPK and source are in
`2026-09-06-075407-bridge-usb-candidate`; new hardware gameplay results are pending.
