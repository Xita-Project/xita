# September 5: normal solo Split Screen launch

The user requested the Split Screen Enlisted Players screen to launch a solo
match through its normal Start Game control. System Link is outside this change.

Halo 3925's host initializer at 0x9C430 sets the minimum player count to two at
0x9C4C1 (host + 0x115). A live Split Screen trace confirmed one player, minimum two,
session state two, and an unblocked lobby. The root and child tags were E36801F2
and E36901F3, the split-screen pregame wrapper and screen. Global 0x2E3630 is zero
for this route; the connected/server-list creation callback sets it to one.

`tools/patch_split_screen.py` changes only that initializer's minimum: one for
Split Screen, two for System Link. It checks an exact generated-source signature
before writing and runs after `tools/recomp.sh`. Readiness checks, controller
events, countdown, network message handling and engine/UI cleanup remain the
original functions. No force-start input is required. Empty-team restrictions
for team variants are retained; the end-to-end emulator test used free-for-all
Slayer on Blood Gulch.

`make -C recomp/host test-split-screen` compiles the user's actual generated
initializer, start-request and countdown functions. It checks the initializer's
register/flag ABI and surrounding host bytes, zero/one/two-player lobbies, blocked
map/variant child menus, countdown completion, and the unchanged System Link
minimum. The same checks pass with ASan/UBSan. Generated game bodies are confined
to ignored host build output.

The user confirmed the emulator launch works. Logs show the real Split Screen
root, one-player minimum, then gameplay with UI root zero and pause flag zero.
The force-start shortcut was not called in that run. The combined build also
includes the geometry workers, variant-signature recovery and input fixes.
The combined build was installed over USB at 22:06 CDT. An exact-length in-place
write, direct read, and unmount/remount verified the installed SHA-256
`6ad07ed12c73d5943cd32c038a547072821d025e5857a300238eaee3a9a3ed18`.
All 687 other checked files remained unchanged, and USB was safely unmounted.
Hardware gameplay and performance testing of this build are still pending.
The candidate, source snapshots and verification records are retained in
`/home/birchwoodgod/xita-backups/2026-09-05-220558-split-cpu-usb/`.

## Frozen menu background

The Enlisted Players widget definition has flag bit 1 set (flags 0x3). The
original UI builder copies that bit into widget + 0x13 at 0xD0541, then increments
the pause-widget count and sets game_globals + 2 at 0xD067D. The trace shows the
pause flag enabled in the lobby. This pauses the animated menu scene while its
controls continue to render; it is distinct from a stalled frame submission.
No change to that pause flag is included in this fix.
