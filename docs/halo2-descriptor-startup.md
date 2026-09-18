# Halo 2 descriptor initialization

The native45 stop at `106460` comes from the original descriptor initializer
walk at `1088E0`. The revision-checked preparation tool now models that bounded
walk before adding its initializer roots. The guest still executes the original
linking code and every original callback.

The 124-byte caller fingerprint is
`c1bf2193fbf5a7f7a8d0de9fffaaf9ee5cbe12ced08b39059af29896540348ec`.
It reads 13 parent slots at `468630..468664`, visits at most 16 child pointers
at parent offset `84h` until the first null, and links records through `C4h`.
A child is appended only if its current next pointer is zero; parent writes
can subsequently overwrite links. Shared children and self references therefore
require the original write order rather than simple graph deduplication.

Extraction validates each complete aligned `C8h` descriptor against raw
`.data`, rejects overlapping records and nonzero initial links, and validates
non-null initialization fields at `+10h` as `.text` code. It checks the resulting
chain for cycles before returning callbacks. It reads no recursive child tables
and changes no image bytes. The owned image produces 17 nodes and five callback
invocations: `175F40` four times (its original body is `RET`) and `106460` once.
Only `106460` was missing from the previous generated discovery.

Synthetic tests exercise shared/self children, repeated callbacks, original
order, first-null termination, the full 16-child bound, nonrecursive children,
unchanged input data, malformed spans, overlapping records, invalid callbacks,
nonzero initial links and changed caller fingerprints. The private preparation
output includes `descriptor-initializers.json` for comparison with native logs.
The diagnostic boot harness records the guest chain once at its first original
callback, using bounded reads of the pinned descriptor array.

This milestone does not implement drawing, audio hardware or a substitute menu.
The strict normal profile and the explicit unavailable-audio diagnostic remain
separate. Owned generated code, traces and game-embedded packages stay private;
the package must not be uploaded as a distributable release.

## Native 46 result

The actual native log contains all 17 descriptors in precisely the statically
modeled order, with every callback and next pointer matching. The original
`106460` runs and startup proceeds through five further memory allocations.
It then stops at missing target `235486`, dispatched from `147736` in `1476C7`,
return `147738`, object `54D62C`, vtable `4599A8`. This is the next original
initializer boundary, not an unsupported graphics command. Device/push snapshots
remain byte-identical to native45. The actual display capture is still black.

Private artifacts: `native-46-artifacts`, `native-46-view/window-2.5.png`,
`native-milestone-46.json`, `native46-next-boundary.txt`. Generation is preserved
at `descriptor-chain/generated`. Validation: 15 host executables and 25 Python
regressions pass, including eight callback-root tests. The emulator process was
stopped after capture; no hardware was touched.

| Native 46 artifact | SHA-256 |
| --- | --- |
| ELF | `bb5bfea3ba330b3aef34c76cbb21bb5b49755b593a565ba77bc0a461c33af5c1` |
| EBOOT | `6f9710b32ba0e22d7bd5967f977b34b88ddbc6a7ebb88bba8e83ab760ef09b45` |
| VPK | `692b0fe5b2c61916eb5563340129c9b9e8cbe202c81ca8c3e5a1d5639fe71681` |
| Boot trace | `780258891c7add5746f0587387ab8098906a6767ad64e5aae00b7104bd7344de` |

Replay with a fresh label using the private lab helper:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay46-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-46-artifacts/halo2-boot.vpk
```
