# Bounded Halo 2 game initialization callbacks

Native attempt 42 executes the first game subsystem initialization callbacks
after the explicitly selected unavailable-audio failure path. It reaches the
original network-status query, then stops at undiscovered callback `662E0`
from `662F0` (indirect call `66305`, return `66307`). There is still no menu,
additional drawing/presentation, or audio. The normal profile remains stopped
at MCPX audio hardware; this later trace uses `--audio-unavailable`.

The owned initialization loop `137C84..137C97` is exactly 19 bytes, SHA-256
`44c1c20adf4bb014714a0825d601e592e668699e31671c7676a674951946da02`.
It starts ESI at zero, calls `[440DD8+ESI]`, increments by `24h`, and loops while
ESI is below `990h`. This proves 68 exact callback slots, with 49 distinct
targets in `.text`. Only those fields are read as roots; neighboring record
fields are not scanned. The whole-image and loop fingerprints are checked,
and null/non-code/wrong-section targets reject generation.

The native 42 terminal callback comes from a separate record:
`[477058]=467140`, `[467140+10]=662E0`. Its original ten-byte body calls
`66330`, sets AL=1 and returns with RET4. That target is not part of the
68-slot table and has not been added by this milestone.

Four callback-root tests pass, including exact stride/end, duplicates,
null/non-code/wrong-section rejection and a modified loop fingerprint. The
previous 15 host executables remain applicable; this change only adds bounded
discovery inputs, with no runtime/API behavior changes. Private native build
uses four jobs and guest O0/runtime O1. It was launched on `:111`, captured,
archived and stopped.

Private evidence: `game-init-callbacks.json`, `game-init-root-tests.log`,
`native-42-artifacts`, `native-42-view`, and `native-milestone-42.json`.

| Native 42 audio-error diagnostic artifact | SHA-256 |
| --- | --- |
| ELF | `021031149de68cdcca0b541be107e04650d6bf9cc8d7066cf2993c641d5d7e39` |
| EBOOT | `9295048f725acb40ace4966578130ade1e721e55d2e7c1646716fa9d8c5dfb91` |
| Boot trace | `a055a667458769f811da3ddfc40846b6a00c885a0219dbe63d9e40cfce611bf8` |

Owned generated code, captures and packages stay private. Game-embedded VPKs
must not be uploaded as distributable releases.
