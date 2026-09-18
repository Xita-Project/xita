# Halo 2: submitted depth clear stops at a software-method request

Native attempt 27 submits the buffer containing the original constructor's
640-by-480 depth/stencil clear. The consumer accepts its preceding setup through
GET `03C2B808`, then rejects software-method value `28`. **The clear has not
executed; no displayed frame or menu has been observed.**

## Preserved state, guarded execution

The host AV adapter now records the observed flicker-filter request (option 11,
parameter 5) and luma-filter request (option 14, parameter zero). Both require a
valid register base and null output. Other values still reject. These are
desired encoder settings available through `h2_host_av_configuration()`, not an
applied display mode. A future presenter must implement them or reject
presentation. `AvSetDisplayMode` still stops explicitly. Tests verify the void
export's preserved EAX, RET-16 contract, rejected parameters and unchanged output.

The command consumer preserves the audited constructor's non-executing setup
methods, including render enables, comparison/blend/stencil settings, texgen,
combiner factors, texture control/bump inputs and four packed color attributes.
The method-input store is an explicit whitelist, not a complete PGRAPH register
file or a draw implementation. Unknown ranges, texture resources and position
attributes that would emit a vertex reject. Raw floating-point and packed-color
bits are retained.

The 136-instruction transform-program store uses checked upload/start indices
and advances its vector index after component three. Program execution remains
unsupported. Viewport offset/scale share the same Cheops constant storage as
constant uploads. Zero-valued NO_OPERATION and WAIT_FOR_IDLE are supported; the
latter orders already synchronous work. Nonzero NO_OPERATION values reject.

Clear execution additionally checks integer depth format, disabled antialiasing,
and a fully covering first inclusion window. Exclusion/narrow clipping and
partial window configuration reject. Dithered color clears reject. The existing
surface-format, extent, DMA, host-alias and logical-depth-region checks still run
before writes. Synthetic clears verify accepted pixels and unchanged memory
after each unsupported state combination.

The state meanings and shared constant/program layout were checked against
[pinned xemu](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c),
[its definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h),
and [nxdk's constructor setup](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.c).
Those references do not establish full hardware equivalence for this restricted
consumer. Window clipping remains conservatively restricted because the primary
clear implementation itself identifies unresolved clipping behavior.

## Native evidence

Both AV configuration requests are recorded. The guest then submits PUT
`03C2BAB8`. State/program processing reaches source/GET `03C2B808`, subchannel
zero, method `0100`, value `28`, and returns METHOD_REJECTED. The earlier semaphore
release remains at one, address `03CAB000`, value 5. Clear/pixel counts remain
zero. The caller's MMIO write stops at `3FAC58`, address `FD800040`.

The private snapshot contains the pending depth/stencil clear followed by a
second completion semaphore value 7. Neither operation has executed. Inspection
of the original software-method handler and nxdk identifies value `28` as a
DXT1-noise setting request; subsequent value-9 requests update specific GPU
control registers. They must not be discarded as ordinary NOPs.

| Attempt 27 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,343,784 | `f1ec59817386c0a17a46fcb5232bbba4c8dbca298f8a9c3f0f0bbab5699035d9` |
| EBOOT | 80,076,866 | `387f114a0ddd26e2cea2dddd3957bb3b977e58c69d6f41d0ac014336b7531a4f` |
| VPK | 22,797,520 | `3cd52b9fb364b67c182580409f2d8f3a417afc69d13b8201b96fdca72e28b1df` |
| Emulator trace | 302,445 | `5b9cceabdcb77f35d29b8574405c79240065807471cb88d0a511f9b6c2abd840` |
| Boot log | 194,595 | `f1770b898a6d9b4009e82e38e9e5727b4a29ddc8ac7e79883b94876e13eb0b55` |
| Device snapshot | 9,376 | `f043aed830cf4dcfe01b5ed507aaaad63e9566cfe779e6cfb0594134eb1e6794` |
| Push snapshot | 524,304 | `fd80478906a0603a832a68beba7326af77d2ec525fa602eb8f71ca45dc588b3a` |

All twelve host executables, sixteen Python regressions and the updated command
state/runtime ASan/UBSan suites pass. Native compilation is warning-free. Full
artifacts and manifest are private in `private/native-27-artifacts/` and
`private/native-milestone-27.json`; the emulator process is no longer running.
Input hashes and generated discovery counts are unchanged from attempt 26.
Reproduction uses the [host-channel build](halo2-host-channel-20260913.md).
The diagnostic VPK embeds owned game image/code and must not be distributed or
uploaded as a release.

Next is a narrow translation of the evidenced software-method protocol, checked
against the original handler's actual reads/writes and calling contract. General
software interrupts, arbitrary register writes and rendering remain unsupported.
