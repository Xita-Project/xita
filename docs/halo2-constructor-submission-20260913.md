# Halo 2: initial command submission and a real semaphore release

Native attempt 24 completes the original constructor's first 640-byte submission
and writes its completion semaphore into guest RAM. **It has executed no native
clear or draw and displayed no frame or menu.** The next stop is a memory-operand
comparison reading the already modeled channel GET register.

## Supported command semantics

The opt-in host channel now keeps one persistent object per supported class:
Kelvin `97`, memory-to-memory `39`, image blit `9F`, surfaces2D `62`, and pattern
`44`. Bindings resolve the guest RAMHT handle and all four context words before
changing state. Eight subchannels can bind these objects; replacing a subchannel
cannot accidentally dispatch to its previous class. A second instance of an
already bound class, another class, or an invalid context rejects.

The smaller non-Kelvin subset consists of notifier/DMA references, SRCCOPY
operation state, null context references, a surfaces2D context reference and the
pattern color. Copies, blits and notifications still reject. Kelvin retains the
audited DMA references, three flip indices, provoking/edge state, compression
metadata, shader input bits and shadow slope. Flip execution and drawing still
reject. These are state assignments with preserved values, not completed renders.

Texgen planes, fog, eye position and transform-constant uploads use the same
192-vector Cheops context array. This preserves their actual aliasing and raw
floating-point bits. Constant upload advances the vector index after component
three; an out-of-range write rejects before mutation. The depth-compression bit
remains metadata for the canonical logical depth representation.

Method `1D70` performs a real four-byte little-endian semaphore release. It
reloads the bound DMA descriptor, verifies write permission, the inclusive
limit, physical alignment/bounds and a complete aligned host mapping, then
publishes the value with release ordering after prior synchronous work. It
does not signal a hardware IRQ or complete unsupported work. A rejected method
leaves GET at that method's value word; already executed releases are not replayed.

Primary references are pinned [xemu method implementations](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c),
[its register/context definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h),
and [Cxbx's method names](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/XbConvert.h)
for edge-flag and depth-compression enable. This implementation rejects unknown
methods instead of adopting the reference emulator's unhandled-method behavior.

## Native evidence and validation

The actual driver submits PUT `03C2B280`; the consumer finishes with GET
`03C2B280`, result COMPLETE, clears zero and pixels zero. It records exactly one
semaphore release, physical address `03CAB000`, value `5`. Guest execution then
reaches `3FC406`, `CMP ECX,[EAX+3244h]`, in original constructor `3FBC10`.
Its memory access stops at `FD003244` because the current bus lowering covers
MOV/MOVZX, while this comparison retains the checked guest-pointer path.

| Attempt 24 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,278,200 | `62431336ffca1c4d0ec54ac70c1874229282d4631549db6c18ec55b460b565c1` |
| EBOOT | 79,999,438 | `6e9eeb4af0fc76ae46e9a15c6ff5753a68d60a20107a656cf688a2827e6e2b23` |
| VPK | 22,779,518 | `284a7de3d52038120b5f013edd751d39ebbb0318bc4e9a5c492fd2ddf55d24fa` |
| Emulator trace | 301,612 | `a40a8b3c73feebf249556ded8a33526488d33f2f83c052bb6ce91ce1e85c2e56` |
| Boot log | 194,069 | `80f6be8e0c1001b61eb92f4eb99e206a7f3ec04f16ed1d0b6e63c72e2c65783b` |

Device and push snapshots are unchanged from attempt 23. Full artifacts are
private in `private/native-24-artifacts/`, with `private/native-milestone-24.json`.
The emulator reached Idle and was stopped. Input game hashes are unchanged;
build commands remain in the [host-channel report](halo2-host-channel-20260913.md).
The diagnostic VPK embeds owned game image/code and must not be distributed or
uploaded as a release.

All twelve host executables and fourteen Python regressions pass. The new command
state suite passes ASan/UBSan. Synthetic tests cover raw constant bits and shared
context aliases, subchannel rebinding, partial context reads, invalid DMA/class
references, component/vector boundaries, actual semaphore bytes, read-only DMA,
inclusive limits, mapping/alignment failures, split packets and no replay after
a later rejection. Unknown draw/blit/flip operations are explicitly tested to
reject without modifying state or guest RAM. The native build has no warnings.

The next bounded task is a tested CMP memory-read lowering through the existing
strict bus, followed by another native trace of the constructor's next boundary.
