# Halo 2: startup object callback discovery

Native146 completes the original menu effect-data calls, then reaches an
untranslated indirect call at `234E2C`: slot `48h` of the object returned by
`2B72E6`. The target is `2B7289`; this stop is preserved in native146.

The original allocator requests `610h` bytes. Constructor `2B7269` calls its
base constructor and stores table `45BC60` into the object. The 32-byte
constructor fingerprint is
`80197ecb7c788fcdef80d420ace1c23bb3b5b063704d5a2de1b3f0a9a663e90c`.
Slot `45BCA8` contains exactly the native target `2B7289`.

The adjacent table starts at `45BCD0`, independently assigned by the original
six-byte store at `2B7388`, fingerprint
`9a1381c2d5cae2abe6b261ed8da05f4b4ffb50cd3f83001353654c83aab7ad4c`.
This bounds the selected table to 28 four-byte slots, 23 unique executable
targets, with no adjacent strings, padding or other object's entries included.
The 112-byte table fingerprint is
`fc782b81e604e088dfc702e9f509faa0b51fcfeac10a739eaf1890b91d65b265`.
The whole owned XBE revision guard remains mandatory.

Preparation adds those original callback roots only in the Halo 2 host-channel
mode. It does not replace their implementations or relax an API/graphics
guard. The 21 callback tests pass, including constructor revision changes,
first/observed/last slot rejection and executable-section checks. Regeneration
adds 47 reachable original functions, from 11,807 to 11,854 total generated
functions; these counts are automatic translation coverage, not runtime
validation or menu progress. All selected targets have generated bodies.

Private audit data is in `startup-widget/constructor-audit.json`,
`startup-widget/discovery-change.json`, `audio-host/native146-*-constructor*`
and `audio-host/native146-constructor-boundary.txt`. The build uses
`startup-widget` sources/image and the same DSP asset, shader artifacts and
runtime options as native146. Owned game data, generated code and diagnostic
packages remain private and outside Git; packages must not be distributed.

Native147 executes past `2B7289` after normal Start at 23:19:22.513 UTC.
The four verified effect writes still complete. The next strict indirect
target is `22F57F`, called at `253BF3` through slot `3Ch` of table `458940`;
the return is `253BF6` and ECX is `82537388`. The original target returns a
pointer to the object's embedded member at offset `74h`; its caller then
dispatches that member's slot 4. This further callback chain needs discovery
validation before another replay.

The actual SDL-window capture `native-147-view/window-intro.png` clearly shows
the original Microsoft Game Studios logo without the surrounding Vita3K GUI.
This is a direct window capture, not a reconstructed image. First movie input
and output snapshots remain byte-identical. The terminal frame 166 is black;
no original main menu is visible. The emulator has exited.

| Native147 artifact | SHA-256 |
|---|---|
| ELF | `348342a4d6da38e12fab7d2d1d6533f47af03e9cd1c8149e49cab11ac9487b0b` |
| EBOOT | `fbc9beeea34e1b2d141cf831f71877b8fad38f2497098654dfb6ab8198be8e6c` |
| VPK | `7b7da9fe05a78d70b0ebc889a95a02f55fe877ad328d2cc08d7f038afbb860b3` |
| Guest trace | `b93da33deb943ef049a1b577abb908cf41836e9923d75645e165bb1ab58518d2` |
| Channel snapshot | `4614da93565f594e355c6a4b6a8825606f985105230c7238783474240f8642c3` |

All 171 completed build dependency targets are verified, including the new
generated unit. Exact private artifacts are under `native-147-artifacts` and
`native-milestone-147.json`; replay follows the documented fresh-cache/SPIR-V
procedure with this archived package and a unique attempt label.
