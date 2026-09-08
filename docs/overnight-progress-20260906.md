# Overnight progress — September 6, 2026

The current CPU draw-preparation step is built and locally validated. Subsequent
work fixed several independent rendering, memory-corruption and save problems.
All validation results below are local unless explicitly noted. The user returned
and connected USB on September 6. The combined candidate was installed and
verified at 07:55 CDT, preserving the existing file allocation and all 664 checked
other files. The user subsequently confirmed good rendering on hardware; the
[new USB log](hardware-20260906-draw-prep.md) records Blood Gulch at a median
8.1 FPS, so the performance target remains unmet.

| Area | Change and evidence | Remaining check |
| --- | --- | --- |
| CPU draw preparation | Index bounds are scanned in cached CPU memory while copying into frame-owned GPU storage. Profiling separates draw setup stages. Host cases and emulator captures pass. | Measure the same route on Vita; no new FPS claim. |
| Flare draw work | Conservative screen rejection avoids 134 of 249 flare quads in the cryo test view and 21,440 bytes of immediate copies per frame. Blood Gulch turns retain plasma charge, projectiles and vehicles. | Vita appearance and cost measurement. |
| Visibility lookup CPU work | Reused query IDs check their matching result slot before searching; arbitrary IDs keep the existing fallback. Full-capacity permutations, 200,000 threaded handoffs and native cryo resume pass. | Vita cost measurement. |
| Geometry/effect storage | Corrected callback stack cleanup that overwrote model data; expanded index and immediate pools to cover the complete opening cinematic with restored flare draws. | Long Vita camera-turn and vehicle/combat sessions. |
| Flashlight | Resident vertex shader selection now preserves the vertex declaration and uses instruction addresses correctly. Cryo pod bodies stay visible through repeated toggles. | Hardware and charged plasma beside a Ghost. |
| Campaign loading/freezes | Large reads recover safely from a protected destination page rather than leaving BSP data incomplete. Full natural opening cinematic completes locally. | Hardware opening and later missions. |
| Cutscene camera | Corrected the 12-byte XNet address ABI; the old 36-byte write overwrote Halo's adjacent camera pointer. Repeated opening skips and a fresh Keyes skip return to first person with working look/movement. | Hardware. |
| Main-menu resume | Fixed a native ARM file-seek argument bug and reserved save-file growth. Save and Quit plus a full application restart resumes through Campaign. | Hardware and older damaged saves. |
| Older checkpoint recovery | An offline tool reconstructs the erased header from an exactly matching cache and rebuilds the profile CRC. The repaired Keyes/combat backup resumes the later corridor in first person, including after native Save and Quit and a full app restart. | Hardware installation/validation; other damaged saves need matching cache data. |
| Black glass | Added captured glass transmission/reflection programs. Cryo windows and pod glass show their interiors, including with the flashlight. | More glass materials and hardware. |
| Radar black spot | Restored the zero-alpha additive blip shader. Movement and plasma charge no longer create the black patch locally. | Other players/AI contacts and hardware. |
| Plasma glow | Restored discarded immediate flare quads, then replaced one-pixel visibility stubs with completed GPU counts. Idle lights and charged orb now glow. | Hardware appearance and GPU query cost. |
| Plasma projectiles | Frame-by-frame recording now shows normal bolts in flight and their impacts, plus the released charged orb and impact. | Hardware and broader projectile/trail coverage. |
| HUD damage/recharge | Grenade captures show four yellow health bars dropping to one red bar, shield depletion, then visible blue refill. Existing combiner fixes pass this fresh local check. | Hardware health and shield animation. |
| Sniper HUD | Restored the two-texture scope mask and complete filtered composite with per-texture coordinate conversion. Markings, scope border and surrounding blur appear at 2x/10x and disappear unzoomed in Vita3K; arms remain visible. | Hardware zoom and first-person arms. |
| Effect passes | Added captured blur/lighting, alpha and flare programs and corrected false render-target feedback rejection from unused textures. | Broader effect coverage; bloom/smoke are not declared fully fixed. |
| Grenade effects | Two frag explosions show flame, smoke, fading translucent dust and debris locally. The missing model-lighting pairing now links correctly during the flash; native captures retain weapons and effects. | Hardware and other smoke/bloom effects. |
| Campaign combat effects | Four captured effect/model-lighting pairings now use their complete shaders. Native corridor turns, flashlight toggles and pistol shots cover all 62 observed pairs, with no captured-data mutations or storage drops. | Hardware and other campaign materials. |
| Bridge materials | Sixteen further captured pairings add eighteen compiled fragment assets. Existing source programs are unchanged; lookup, sanitizer and native-build checks pass. Subsequent cafeteria gameplay checks 598 draws with no data mutations. | Visual coverage of each added material, two newly captured cafeteria effects, and hardware. |
| AI firing after pistol | User reported enemies not shooting after receiving the pistol. A cafeteria video is preserved; the original cafeteria script does not globally disable combat, and the checked cheat flags are off. No gameplay fix is claimed. | Trace actor firing, projectile creation and damage separately from their rendered effects. |
| Surface descriptions | Corrected Xbox's 28-byte layout; the PC layout overwrote adjacent memory and reported a 128x128 target as 0x128. Host guards and emulator viewport/zoom/campaign checks pass. | Hardware render-target effects. |

Details and test evidence are linked from [the roadmap](../ROADMAP.md). Preserved
candidates and logs are under `/home/birchwoodgod/xita-backups/`. Core roles remain:
C2 runs the ordered guest workload and draw recording, C1 submits rendering and
helps with large geometry jobs, and C0 handles eligible texture/geometry work.
This is not an even three-way split of the original single-threaded game.

The first hardware target remains a stable 20 fps. On September 6, after
installing this candidate, the user confirmed that the game renders very well
on Vita but reported little performance improvement. The collected log contains
66 Blood Gulch gameplay windows at 5.0–12.1 FPS (median 8.1). Indexed draw
preparation takes a median 9.45 ms per frame against a 118.35 ms game-side
interval. These overlapping measurements do not identify the remaining engine
cost. Prioritize guest-function attribution and separate render submission from
GPU/display waiting before expanding worker-thread work. See the
[hardware report and proposed benchmark](hardware-20260906-draw-prep.md).
Start with the same Blood Gulch settings and route, including camera turns,
flashlight/charged plasma beside vehicles, firing and a short Warthog drive.
Broader smoke/bloom coverage, AI firing after the pistol and close-wall
weapon depth remain open. No global depth change has been made from the old
shotgun screenshot without a matching original-Xbox reference.

Latest ordinary candidate: [`2026-09-06-075407-bridge-usb-candidate`](/home/birchwoodgod/xita-backups/2026-09-06-075407-bridge-usb-candidate).
It contains the native executable, VPK, a separately padded same-size USB
executable, source snapshot, hashes and validation logs. Native executable
SHA-256: `0879f69a1b903c88a16683c510ee71eaad113d60f0a1c38247a36ee2dbdafd9c`. The VPK embeds that exact executable.
The installed padded SELF SHA-256 is
`7067df1e3daff612718c04339b6e45c7ad07541f39c7a24ade9fe3e1156e20c3`.
Direct USB readback and unmount/remount hashing both match. USB is safely
unmounted. The user's latest 480p, textures 128, mip smoothing off, filter 1 and
rear touch off settings are unchanged.

The subsequent [performance diagnostic](performance-diagnostic-20260906.md)
was installed and USB-verified at 08:54–08:55 CDT. Its installed SHA-256 is
`6159534a2870ea94347284f2eb3e187f08310f46eaca6f1d1e148d2f38cea683`.
It retains these rendering fixes and adds guest-function sampling and separate
render-pump stage timing. The next Blood Gulch run identifies between-target GPU
completion waits as the dominant measured pump stage: 85.2 ms median, versus
3.74 ms submission. A ground-view sample averages 17.5 FPS. Instrumentation and
different play routes prevent treating these figures as a controlled comparison
with the ordinary build. The [queued render-pass candidate](queued-render-passes-20260906.md)
tests removing intermediate CPU waits while retaining final frame completion.
It was installed and USB-verified at 09:25–09:26 CDT:
`8c2d56a9d96796ef3676b860586a9c7083bbb14ace30b6a889bbf5c63d4f1006`.
Saves and settings match after remount; the card is safely unmounted. The
[first queued-pass hardware run](hardware-20260906-queued-render.md) confirms
the new path and records median 8.0 FPS. Route differences prevent a causal
gain claim; GPU work and visibility waits still need optimization.

The subsequent installed executable was the
[rendering and visibility candidate](render-alpha-visibility-20260906.md),
USB-verified at 11:50–11:51 CDT:
`f2f480ccf8e590f2e02bb77cecb0f0e87cb70faf65772903d40104a6b6a5c752`.
It adds detailed submission timers, 572 alpha-disabled shader variants and
cooperative visibility polling while retaining all prior rendering/CPU work.
The exact executable passes isolated Blood Gulch menu, movement, camera and
foliage/plasma checks; host and native-build checks pass. Direct device readback
and a fresh read-only mount verify the executable and 657 unchanged other files.
The card is safely unmounted. Its
[first hardware follow-up](hardware-20260906-alpha-visibility.md) records median
9.75 FPS in Blood Gulch and 8.8 FPS in Battle Creek. EndScene accounts for large
intermittent submission stalls; final graphics completion remains the largest
measured pump stage. The 20-FPS milestone is still open.

The latest installed executable is the
[scene-capacity candidate](render-scene-capacity-20260906.md), installed and
USB-verified at 12:44–12:45 CDT:
`a8c34f3dff71e6ba5e8738eed55186008ce5089c6d6b9b8ff11586015b906840`.
It requests four scenes per target with bounded driver memory and fallback to
smaller allocations, and logs EndScene cost and scene counts per target.
All prior shaders and guest code are unchanged. Host/sanitizer, native build
and isolated Blood Gulch-to-Battle Creek checks pass. Direct readback and fresh
mount verification preserve 657 other files. USB is safely unmounted; the next
hardware log must establish whether whole-frame performance improves.

The new [hardware baseline](hardware-20260906-overnight.md) and fourteen
screenshots belong to the previous September 5 executable. They cannot measure
the overnight candidate's performance or establish the cause of the campaign
AI report.
