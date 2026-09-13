# Halo 2 instance and DMA object contract

The original guest descriptor builders can feed a checked host object resolver.
A private host execution probe confirms their output and ABI; this is a unit
probe, not native boot progress. Normal Vita3K boot still stops at attempt 15's
PVIDEO read. No device return, command execution or rendered frame is added here.

`gpu_objects.c` resolves a unique handle/channel pair in the already configured
4 KiB RAMHT table. It reads the guest's entries, including validity, instance and
engine fields, without creating replacement objects. This is a bounded logical
lookup for a host channel, not an emulation of hardware hash-search/cache behavior.
Missing, duplicate, malformed or unreadable matching state is rejected.

DMA decoding accepts the original driver's linear, coherent, duplicated-entry
form for FROM_MEMORY, TO_MEMORY and IN_MEMORY classes. It extracts the base frame
and byte adjustment, retains the inclusive limit, and permits only the supported
NVM/PCI unified-memory targets. Tiled, AGP and other page/control forms remain
unsupported. The layout was checked against the pinned
[xemu DMA decoder](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a.c)
and
[register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h).
The inclusive last-byte interpretation is independently used by
[nxdk's DMA surface setup](https://github.com/XboxDev/nxdk/blob/master/lib/pbkit/pbkit.c)
and agrees with the owned caller passing limit 31 for a 32-byte buffer.

Every requested access checks direction/class permission, nonzero length, object
limit and the actual physical RAM extent using widened arithmetic. Some owned
constructor descriptors have limit `0x07FFAFFF` even with 64 MiB RAM; that limit
does not make nonexistent RAM accessible. No address mask silently aliases an
invalid high address back into physical memory. All rejected operations leave
the output object/address unchanged.

## Owned probe

The private ASan/UBSan harness invokes existing generated functions `0x3FE374`
(DMA descriptor) and `0x3FE49B` (RAMHT installation), including their actual helper
functions. It provides a synthetic test miniport, stack and claimed PRAMIN window,
then validates register preservation, RET 0x10/RET behavior and descriptor state.
For handle 2, TO_MEMORY class, guest base `0x83D00234`, limit 31, it observes:

| Instance word | Value |
| --- | --- |
| Flags/class/byte adjustment | `0x2342B003` |
| Inclusive limit | `0x0000001F` |
| First page entry | `0x03D00237` |
| Duplicate page entry | `0x03D00237` |

The resolver produces physical base `0x03D00234`, permits a 32-byte write and
rejects a read. The original RAMHT installer produces a valid channel-0,
software-engine binding at instance `0x11120`. The page entry retains the base's
low offset bits alongside its flags; the decoder takes the byte adjustment from
the flags word, as the primary layout specifies.

All seven Halo 2 host executables and resolver ASan/UBSan pass. Tests also cover
handle reuse across channels, same-channel ambiguity, unreadable/malformed
entries, nonzero adjustment, permissions, last-byte access, oversized ranges,
physical-memory overflow and rejected descriptor forms. The private probe is at
`private/owned-dma-probe.c`, with separately extracted generated functions and
`private/owned-dma-probe.log`; none contains distributable game source.

## Narrow native connection still required

The audited memory initializer's software fields are:

| Miniport offset | Observed-derived value after initialization |
| --- | --- |
| +0x10 | Claimed CPU instance start `0x83FEB000` |
| +0x130 | RAMHT BAR-relative offset `0x710000` |
| +0x128 / +0x12C | RAMFC offsets `0x711000` / `0x711080` |
| +0x160 | Next 16-byte instance slot `0x1112` |
| +0x140 | Context-table instance slot `0x110A` |

These follow the native-verified 64 KiB padding and 20 KiB claim. The original
channel allocator `0x3FE43D` initializes channel 0, reserves `0x37F` instance slots
and delegates hardware setup to `0x4026CE`. That subordinate interface uses EDX
miniport, EAX scheduling value and three stack arguments, RET 0x0C. It is a
candidate for a host-channel replacement that preserves the outer allocator and
instance-object construction. It also establishes instance/context state used
by subsequent code; returning success without those effects is insufficient.

Next is a supported method consumer and explicit virtual miniport/channel setup,
with submitted/consumed/completed state kept distinct. The parser/resolver are
not linked into the normal boot until that contract exists. Unknown methods and
registers remain fatal. Shared CE runtime files and profiles are untouched.

Owned generated code and native packages stay private; a VPK embedding owned
code/image data must not be uploaded as a distributable release.
