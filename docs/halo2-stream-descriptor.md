# Halo 2: first stream descriptor

Native109 is a read-only terminal probe at the native108 stop. It does not accept
CreateSoundStream or change guest control flow. It captures the original call at
`37D4E2`, return `2AE692`, descriptor `005E5EEC`:

- Flags `20000000` (`NOMERGE`), maximum attached packets 2.
- Format `005E5ED8`: Xbox ADPCM tag `69`, mono, 44,100 Hz, average 24,804 bytes/s,
  36-byte block alignment, 4 bits/sample, two extension bytes, 64 samples/block.
- Original callback `220730`, context 0, null mix-bin list, output `007317EC`,
  null outer object.

The existing original format builder `21E410` constructs the mono/stereo Xbox
ADPCM and stereo PCM variants used by the stream allocation loop. Callback
`220730` accepts stream context, packet context and status, returning `0C` bytes;
it dispatches into the original game's packet owner. These are evidence for
the next adapter, not a claim of supported stream callbacks or processing.

All 34 host executables and extra modes pass. Native109 reaches the same strict
stop and one black scanout as native108. After complete terminal capture and
worker close, Vita3K crashes while stopping/reinitializing its application
session (`Unhandled SIGSEGV`, access `2008C8`). No emulator remains running. This
host teardown failure is separate from the earlier explicit guest API stop.

| Private native109 artifact | SHA-256 |
|---|---|
| ELF | `d02ca4da9c41d0cd2f296ae1e8c9ca3b4dbc8afa03e0414743f42925709484c0` |
| EBOOT | `d362deefc9c7597d37d2091591907c76065ba377d3523fc8531ad82339f2dccf` |
| VPK | `1bbc7731307c3fc98f02dee1e9e4da2ed4be5ff358fcb145078b20f661e52d28` |
| Boot trace | `30a3c6482538d9a5d4d8c9de5fa7a887598d4ec3611e192ef994976fb7ff02c6` |
| Last scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence is `../private/native-109-artifacts`, `native-109-view`,
`native-milestone-109.json`, and `native109-host-tests.log`. Use the
[DSP build selections](halo2-dsp-game-init.md); replay the exact archived package
with `python3 run_lab.py 109-replay native-109-artifacts/halo2-boot.vpk` from the
private directory in the owned `:111` lab, preserving prior evidence first.
The game-embedded diagnostic package must not be uploaded or distributed.
Native106 remains the visible intro milestone; no main menu is displayed here.
