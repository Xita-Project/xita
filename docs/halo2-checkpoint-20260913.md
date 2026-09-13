# Halo 2 checkpoint: native attempt 45

**No Halo 2 menu or game geometry is displayed.** The real native Vita target
runs the owned executable's entry, creates and retires the initial graphics
device, constructs its second device, loads font assets, opens the real Vita
controller and enters game initialization. The only presented framebuffer is
the original black startup buffer. Draws, texture binding and recurring
presentation still stop when unsupported.

There are two distinct startup results. The normal host-channel profile stops
at MCPX audio hardware (`FE801100`, function `384A36`) in preserved native 40.
The explicit `--audio-unavailable` diagnostic returns the real unavailable-driver
error at public sound creation; the game follows its own cleanup/error path and
continues without an audio object. Attempts 41–45 use that diagnostic. They do
not establish working audio.

## Final original-code milestone

Native 45 includes only the three additional initializer fields proven by
native 44: `4674A0 -> 115D10`, `4674D8 -> 116A10`, `467510 -> C3A40`.
The loop at `D486D..D4885` uses three records, stride `38h`, and skips nulls.
The whole-image revision guard remains in force. Neighboring fields were not
scanned or treated as functions.

The final stop is undiscovered target `106460`, called at `10894B` within
`1088E0`, return `10894D`. The original caller constructs a descriptor chain
from 13 parent slots `468630..468664`, considers up to 16 child pointers at
record offset `84h`, links records through `C4h`, and invokes initializer `+10h`.
A private read-only inventory found 17 distinct parent/child records and five
non-null initializer fields with two distinct targets. That inventory is not
yet a validated root extractor: guest link construction, duplicates and cycles
must be checked before expanding discovery. This is the next bounded task.
Its 124-byte caller fingerprint is
`c1bf2193fbf5a7f7a8d0de9fffaaf9ee5cbe12ced08b39059af29896540348ec`.

## Exact private replay

The existing `:111` Xvfb display is idle. This command launches the exact
archived native 45 package in the existing isolated lab, using an unused run
label, without rebuilding:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay45-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-45-artifacts/halo2-boot.vpk
```

The helper stops only a Vita3K process using its own lab configuration. Use a
new run label for another replay. Its title is `XH2B00001`, data directory
`ux0:data/xita-halo2`, emulator `/home/birchwoodgod/vita3k/ubuntu/Vita3K`.
The owned game assets remain at `/home/birchwoodgod/games/halo2`.

Build provenance: `prepare_boot.py --host-channel --audio-unavailable`,
private `resource-init/generated`, Vita SDK `/home/birchwoodgod/vitasdk`,
four build jobs, guest O0/runtime O1. The built generated tree is also present
at private `host-channel/generated`. Source is the isolated Halo 2 branch,
never the original dirty checkout or CE worktree.

| Archived native 45 artifact | SHA-256 |
| --- | --- |
| ELF | `2fda4999a0164c8165e099f0b529c89db369f00e5882a7557363a909b59c4a92` |
| EBOOT | `9c0afaa80f259bbbb8370b98134f6f3efcc7edce8b2acd860431c5d2535c5c1c` |
| VPK | `a17355eac48abc703007d10880ea61c7088e1d1c7430258498f25bc3aa97a6c3` |
| Boot trace | `7edacba6f0612629ac65e00476f25fe0c4758b36d9432b774de1105fdaa4d860` |

Private evidence is under the Halo 2 `private` directory: `native-45-artifacts`,
`native-45-view/window-2.5.png` (black app window), `window-4.0.png` (after the
stop), `native-milestone-45.json`, `native45-next-boundary.txt` and
`native45-descriptor-inventory.json`. Earlier native 40 is preserved separately.
All 15 host executables and 21 Python regressions pass. The new input and
unavailable-audio ABI tests also passed ASan/UBSan during their milestones.
No native build or Halo 2 Vita3K process remains running at this checkpoint.

Owned generated code, captures and game-embedded packages stay private and out
of Git. The VPK must not be uploaded as a distributable release. No Vita/card,
hardware deployment, repository visibility or release changes were made.
