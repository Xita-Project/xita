# Halo 2 remaining lifecycle callbacks

Native159 reaches `D4E60` through the existing 68-record lifecycle table,
field `18h`: caller `11C1B0`, indirect call `11C21B`, return `11C21D`.
The original caller iterates all records at stride `24h`, skips nulls and
passes its original transition argument. It then performs the original
state changes, related callbacks and failure handling.

Three complete caller fingerprints prove the table's remaining fields:

| Caller | Table field | Original behavior retained |
| --- | --- | --- |
| `12B690` | `4h` | Required disposal callbacks, in reverse record order |
| `11C1B0` | `18h` | Optional callbacks before the original transition |
| `138C10` | `1Ch` and `20h` | Separate optional notification walks after comparing original state arrays |

The disposal loop starts at `441748` and steps backward by `24h` through
`440DDC`. The three forward walks use offsets `0..990h` at the same stride.
The full `11C1B0` fingerprint includes its early return and the failure block
after its later return; no path is clipped at the first `ret`.
`138C10` retains both original comparisons and their conditional skips.
Discovery only compiles the referenced original callback bodies.

Field `4h` requires nonnull executable title targets, matching its
unconditional indirect call. The other three fields may be null; every
nonnull target must be executable `.text`. In the owned image each of the
three optional fields has one nonnull callback: `D4E60`, `17D5E0` and `B6DF0`,
respectively. The disposal field has 31 unique targets. The earlier table
fields and their validation are unchanged. Together the independently
fingerprinted walks now account for all nine words of these records; this
is not an unbounded data-pointer scan.

Private evidence is `action-callbacks/remaining-lifecycle-guards.json`,
`remaining-lifecycle-complete.txt`, `lifecycle-table-references.txt` and
`native159-complete-caller.txt`. Native160 uses private `lifecycle-complete`
output. Its diagnostic package embeds owned game content and must not be
distributed. No shared runtime, sound or graphics semantics change here.

All 48 focused callback, sparse-jump, profile and LOOP tests pass. The new
fixture checks all 68 slots in each selected field, required versus optional
nulls, code sections, invalid targets, ignored neighboring fields/next record,
all caller guards and unchanged image data. Regeneration adds 44 reachable
functions, 280 blocks and 1,585 instructions. Unsupported instructions remain
3,825; these counts describe automatic translation, not executed coverage.

Native160's first run visibly rendered the original Microsoft Game Studios
intro, then lost the isolated Xvfb display during the movie, before the map
copy completed or normal Start input was sent. Vita3K logged a broken X
connection followed by SIGSEGV/access `0x10`; the old display server and its
socket were absent. No guest strict stop or terminal channel snapshot was
produced. The cause of the display-server exit is undetermined. This run is
preserved privately and excluded from validation of the new callbacks.
Only display `:111` was restarted; native161 reuses the identical completed
package without game-code, emulator-binary or configuration changes.

Native161 completes the original map copy and normal Start path, executes
the newly available lifecycle callbacks, and reaches the next original
indirect call: target `2D8780`, caller `1C2690`, return `1C269B`, fixed slot
`461DF0`. The Microsoft Game Studios intro is directly visible in
`native-161-view/early-movie-middle.png`, SHA-256
`ca928fd06a5d7545fb431b84ea37f0b5eed2b9d2b6a7dd78dc0806dd155cb2f0`.
Final frame135 is black and the decoded channel state remains unchanged;
there is no visible original main menu. A private window recording also
covers the normal Start transition; it ends at the controlled emulator stop,
with 13.6 seconds captured from its 90-second request.

All 171 dependency targets were verified after the four-job build. The
identical native160/161 ELF SHA-256 is
`71294f38ac2e382e85fb27368f3412589b3ace8cd883a09bea6b00cab277ba4a`,
EBOOT `53c8024609b95cfd70c60cedf2ece0ba32d520ba4c70ad6b4895f971757ab88a`.
Native161 trace SHA-256 is
`8058e2ec9471a21d9e7991bb882a37abf6cc218afc9954d43fcc4204c9819e51`.
Evidence is private `native-161-artifacts`, `native-milestone-161.json` and
`native-161-view`. With the owned emulator stopped and display `:111`
running, replay using `python3 preserve_fresh_cache.py native161-replay`,
`python3 capture_run.py 161-replay native-161-artifacts`, then
`python3 drive_startup.py 161-replay native-161-artifacts`. The private
driver now archives an unexpected owned-process exit promptly; this alters
no guest control flow. The native build and owned emulator are stopped.
