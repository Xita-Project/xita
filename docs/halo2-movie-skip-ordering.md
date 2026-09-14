# Halo 2 original movie skip ordering

Native103 confirms that the original intro cannot process skip input before
the current first-Play boundary. A normal Enter/Start key press was sent and
released in the isolated `:111` Vita3K window, without changing guest memory
or control flow. The game did not reach its next controller poll or movie-skip
predicate. This is not a skipped movie or a main-menu result.

## Original path

The intro caller `163949` invokes movie initialization `156090` with flags
`3C6`. Initialization calls the first-frame helper `1568D0` at `1564A8`, before
returning. That helper calls original Bink processing `3E2870` at `1568DA`.
The audio write callback `3E3670` commits its buffer, then invokes `3E35C0`
(return `3E36F1`), which calls Play `37B6DF` (return `3E35DB`). This is the
native102/103 checked stop, with arguments `(buffer,0,0,1)`.

The later movie update `155F80` tests bit 2 of the movie flags. If bit `100`
is also set, it requires byte `4E918B` before calling skip predicate `156AB0`.
The latter queries held-button indices `0..7,12,13` through `23E400`, across
the original controller records. Index 12 is Start: original conversion
`124C00` uses mask `10` from table byte `440C44` for that index. A positive
predicate reaches original cleanup `1565A0` and its resource-release path.
That path must execute its required audio/resource calls; it is not a launcher
shortcut or a substitute menu.

For flags `3C6`, initialization clears `4E918B` at `156113`. The enclosing map
load loop sets it to 1 only at `163A69`, after its original state transitions
out of the loading states. Native103 still has this byte zero at first Play.
Changing it would bypass the original loading/input gate and is not done.
The earlier Start-triggered dashboard path in
[native81](halo2-exit-cache-diagnostics.md) is a separate failure route.

The private original-code oracle runs movie update `155F80`, predicate
`156AB0` and controller query `23E400` for eight cases. Ready/timing queries
are isolated, and execution stops at the original cleanup entry before any
resource release. It verifies Start/A/Back acceptance with the gate open,
rejection with the gate closed or skip flag absent, and rejection of a D-pad
button not in the predicate's list. Evidence is
`../private/movie-skip-audit/original-skip-check.json`; owned bytes and the
private execution driver stay outside Git.

## Native103 trial

Only bounded read-only diagnostics were added: at most 32 movie-path entries
and 16 raw digital input changes. They preserve native FPSCR, use validated
guest reads, and never poll input ahead of the original caller or change a
guest result. The native VitaSDK build passes. No generated code changes.

The Start key is pressed at epoch `1789395075.0372577` after the original pad
initialization marker, and released at `1789395079.0375772`. The original pad
initialization reads digital `0000`. No later guest controller query occurs
before the stop, so the key injection is not claimed as an observed guest
button. The entry trace is exactly `156090 -> 1568D0 -> 3E35C0`; neither
`155F80`, `156AB0` nor `1565A0` appears. Flags are `3C6`, skip-allowed byte 0,
Bink object `80062040`, and dashboard/error-active flag `51EA00` is 0.

The buffer has two commits totaling 212,992 bytes. The real sink reports 87
silent grains, no output error, and successful terminal worker/port close.
All terminal snapshots complete. The only displayed frame remains black;
there are no original movie/menu pixels and no nonzero game audio. Vita3K's
known host fault at `2008C8` follows the completed terminal evidence during
shutdown, separately from the strict guest Play stop.

| Native103 artifact | SHA-256 |
|---|---|
| ELF | `dbe85111ad2c9af23cc9114c51ec10c0491f20ceab88f8624bbd026d493524bd` |
| EBOOT | `570cdf372bdb4daa54aa07e6417ca03a5b7e562f18a4746b24cf19335b1085fb` |
| VPK | `cbb36f51aa724a161298aade0ce4daf79732fdb13c07f74cc27fb787abbd791c` |
| Boot trace | `10cb7b7bfbe4453047337137ea663fcb7915d3105bb4ef77803341196886eb4e` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Black frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

The frozen package is in `../private/native-103-artifacts`; captures and input
timestamps are in `native-103-view` and `movie-skip-audit/native103-input.json`.
Replay from `../private` with `python3 run_lab.py replay103
native-103-artifacts/halo2-boot.vpk`, then stop only this lab with
`python3 run_lab.py stop`. A normal Start press after the pad-initialized marker
reproduces the tested input attempt; exact timing does not remove the original
Play dependency. The diagnostic package embeds owned game code/image and must
not be uploaded as a distributable release.

The next bounded implementation is real Play plus a cursor derived from mixer
and sink progress, followed by another original skip trial once that predicate
is reachable. The current explicit effects-unavailable diagnostic also retains
the separately documented [game sound-initialization limitation](halo2-effects-image-audit.md).
