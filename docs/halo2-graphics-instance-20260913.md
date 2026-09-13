# Halo 2 framebuffer configuration and instance RAM

Native attempt 13 reaches PFIFO table setup after reading framebuffer geometry
and completing a real GPU instance-memory claim. It still stops before command
processing, device return or any graphics output.

The read-only virtual memory configuration describes four 32-bit partitions,
one rank, nine column bits, eleven row bits and four banks per partition:
64 MiB, consistent with the exposed memory amount. It returns CFG0 `3`, CFG1
`0x01039000` and PBUS FBIO_RAM `0` (DDR). These are explicit virtual settings,
not captured retail register values. Geometry-changing writes are rejected.
The fields are described by the primary
[Envytools memory register database](https://github.com/envytools/envytools/blob/master/rnndb/memory/nv10_pfb.xml)
and pinned
[Cxbx NV2A register definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/devices/video/nv2a_regs.h).

The H2-only `MmClaimGpuInstanceMemory` wrapper reserves physical
`0x03FE0000..0x03FF0000` before guest kernel initialization. A claim shrinks this
64 KiB reservation from its lower end, keeping its upper guest address
`0x83FF0000` stable and returning padding `0x10000` through the second argument.
Queries (`0xFFFFFFFF`), repeated claims and page rounding work; regrowth and
invalid requests are rejected. The bounds and API contract follow the pinned
[Cxbx memory manager](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/memory-manager/VMManager.cpp)
and its
[retail layout constants](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/init/CxbxKrnl.h).

This matters because the shared runtime's three-MiB kernel arena overlaps those
addresses. New optional fixed reservations prevent kernel allocations from
reusing live GPU bytes. Bounds, alignment and the full reservation type are
checked before reserving/releasing any units; an ordinary free cannot release
a fixed reservation. Released prefixes become available to that kernel pool.
The shared physical-free statistics retain the existing kernel-arena accounting;
exact retail physical-page reclamation is not claimed. CE does not invoke the
new APIs, and its existing `MmClaimGpuInstanceMemory` behavior is unchanged.

Shared host tests pass, including exhaustion around fixed ranges, rejected live
allocation overlap, partial release, mixed-ownership rejection and complete
reuse after release. Title tests exercise the export's actual stack/output ABI,
queries/repeats/rounding, failed requests and allocator isolation. Local
cache/register/timer tests, 14 Python regression tests and instance-bridge
ASan/UBSan also pass. The incremental four-job Vita build succeeds.

Native attempt 13 observes:

| Instruction/API | Result |
| --- | --- |
| `0x401D9E`: read `0xFD100200` | `3` |
| `0x401DAA`: read `0xFD100204` | `0x01039000` |
| `0x401DB6`: read `0xFD001218` | `0` |
| `MmClaimGpuInstanceMemory(0x5000, 0x5E5E4C)` | Padding `0x10000`, end `0x83FF0000`, active `0x5000` |
| `0x401E05`: write `0xFD002210` | `0x03000100`; rejected, unknown register |

The final address is PFIFO RAMHT configuration. The guest has not touched
PRAMIN through a supported bus mapping yet. The next bounded task is validating
that table's address/size/search fields, backing the claimed PRAMIN window, and
checking the constructor's table initialization. Unknown accesses remain fatal.
After the deliberate guest stop, Vita3K reported a host SIGSEGV during app
teardown. The trace distinguishes this from the explicit guest graphics stop.

All owned assets, generated C, packages and full traces remain private. The
VPK embeds owned game code/image data and must not be uploaded as a distributable
release.

SHA-256 from `private/native-milestone-13.json`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `2524b81cb03634839b49332d1e9dcec6659decc342b0795ed9caba8ca4e5ae29` |
| EBOOT | `60919a58a5ba19dcc02e6248f4826449f7426a8c7c0b69c78a50f6e8136ea4f9` |
| VPK | `d1480f06a1a64ba262ca96fbd0031baa7b20bd37b9459499cb6b5de2212604a1` |
| Native trace | `010cbefd69e90f594b389282648dcf3f0ac8c274020076261b955b5de67da594` |
