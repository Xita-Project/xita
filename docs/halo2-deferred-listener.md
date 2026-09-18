# Halo 2: deferred listener vectors

Native107 stops at the original public listener-position wrapper `37D598`,
called from `2207BF` with `(0,0,0)` and deferred apply 1. The same game function
subsequently calls orientation wrapper `37D54E` from `220891`, also deferred.
The argument counts agree with the pinned primary
[Cxbx DirectSound declarations](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/DirectSound.hpp).

The owned position implementation `37D430` copies three words to listener offsets
`38..40` and ORs dirty bit 1. Orientation `37D3A3` copies six words to `50..64`
and ORs bit 4. Deferred calls return without invoking `37CD15`, the actual commit
path. Shutdown returns `80004005` without writing those values. The adapter stores
pending values on its real device owner, preserves existing dirty bits and does
not claim spatial processing. Only normal finite values and signed zero are
accepted; subnormal/nonfinite x87 behavior remains outside this boundary.

Immediate vector calls stop. Existing immediate scalar setters also stop before
mutation if spatial bits are pending, so they cannot silently clear unapplied
position/orientation changes. Pending vector arrays are valid only while their
respective dirty bit is present; they do not invent active listener defaults.
Live device, sink, shutdown state and complete mapped stack are checked before
any state change. Other DirectSound methods and unsupported voice routes remain
strict.

A private original-code oracle passes 864 combinations of values, dirty flags,
apply mode, IRQL and shutdown state. It executes both original public wrappers and
internal setters. Kernel synchronization is isolated; immediate commit delegation
is counted rather than implemented by the oracle. The exact callee-saved registers,
return stack and listener writes match. The tracked synthetic adapter tests cover
all supported vector positions, repeat/combined pending updates, rejected x87
representations, immediate calls, scalar commit guards, missing stack pages,
invalid device, shutdown and failed sink, with complete context/FPSCR, memory and
resource checks. All 34 host executables and extra channel modes pass; device
fixtures also pass ASan/UBSan, and all six audio-hook Python tests pass.

| Audited owned range | Bytes | SHA-256 |
|---|---:|---|
| Position wrapper `37D598` | 53 | `11f3ebc6cebe38a0556eab50c525ab2bff3db2c43b13f48203bc6fa5fcfa8a88` |
| Position implementation `37D430` | 115 | `b14f5bca30d27911e73ecbf751e8f77ceb5f6649f8670542302bf88458b54d4a` |
| Orientation wrapper `37D54E` | 74 | `c6c983ec615fe09776f47623d30b9c89230ce38602e521e8ec6a6976d4335afd` |
| Orientation implementation `37D3A3` | 141 | `d52898fc887fd41acb1fcf44f614098c87d040b846922e87700732de69038901` |

Original bytes, oracle and native artifacts remain private. The source adds only
fingerprints, explicit adapter behavior and synthetic fixtures to Git. Native106
remains the [visible intro checkpoint](halo2-visible-intro.md), not a main-menu
claim.

Native108 executes both updates: position is zero, orientation front is `(1,0,0)`
and top is `(0,1,0)`. Dirty bits become `25` hex, retaining deferred Doppler.
The next strict stop is `37D4E2` (CreateSoundStream), caller `2AE692`, descriptor
`005E5EEC`, output `007317EC`, outer null, ESP `005E5EA0`. Its one presented frame
is black; there is no new intro or menu output. Three silent sink grains are
observed before the worker closes. The owned `:111` emulator is stopped.

| Private native108 artifact | SHA-256 |
|---|---|
| ELF | `a32aac89c9c7a743b4ee909b9fa5f020dc34849c25706e15efc7bcac16eab2fb` |
| EBOOT | `93dd6a659c2983420fba0b4e00c9f893cbbaea58dddaa069f9327b5bfa995c22` |
| VPK | `d4b47e1ca5137eec20a56cf2bb9306ae5edd1662694901cbb49890ee258b033a` |
| Boot trace | `c58e73cf2ad5dcc5705e54350a9eefafc6f24f29e384108f06cd5d0ee90ecf50` |
| Last scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: `../private/native-108-artifacts`, `native-108-view`,
`native-milestone-108.json`, `native108-host-tests.log`, and
`audio-host/check_original_listener_vectors.json`, `listener-vector-san.log`.
The same [DSP game build selections](halo2-dsp-game-init.md) apply. Replay the
archived build only in the private lab with
`python3 run_lab.py 108-replay native-108-artifacts/halo2-boot.vpk` from the private
directory after preserving the previous trace. The package embeds owned code and
must not be uploaded or distributed. The next task is read-only stream-descriptor
capture, followed by audited real stream ownership and processing support.
