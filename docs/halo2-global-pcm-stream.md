# Halo 2: global 8 kHz stream ownership

Native132 creates the original global mono16/8000Hz stream and removes its
headroom, then stops at the first Process call `37AD25`, return `33610E`.
**The frame remains black and no nonzero game audio or main menu is shown.**
This follows [the two muted PCM voices](halo2-muted-pcm-gp.md).

Native131 captured the original call without changing execution: `37D835`,
return `335ED8`, descriptor `005E5E34`, output `8245590C`. The format is PCM1,
mono16, rate8000, average16000, alignment2, no extension. Flags are40000000,
packet limit2, callback `335D82`, context `824558F4`, route27 at unity.
The original caller `335DB0` constructs these exact values and puts the output
at context+24. The flags match accurate notification in the pinned
[Cxbx type definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/XbDSoundTypes.h).
Their timing behavior is not implemented by this creation milestone.

The global wrapper takes descriptor/output and returns with RET8. It obtains
a temporary singleton reference, calls internal creation `37D1F5`, then
releases the temporary reference. The private oracle runs110 original
instruction addresses per success/failure case with only internal creation
isolated; output and net references remain unchanged on failure. Its87-byte
fingerprint is `51775a4ae6d24d39db3ab4f2b8b66a190acba4bfffeed8f358aa4738dde0fd2e`.
A second oracle executes187 original parameter-constructor addresses, without
allocation or MMIO, confirming stored flags, format, route27, headroom600,
packet limit and callback/context. It does not emulate packet playback.

The adapter reuses real stream allocation and lifetime with exact separate
caller/format/route checks. It preserves the flags and route in owned state;
packet processing and callbacks remain strict stops. Inputs, mapping, output
aliases and allocation overlap are checked before mutation. The stream owns
one real shared-mixer voice and one parent reference. Headroom0 updates that
voice's real gain. No silent substitute buffer or invented callback is used.

All41 host executables and28 Python tests pass. The stream fixture covers full
ABI/control state, cross-page unaligned descriptors, each changed format or
route field, caller/context and output aliases, allocation/mixer exhaustion,
unchanged failure output, parent/child references, headroom and final release.
A synthetic direct feed exercises the actual mono16/8000Hz decoder and gain;
this is not a guest Process or GP27-routing test. Stream ASan/UBSan passes.

Native132 owns stream `015F6000`, voice84, parent references153. Its next call
is Process with packet `005E5EA4` and null output packet, ESP `005E5E64`.
The original caller prepares two320-byte packets; packet contents and
completion timing require the next read-only capture and original audit.
Terminal worker close and all snapshots complete.

Only generated `code_106.c` changed;131 files were compared byte-for-byte
before reuse. All169 dependency targets were retargeted and verified. There
are no shared CE changes.

| Artifact | Native132 SHA-256 |
|---|---|
| ELF | `2ab46270b377efe3b84b157285583c44caa4f6420865d7795dfa5523cf70b1d9` |
| EBOOT | `530a627e258fe464a64150c87e7335e59f1f9d95651aa609b797b649b5439501` |
| VPK | `4a5b81216746553a1a9af047507731befc586c2ef3bfdfa5a140ba79c8968b3d` |
| Guest trace | `c1303649e50214b9ad6de7b60d2aba0bb208f92c78bfa21a27c6653a9ec9a492` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: native131/132 artifacts/views/manifests,
`audio-global-stream`, `audio-host/global-stream-*-original.*` and
`dsp-bringup/audio-global-stream-*`. Replay from the private directory:

```sh
python3 run_lab.py 132-replay native-132-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses the existing real DSP/spatial/filter and diagnostic multibin options
with `audio-global-stream/generated`, image and build. **The diagnostic
package embeds owned game content and must not be uploaded or distributed.**
