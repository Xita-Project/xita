# Halo 2 graphics master control and clocks

This follows native attempt 10's strict read failure at `0xFD000200`, instruction
`0x401C43`. It extends only the opt-in, executable-hash-checked Halo 2 graphics
adapter. It does not replace the guest device constructor or enable a renderer.

The master-control enable register now retains guest writes. Disabling the
modeled timer/display units clears their interrupt-enable state and makes their
register ranges inaccessible; framebuffer register access is similarly gated.
The virtual loader initially exposes those three units (`0x01110000`). This is
an explicit HLE boot configuration, not a recorded retail reset state. Other
engine bits are retained without implementing their engines. Reserved-bit
readback has not been validated against retail hardware. Unknown registers
remain fatal even after a guest writes `0xFFFFFFFF` to the enable register.
[Envytools documents the engine gate/reset behavior](https://envytools.readthedocs.io/en/latest/hw/bus/pmc.html).

The master interrupt-enable register can be read or disabled. Requests to enable
interrupts are rejected because delivery is not implemented. Existing timer and
display interrupt-enable requests retain the same restriction.

The read-only clock configuration is internally consistent with a 16,666,666 Hz
reference and the formula `reference * N / 2^P / M`:

| Register | Offset | Coefficient | Decoded frequency |
| --- | --- | --- | --- |
| NVPLL, core | `0x680500` | `0x00011C01` | 233,333,324 Hz |
| MPLL, memory | `0x680504` | `0x00000C01` | 199,999,992 Hz |
| VPLL, video | `0x680508` | `0x0003C20D` | 31,089,742 Hz |

The register offsets and divider fields come from the pinned
[xemu NV2A register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h).
The core/video coefficients match its
[virtual initial configuration](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a.c).
The memory coefficient is an explicit virtual approximately 200 MHz setting;
it is not copied from xemu's zero-M memory reset value or claimed as a retail
capture. Clock writes are rejected. No display timing or PLL lock event is
claimed by these read-only values.

Halo 2 reads the N divider as a byte at each coefficient's `+1` address. The
adapter now routes memory `MOVZX` into a 32-bit general register through checked
byte/word bus reads. Register reads extract little-endian lanes and reject
unaligned halfword/word MMIO. Normal x86 memory keeps its existing unaligned
and cross-page behavior. Other instructions retain the existing checked pointer
path, so unsupported ways to access NV2A still stop.

Validation uses the synthetic generated-code bus test and the title-local
register test. Cases include destination/address aliasing, unsigned high-bit
bytes and words, arithmetic flag preservation, normal cross-page halfwords,
engine reset/gating, unknown engine registers after master enable, immutable
clock values, all four byte lanes, maximum P divider, zero M, and rejected
width/alignment accesses without output mutation.

Native attempt 11 completed the original `0x401C33` clock-reading function.
The observed sequence includes master enable `0x01110000`, master IRQ enable
`0`, guest master-enable write `0xFFFFFFFF`, then MPLL `0x00000C01`/byte `0x0C`,
VPLL `0x0003C20D`/byte `0xC2`, and NVPLL `0x00011C01`/byte `0x1C`.
It stops at instruction `0x3FE1FC`, writing timer numerator
`0xFD009200 = 0xDE86`, with unknown-register reason 1. The constructor has not
returned; no frame or menu has rendered. The owned Vita3K process was stopped
after preserving its complete trace and private artifacts.

The 14 Python regression tests, local cache/register tests, and register
ASan/UBSan run pass. Native generation and the four-job Vita build pass.
Changes are limited to the title's bus, adapter, register model, their tests and
this report; shared CE paths have no changes in this milestone.

SHA-256 from `private/native-milestone-11.json`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `9f8c639dd5c48220fc6d53016bb48448d3b6af1fdc3f0269f28bed8ec029b8e5` |
| EBOOT | `d5d8b9f5274af737020b5527192cafb185e6fe65d8de80b5e43ea062a38ca143` |
| VPK | `bc8e90a7d01d2d2b6a222d255c3f7d9517e148fff21e60e5dbd94f4e07e353be` |
| Native trace | `55e13a5f8b7886f797900f0fe0d468d1d3b5f47a81a1a1cae56ca69e35b6a709` |

The next bounded step is PTIMER rate/alarm semantics with deterministic host
tests, followed by another native trace. GPU instance-memory allocation,
PFIFO/PRAMIN setup and display register accesses are further static dependencies;
they are not yet observed as passed. Owned executable bytes, generated C and
packages remain private. The diagnostic VPK embeds owned game code/image data
and must not be uploaded as a distributable release.
