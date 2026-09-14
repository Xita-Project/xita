# Halo 2: original sound-owner dispatch roots

Native110 reaches game callback `2AE230` through `47F0D0` / `45711C` at `2AE66B`.
This is a missing discovery root, not an unsupported host API. The preparation
step now includes three bounded, fingerprinted original game vtables used by
the same sound initializer and format-owner path:

| Original object | Vtable | Bytes | SHA-256 |
|---|---|---:|---|
| `47F0D0` | `45711C` | 36 | `3390e861a1a9ebe7c9da27fbef73da9982814ad5696894b63d9aed83c665c38b` |
| `47F088` | `457140` | 28 | `cfe13e6e922ae34f6ec6ffdfae387fecb3c865fe164f75f1be70c9004d969120` |
| `47F0F0` | `45715C` | 36 | `5176d30bf8e4cea56c2a70787eed63a1704718b78f13a40bcd04edd9074be25e` |

Original `21E410` selects the PCM and ADPCM owners; `2AE660` invokes their first
slot and stores the chosen owner for later packet callbacks. Original `21E4B0`
also selects object `47F088` and calls its interface. The table boundaries stop
at the next distinct referenced vtable or following non-code string data. All
slots in the fixed ranges must resolve to executable `.text` addresses, and each
object must still point to its reviewed table. Nothing replaces these game
functions or changes their return values, arguments or state updates.

The roots are enabled only for the opt-in H2 audio-host preparation. Synthetic
tests check exact binding, full table fingerprint, bounded iteration, null/non-code
and wrong-section rejection. All 20 callback-root tests pass. Native110's tested
host audio and rendering code remains unchanged by this discovery-only update.
Automatically generated functions still include unvalidated paths; discovery
counts do not establish runtime compatibility or main-menu success.

Native111 runs the missing callback and creates the final stereo PCM stream:
82 real empty streams in total (40 mono ADPCM, 41 stereo ADPCM, one stereo PCM).
It then stops at `37D4BE`, return `220AC8`, for a buffer descriptor at `005E5EC8`:
size 24, flags `2010` (`MIXIN|CTRL3D`), zero data bytes, format, mix-bin list and
input-bin field; output `00733208`, null outer object, ESP `005E5EA0`. The existing
adapter rejects this unsupported resource type. Device references are 83, matching
the 82 stream owners plus the public device owner. The worker closes at the
terminal stop. Only the initial black frame is displayed; there is no new intro
or menu output. Vita3K later crashes during application teardown as in native109;
no H2 emulator remains running.

| Private native111 artifact | SHA-256 |
|---|---|
| ELF | `5c921b42a5e17a579e3edc96610341af1570fffbd4b1d72a10ab557d7af4c417` |
| EBOOT | `65bab528a98656e86b73885e2e7107d12a8fe0583c59325e021cf874921deeb9` |
| VPK | `7fa136a4487f3631c02bea4cd4dc7c76d8bf12cba23b7d6006fc71aaab8a5a30` |
| Boot trace | `cd1373ed8099f636cbbd8fd8be3aaba31d53ef523a184755b80f156dfa1aab6c` |
| Last scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: `../private/native-111-artifacts`, `native-111-view`,
`native-milestone-111.json`, `audio-host/sound-owner-roots-tests.log`.
Use the [DSP game build selections](halo2-dsp-game-init.md). Replay only in the
owned `:111` lab with
`python3 run_lab.py 111-replay native-111-artifacts/halo2-boot.vpk` from the private
directory, preserving prior evidence. Packages embed owned code and must not be
uploaded or distributed. The next bounded task is the observed multipass mixing
resource and its deferred setters, preserving strict unsupported processing.
