# Halo 2 command-to-pixel clear consumer

An owned Halo 2 Clear function now passes a private host execution probe from
original guest code, through emitted NV2A packets, to checked framebuffer pixels.
This is a host unit probe with an explicitly initialized test device. Normal
Vita3K startup remains at attempt 15's PVIDEO stop; there is no native game frame,
completed device constructor or menu yet.

The new `kelvin_clear.c` consumer binds one existing Kelvin object through RAMHT,
resolves its color/depth DMA handles, records surface/clear state and executes
synchronous CPU clears into physical RAM. It supports origin-zero, linear ARGB8
and Z24S8 surfaces, inclusive clear rectangles wholly inside the surface, per-color
component masks, and independent depth/stencil masks. Each attachment's full span
is validated before either is written. Overlapping physical or returned host attachment spans, bad DMA direction
or limits, invalid rectangles and unavailable physical mappings reject without
changing framebuffer bytes or consumer state.

This deliberately small consumer accepts no draw methods, shaders, swizzled
clears, multisampling or presentation. Surface setup values are retained as
state, but unsupported layouts reject at the clear operation. Unknown methods,
classes and different object contexts reject. Completed-clear/pixel counters
represent actual synchronous memory writes, not GPU fences or displayed frames.

The method/state layout comes from the pinned
[xemu method definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
and
[NV2A register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h).
The clear masks and inclusive rectangle convention agree with
[xemu's clear implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/draw.c).
Window clipping and out-of-surface rectangles have unresolved details in that
source, so this supported subset rejects those cases rather than guessing.

The synthetic integration test sends real packet encodings through the parser,
RAMHT/DMA resolver and consumer, then compares every RAM word, including row
padding and surrounding guards. It verifies partial color/depth/stencil clears,
last-row bounds, read-only/wrong handles, duplicate objects, unsupported layouts,
second-attachment mapping failure, physical/host alias rejection and exact parser stop position.
All eight Halo 2 host executables, clear-consumer ASan/UBSan and ARM cross-compile
pass. No CE runtime or profile changes are included.

## Owned Clear probe

The private probe loads the owned image and calls the existing generated
`0x3F9FC0` Clear function and its actual format/rounding helpers. Its supplied test
contract is a four-by-three linear surface, 64-byte pitch, one color attachment,
origin-zero viewport/scales, Count 0, flags `0xF0`, color `0x12345678`. It validates
RET 0x18 and preservation of EBX/EBP/ESI/EDI. The original function advances its
push cursor by 28 bytes and emits:

| Method | Parameter |
| --- | --- |
| `0x1D98` clear horizontal | `0x00030000` |
| `0x1D9C` clear vertical | `0x00020000` |
| `0x1D8C` depth/stencil value | `0` |
| `0x1D90` color value | `0x12345678` |
| `0x1D94` clear surface | `0xF0` |

The consumer accepts the batch and writes exactly 12 pixels with that color;
row padding remains unchanged. The ASan/UBSan run passes. This is stronger than
parsing a handcrafted clear packet, but the supplied device/ring/attachment
state is a test fixture, not evidence that the normal constructor produced it.
The generated functions, image and harness stay in `private/owned-clear-probe*`.

Next is the explicit virtual miniport/channel contract that lets the original
device allocator and inline command generation reach this consumer during normal
startup. Submitted, consumed and completed work must remain distinct, with
unknown methods fatal. Native display/GXM presentation and the game's original
Swap request still need a separate integration stage. No diagnostic-colored
screen is being substituted for Halo 2 rendering.

Owned images, generated C and packages remain private. A VPK embedding owned game
code/image data must not be uploaded as a distributable release.
