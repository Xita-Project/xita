# Halo 2 graphics startup boundary — 2026-09-12

The native Vita3K trace reaches the real Halo 2 `Direct3D_CreateDevice` call.
It still stops during Xbox GPU initialization; **there is no title/menu or
rendered Halo 2 frame**. This investigation identifies an API boundary for
future translation, not a working graphics adapter.

## Verified entry and calling convention

For the executable pinned by the [Halo 2 profile](../games/halo2_5849/profile.json),
the direct call path is:

```text
application 0x12190 → 0x12420 → 0x12490 → 0x3F5240 → 0x3FBC10 → 0x3FE005
```

`0x3F5240` uniquely matches variant 2048 of the public XDK 5849
`Direct3D_CreateDevice_4__LTCG_eax1_ecx3` signature when scanning every executable
section's raw bytes. Its 168-byte body, `[0x3F5240, 0x3F52E8)`, has SHA-256
`c664a89d2c13bfeb171af813845bcf56d38de4a68c18860e010a8f749c29b4bf`.
The owned caller and callee establish this interface:

| Location at entry | Meaning | Observed native attempt 07 |
| --- | --- | --- |
| `EAX` | Behavior flags | `0x40` |
| `ECX` | Output device-pointer slot | `0x5093B0` |
| `[ESP+4]` | Presentation parameters | `0x5E5F74` |
| Return | HRESULT in `EAX`; callee pops one argument | `RET 4` in owned callee |

The native observation reads presentation width 640, height 480 and format 6.
It does not modify registers, presentation parameters or the guest device.

The owned entry uses a static device at `0x404FE0`, stores that address into
library pointer slot `0x407488`, and clears `0x928` dwords (`0x24A0` bytes) on
failure. The guest constructor `0x3FBC10` initializes that object and performs
hardware setup; replacing only the outer return would omit those effects.

The primary sources are the pinned
[XbSymbolDatabase signature](https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/20eced544726f5558c5a408458f38a086cc4e543/src/OOVPADatabase/D3D8LTCG/5849.inl)
and the matching
[Cxbx CreateDevice wrapper](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/Direct3D9/Direct3D9.cpp).
Cxbx explicitly identifies Halo 2 as a user of this calling convention. Its
wrapper calls the original guest constructor between host setup and completion;
it does not substitute a zeroed object and return success.

## Why the CE entry cannot be bound directly

The current shared `xv_hle_Direct3D_CreateDevice` accepts six stack arguments and
returns with `X_RET(6)`. It allocates its own `0x4000`-byte device, builds host
surface state, and uses CE's callback offset `0x24E8`. Halo 2 needs the verified
register/stack interface above and a layout compatible with its guest code.

In the owned Halo 2 code, `0x3F52F0` loads the device through `0x407488`, reads
its push-buffer cursor/limit at offsets 0 and 4, and writes NV2A commands into
that buffer. Shared `CDevice_MakeSpace`, `CDevice_KickOff` and
`D3DDevice_KickPushBuffer` currently do not consume raw command buffers. The
shared runtime implements selected D3D operations, but has no general NV2A
push-buffer decoder to support this guest path.

Shared `xd3d.c` also contains CE pacing/diagnostic addresses and a CE-specific
extra function-dispatch table, while the Vita renderer's compiled shader table
comes from the selected game's shader preparation. A Halo 2 graphics assembly
must select its own device layout, dispatch, shaders and frame/input integration.
No shared CE graphics implementation has been changed by this investigation.

## Candidate inventory and its limits

The primary XbSymbolDatabase CLI was built from commit
`20eced544726f5558c5a408458f38a086cc4e543` and run privately on the owned XBE.
It reports 435 symbol/variable candidates across all libraries. These are leads,
not 435 validated interfaces. The generated symbol file remains outside Git.

For example, the scanner associates `0x3FE005` with
`CMiniport_InitHardware_4` and describes a stack `this` argument. The actual
caller passes `this` in `EAX`; its generated call has no pushed `this` argument.
That disagreement alone rules out automatically binding every scanned signature.
The scanner also suggests a swap-callback offset of `0x1DB4`; this requires
checking its consumers before using it as a device-layout contract.

The first unsupported instruction in attempts 06/07 was `OUT DX,AL` at `0x3FE131`, port
`0x80C0`, low-byte value 1. Primary
[xemu Xbox ACPI code](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/acpi_xbox.c)
places GPIO at offset `0xC0` within the power-management I/O region and models
the TV encoder field pin. The surrounding guest code also accesses NV2A MMIO.
Implementing that isolated port write would not supply the missing graphics
device, command processing or interrupt semantics.

Attempt 08 enables a target-local pointer policy for the unmodeled 16 MiB NV2A
BAR, consistent with the primary
[xemu device definition](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a.c).
It stops before the earlier MMIO read of `0xFD001804` in `0x3FE165`; the owned
instruction is at `0x3FE16B`. Application entry and the CreateDevice arguments
are still observed. The target now rejects that pointer instead of reading
the shared runtime's generic unmapped-memory trash page. It does not implement
the register or claim to validate all guest memory accesses.

## Reproduction evidence and next gate

Use the preparation and isolated launch procedure in the
[native boot report](halo2-native-boot-20260912.md). Attempt 07 used generated
guest code at `-O0` and shared runtime code at `-O1`. Its private native EBOOT
is 83,095,638 bytes with SHA-256
`a7970fe5164ac83c28e60cbdc4052d591c8176db53b3de9e4c876ad9ffb610a9`.
The private emulator log's SHA-256 is
`f5bce51b4877624b44f526af1867c2fdb5e61d415f3d8354d3a06a4d6e22d78d`.
The trace confirms application entry, CreateDevice arguments, and the strict
instruction stop. It contains no successful device-creation return.

The current checked attempt 08 EBOOT is 79,918,294 bytes with SHA-256
`b2296cf0102771626195ce2559804232dfdee5a78d4b764ac8a83b1f47d6b120`.
Its private emulator log's SHA-256 is
`6c8026eb9603d1d2a289a18d469f9972ef01567664e31c797a9327d2e1147b87`.
The guest exits at the MMIO diagnostic, and its isolated Vita3K GUI process
was then stopped. Shared host regression tests, cache-volume tests and 13
Python tests (including checked/default generated-memory execution) pass.

The next gate is a device/command translation contract that preserves the
verified LTCG ABI, initializes the guest fields its inline code consumes, and
connects submitted commands to actual rendering. Alternatively, preserving the
guest constructor requires a working Xbox GPU/MMIO model. Neither contract is
provided by the current CE assembly. Each new binding needs owned-code and
synthetic ABI/state validation before native execution can establish progress.
