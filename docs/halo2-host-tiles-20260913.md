# Halo 2: color resource accepted; compressed depth request stops explicitly

The host-channel profile now accepts the original constructor's color-region
assignment. It then allocates its depth/stencil resource and stops at a compressed
depth request outside this checkpoint's supported subset. **No native clear,
displayed frame or menu has been observed.**

## Complete API boundaries and memory representation

Two additional full functions are fingerprinted in the already opt-in profile:

| Boundary | Calling contract | Behavior |
| --- | --- | --- |
| `3FE67F`, 491 bytes | EAX = index; seven stack arguments: miniport, physical address, bytes, pitch, flags, Z start tag, Z offset; RET 28, EAX = 1 | Validates and installs one canonical linear, uncompressed host memory region. |
| `3FE86A`, 189 bytes | EBX = index; stack arguments: miniport, clear-Z-offset request; RET 8, EAX = 1 | Removes one region. No compression offset exists in the supported subset. |

EBX/EBP/ESI/EDI are preserved. The original `3F8690` caller continues to maintain
the game's six-word tile cache at device `+1AC0 + index*24`. These are typed host
resource operations, not emulated FIFO pause, PGRAPH/RDI or memory-controller
register sequences. Unknown MMIO still stops.

The host representation stores logical linear bytes. It accepts eight region
indices, 16-KiB aligned addresses/sizes, checked physical extents, 64-byte aligned
nonzero pitches up to 64 KiB and only flag bit zero. It rejects compression flags,
Z tags/offsets and overlaps with other active regions. A mapped lease may be
outside regions or wholly within one; partial intersections are rejected.
This subset does not claim physical DRAM bank layout or compressed-byte fidelity.

The register/address and region concepts were checked against pinned primary
implementations. [nxdk's pbkit](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.c)
provides tile assignment/removal and uses the same `84000001` request for its
32-bit depth/stencil region. Its source at this commit matches the previously
inspected private copy. [xemu](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a.c)
uses shared linear host VRAM and records tile limits for blit clipping. Those
implementations inform the boundary; they do not establish complete hardware
equivalence for this smaller consumer.

## Validation before mutation

The runtime bridge validates stack argument spans, the aligned guest DMA
descriptor, the entire physical ring and the claimed instance-memory alias
before channel resource mutation. Original helper calls also require a mapped
256-byte stack work area. Tile assignment checks every cached physical-alias
page of the requested region before changing the region table. Instance reads
verify their physical alias before loading a word. Unmapped trash-page aliases,
wrapping ranges and partially unmapped spans cannot be accepted as resources.

These checks are local to Halo 2. They neither alter shared memory translation
nor assume the recompiler's optional MMIO checker validates ordinary guest RAM.

## Native milestone 21

Attempts 19, 20 and 21 observe identical constructor progress. The final run
includes the complete mapped-span checks:

1. The existing channel bootstrap completes with PUT = GET = `03C2B000`, clears 0
   and pixels 0.
2. Original allocation returns `839D0000` for `00258000` bytes. Tile assignment
   index 0 requests physical `039D0000`, pitch 2560, flags 0 and zero Z fields;
   the host region is accepted.
3. Original allocation returns `838A4000` for `0012C000` bytes. Index 1 requests
   physical `038A4000`, pitch 2560, flags `84000001`, Z start 0 and Z offset 0.
4. The adapter stops at boundary `3FE67F`, address `038A4000`, value `84000001`,
   reason UNSUPPORTED_OPERATION, before installing that unsupported region.

The live assignment trace clarifies the preceding shared-helper stop at
`3FE927`: the actual path is assignment `3FE67F`. Tile removal was another static
caller candidate, not an observed call in this constructor path.

| Attempt 21 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,277,220 | `7cd337c40998c379872974646750373b636c349337f907a96471316500bbef43` |
| EBOOT | 79,997,606 | `bb2e9489b35504c6dcae72e0835e8194fa236106e28027c955e32c7c2b00ca2d` |
| VPK | 22,776,331 | `1f63ecbac0f2a5f9270e491c59a94a4bda283a455f9fe0b4a45a05c90fcc6357` |
| Emulator trace | 300,633 | `ee7ffc9b128397a3f1fbe7812d37b4a7f504f0c54fb90475330b8417fcfabf3d` |
| Boot log | 193,488 | `78a351b19f66656fd93bd37269453ef6997ffd28a0ec00b085cf83b4b28d0c7f` |
| Device snapshot | 9,376 | `6849ea1a55bf364edbd7ae56c2d27a2e75d82124b3506cc95f891ffd333347e8` |

The original executable and image hashes remain unchanged. Private artifacts
are in `private/native-21-artifacts/` with `private/native-milestone-21.json`;
attempt 20 also retains a complete artifact set. The emulator returned to Idle
and was stopped. Build/reproduction instructions remain those of the
[host-channel report](halo2-host-channel-20260913.md). The diagnostic package
embeds owned game data and must not be distributed or uploaded as a release.

All eleven Halo 2 host executables pass. New tests cover region boundaries,
overlap and compression rejection, assignment/removal calling contracts, bad
last pages, crossing stack pages, unmapped descriptors/rings/instance aliases,
and actual clear writes inside an accepted region. The tile and updated runtime
tests pass ASan/UBSan. Fourteen Python regressions pass; the native build has no
warnings. Tests contain synthetic inputs only.

The next bounded task is to represent the observed 32-bit depth request with
logical depth/stencil values in the host renderer, while preserving its tile
metadata and explicitly excluding physical compression/tag emulation. That must
be validated before accepting the request, then followed by a trace of the next
constructor stage and the first substantive command submission.
