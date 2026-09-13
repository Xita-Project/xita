# Halo 2: logical depth resources and the first constructor submission

Native attempt 23 accepts the original color and depth allocations and reaches
the constructor's first 640-byte command submission. It stops at the second
object binding, class `39`, which the current class `97` clear consumer does not
support. **No native clear, displayed frame or menu has been observed.**

## Representation and validation

The opt-in host region adapter now accepts the observed depth request only when
flags are exactly `84000001` and both Z fields are zero. It preserves those flags
as metadata and represents the resource as canonical, uncompressed logical
Z24S8 bytes. It does not emulate physical compression, tags or bank layout.
[Pinned nxdk pbkit](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.c)
uses this same request for its 32-bit depth/stencil allocation. The interpretation
is restricted to the host renderer's supported linear format.

Before any clear mutates a resource, the runtime checks its complete mapped
extent against the region table, requires the attachment pitch to match the
region pitch and permits that depth region only as a Z24S8 depth attachment.
Using it as color, selecting Z16, crossing a region boundary, or supplying an
unmapped page fails before pixel writes. Existing physical and returned-host
alias checks still apply. The standalone clear API makes the metadata validator
optional, preserving existing callers.

The host-channel target also replaces the inherited AV capability query with an
explicit virtual target: normal-aspect NTSC-M, 60 Hz, HDTV 480p (`00480104`). It
accepts only query 6 with parameter zero and a mapped aligned output word,
preserves EAX for the void export, and consumes four stack arguments. Constants
were checked in [Cxbx's kernel definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/common/xbox.h).
Applying a display mode stops explicitly; a capabilities query does not create
a display. Other targets retain their existing kernel behavior.

## Native evidence

The original constructor requests color region `039D0000+258000`, pitch 2560,
flags zero, then depth region `038A4000+12C000`, pitch 2560, flags `84000001`.
Both are accepted. The capability query writes `00480104` at `0040748C`.

The first submission reaches PUT `03C2B280`. Class `97` binds on subchannel zero,
then method zero on subchannel one requests handle `0E`, class `39`. The strict
consumer rejects that unsupported binding with GET/source `03C2B00C`, word `0E`,
clears zero and pixels zero. The guest write stops at `3FAC58`, address
`FD800040`. Attempt 22 reached the same command boundary before the scoped AV
query was installed; attempt 23 is the complete checkpoint.

The stop diagnostic now saves a bounded private push-buffer snapshot alongside
the device snapshot. It contains a four-word header (physical base, allocation
bytes, guest cursor, reserved zero) followed by raw ring bytes. Neither snapshot
nor generated game code is tracked.

| Attempt 23 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,277,856 | `0905d47a92ea035a4aa6486341196e25798084bec5b0cd519bb4187d2e314cd5` |
| EBOOT | 79,998,902 | `911229f62207741d6e852b62e27d35c7905c57ac18f42b437e66efb30a9da94e` |
| VPK | 22,777,793 | `3879087bd97229e87a100bc26e1def3b219aa52156b4cd39f520b4a3aceed7bb` |
| Emulator trace | 301,726 | `61e0512541367f4a529eaed3de8eeabe1865bb0d325be945cfc348f78e3e66a7` |
| Boot log | 193,968 | `b33fb306b83d336fb6e1e98ae56a10d51d744ad7618a46be4d7c0d83c3c99f34` |
| Device snapshot | 9,376 | `1f5c5b39b130e5c52302785643a25b74331a116daadd633cd3acb84b5076df43` |
| Push snapshot | 524,304 | `e06bdd67d507e2a07ae959a9d07f7892685cda6d96b6cf405e9b730703d1884e` |

Private artifacts are retained in `private/native-23-artifacts/` and the machine
readable manifest is `private/native-milestone-23.json`. The input executable and
image hashes remain unchanged. The emulator reached Idle and was stopped.
Build instructions remain in the [host-channel report](halo2-host-channel-20260913.md).
The diagnostic package embeds owned game image/code and must not be distributed
or uploaded as a release.

All eleven host test executables and fourteen Python regressions pass. Updated
tile/runtime tests also pass ASan/UBSan. Synthetic tests verify depth/stencil
component preservation, row padding, metadata rejection before mutation, AV
calling contracts, mapped output validation and unsupported-mode rejection.
The native build has no compiler warnings.

The next bounded task is to support the audited object bindings and state
initialization in this submission, including its real semaphore memory write.
Unknown methods and rendering operations without an implementation must continue
to stop. Completion of these initialization commands alone will not constitute
graphics output.
