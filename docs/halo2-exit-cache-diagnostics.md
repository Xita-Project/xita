# Halo 2 original exit and cache diagnostics

Native81 confirms that pressing Start calls 2238F4 from 21347F with three zero
stack arguments, then 22376B(0) -> 3F8AD0. The enclosing original 2133A1 routine
checks the active flag at 51EA00 and controller inputs before requesting this
launch path. Execution still stops at AvSetDisplayMode return 3F8BE8 before the
legacy saved-display/persist setters. This is an exit route, not menu progression.

The bounded boot probes only read original mapped state. They preserve native
FPSCR and do not modify guest registers, flags, headers, results or control flow.
They record up to 16 launch entries, 80 cache state changes and eight header
validations; captured owned headers remain private. No generated code changed.
The VitaSDK native build passed; native81 reproduced the existing checked stop.
The displayed capture is black. These are diagnostic observations, not a new
filesystem or rendering implementation.

The first three 2048-byte mainmenu headers differ from the owned file only at
byte offsets 32..51 (the original game writes its path) and 332..339 (the original
game writes its timestamp). All other bytes match. The original read reports
2048 bytes and the state machine proceeds through states 4, 5, 6, 7 and 8.
It selects cache slot 2, whose file handle is FFFFFFFF; the attempted file-position
operation therefore fails and no cache-header bytes are read. State 17 records
result 3. This repeats three times. Header corruption is not established.

Earlier in the same run, the narrow cache bridge rejects raw access to populated
cache4. This lab already contains four 2048-byte cache-map headers and two empty
savegame files from prior original guest runs. No N: symbolic link is created;
the six original N: cache/save-file opens fail with C000003A. This is a known
raw/namespace coherence limitation, not evidence that owned maps are missing.

For native82, the stopped lab's cache4 directory was renamed intact to private
`native-81-cache4-preserved`, hashed, and replaced with an empty private directory.
The exact same native81 ELF/EBOOT/VPK then ran. Its original formatter validated
an empty FATX volume, mounted N: and created the cache files. No data was invented,
no guest return was forced, and the populated-volume rejection remains intact.
This is a controlled fresh-volume experiment; repeat boots on populated cache4
still require a coherent formatting/namespace design.

Native82 reaches an earlier movie-quad draw stop at method 17FC value 7, GET
03B4238C, PUT 03B42520. Compared with the pinned native73 pipeline, all program,
constant and validity words match; only four retained texture-palette addresses
differ (1B20/1B60/1BA0/1BE0: 03B41000 -> 037BB000). These unused palette words
currently fail the strict full-state renderer contract. The display is still
black and no menu is visible. Next: verify that these palette addresses are
irrelevant for the exact non-indexed texture/disabled units before narrowing
that contract; then continue original main-menu map loading on the fresh volume.

| Private artifact | SHA-256 |
| --- | --- |
| Native81/82 ELF | `71394f2547a09cbe4a10dc7d9a5c692014c2ab1ed6ac3138b21dd72a4247662c` |
| Native81/82 EBOOT | `98f7a0f5c9f3bfafe9c49e9ecc01814f92c620f70efbce6a289ce95e3d9b5572` |
| Native81/82 VPK | `a61e874af62249f474f61c2d9a8a3b069118f38d1ad10e14148b524d943d306b` |
| Native81 boot.log | `84b1328bf6e2c9b0d3b76f96011315dcc7ac92c882c3bae07264996b3a68106c` |
| Native82 boot.log | `c59b773f88ef0e795937ad69e96f2888d4e6e3b202d0c0b20e92aa9c6cfe8a10` |
| Native82 channel-at-stop.json | `d981130f175c01159de306557199ac9d86973ae15b5f9089fd7d1bba8a0049ee` |

Private manifests `native-milestone-81.json` and `native-milestone-82.json`
include the remaining hashes. Captures are in `native-81-view` and `native-82-view`.
Both emulator processes exited/stopped after their exact checked stop. Assets,
generated C, owned headers, caches, shaders and game-embedded VPKs remain outside
Git. Diagnostic packages must never be uploaded as distributable releases.
