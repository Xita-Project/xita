# Halo 2 progressive flicker-filter state

Native64 requests flicker-filter level 3 after presenting the original error
screen. [Pinned NKPatcher configuration](https://github.com/Rocky5/Xbox-Softmodding-Tool/blob/53203aa1c1272468c9374f260233f92557c9f324/App%20Sources/NKPatcher/Main%20NKP11/config.inc)
documents levels 0 through 5 and limits this filtering to interlaced video.
The existing host presenter supports only progressive digital 480p.
The adapter therefore retains valid level changes after that mode is applied;
it does not apply an interlaced filter to progressive output. Before mode
application, the existing initialization contract still permits only level 5.
Invalid levels and output-pointer requests reject unchanged.

All 18 host executables plus the timed case pass. Both runtime cases pass
ASan/UBSan. Tests verify all six levels after mode application, exact stored
state, unchanged CPU state except the specified return stack, no new display
or blanking calls, and rejection of level 6. The pre-mode rejection tests
remain in place.

Native65 retains level 3 and advances to
`AvSendTVEncoderOption(14, 1, NULL)` at return `3F8CA2`, enabling the luma/soft
filter. That setting remains unsupported pending separate semantics review.
The displayed original error screen is unchanged; there is no main menu.
A bounded read-only entry trace confirms the real first main-loop path
`12B450 -> 223240 -> 13F10`, return `22329B` at presentation. The earlier
main audio failure is statically tolerated by its caller; the reason this
error screen is selected has not yet been established. No error flag or
original control flow is changed by these diagnostics.

| Private native65 artifact | SHA-256 |
| --- | --- |
| ELF | `403155bbea06f3cfb30674824aa9ac1b4ddd95001fdfbec159ebe0729cab0200` |
| EBOOT | `163e91c5e3f26cab32c4616a41a8943d038168757c114ce6690962a88d09ba75` |
| VPK | `f72b9d0d1a19736c47ad0894fa7d52028907fa996ba20cab27362e534fe2eb24` |
| Boot trace | `cb30b5a47ac33eab30efcddf1367cab9df6a846c324fa884080e1bf9f58bfef8` |
| Last presented buffer | `a7d8be6fbe31bcc71b47ffc9d958471253719e82a3f032d29bf5af3727260abf` |

Replay from the source directory:
`python3 ../private/run_lab.py replay65 ../private/native-65-artifacts/halo2-boot.vpk`.
The private generated source remains `online-interfaces/generated`. This
package uses the explicit unavailable-audio and limited native FP probes,
embeds owned game image/code, and must not be distributed or uploaded as a
release. Captures and game-derived artifacts remain private.
