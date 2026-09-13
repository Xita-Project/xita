# Halo 2 native startup in Vita3K — 2026-09-12

The separate `XH2B00001` Vita application executes the owned Halo 2 XBE entry
and its guest threads in Vita3K. It has passed XAPI filesystem/cache setup and
runtime initializers, reached the actual application entry, and entered D3D
initialization. **The Halo 2 title/menu has not booted.**
There is no substitute title screen, renderer or audio path in this target.

The matching executable and full extracted game tree are described in the
[initial report](halo2-initial-profile-20260912.md). The input remains pinned to
SHA-256 `03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
The native loader checks base `0x10000`, image size `0x5754C0`, and entry
`0x2D0AEE`; the preparation script verifies the complete executable identity.

## Observed execution

The first native trace created an XAPI thread starting at `0x2D0A7A` through
`PsCreateSystemThreadEx`. The guest initialized its heap/TLS and created its
own `TDATA/4d530064` and `UDATA/4d530064` directories. It wrote title/save image
files from its own loaded XBE image sections. It then exited through
`HalReturnToFirmware(2)` because raw hard-disk partition 0 was unmapped.

With a private disk-header backing file, the guest read and wrote the cache
allocation table at raw offset `0x800`. It next requested cache-partition
geometry and partition information. The generic runtime rejected these device
controls, so a title-local diagnostic volume implementation was added.

The guest then actually formatted cache partition 5: it wrote its FATX header,
FAT and empty root directory. Those bytes were validated before a directory
namespace was exposed. Its requested allocation unit was **64 KiB**, derived
from its own format header; the generic runtime's fixed 16 KiB report caused
`STATUS_UNRECOGNIZED_VOLUME`. Reporting the guest-formatted layout resolved
that check.

The next observed unresolved indirect call was `0x326216`, from the initializer
table walk in `0x2D1D15`. Inspection of this exact owned image verifies three
bounded initializer-table walks:

| Caller | Table interval, end excluded |
| --- | --- |
| `0x2D1D15` | `0x461180`–`0x46118C` |
| `0x2D1CBD` | `0x461814`–`0x46182C` |
| `0x2D1CBD` | `0x461190`–`0x461810` |

The first table then executed successfully. The next observed call was the
single callback `0x322AF8`, read by `0x2D1CBD` from image slot `0x461FA8`;
that verified singleton is included as well.

`prepare_boot.py` adds their non-null/non-sentinel targets after profile
validation. This produces 424 distinct startup roots including the XBE entry;
it does not enable the general data-pointer scan. The resulting discovery has
10,316 candidate functions and 975,087 instruction instances. These counts
describe generated coverage, not validated function boundaries or compatibility.
The target uses `xv_entry_point`; the generated `xv_game_main` metadata also
names the startup entry and is not evidence of a game-main boundary.

Native attempt 06 then completed those initializer calls and entered `0x12190`
at traced function invocation 2,090, confirming the previously inferred XAPI
application boundary at runtime. It proceeded into the `D3D` section's
initialization routine `0x3FE005`. The first strict instruction stop was
`OUT DX,AL` at `0x3FE131`, with port `0x80C0` and value `1`. The preceding
path initializes interrupts, calls `MmClaimGpuInstanceMemory`, and accesses
NV2A registers including `0xFD001804` and `0xFD600140`. This target currently
has no NV2A device model, so those MMIO accesses also lack hardware semantics.
This is a graphics translation boundary, not a reason to ignore the port
write. The next task is to identify and validate the XDK 5849 D3D API boundary
and connect appropriate shared graphics HLE without CE device-layout assumptions.

Attempt 06 used `GUEST_OPT=-O0`, with runtime code at `-O1`. Its private VPK is
19,867,336 bytes; no VPK, executable image, assets, generated code or trace
containing game-derived data is tracked in the repository.

## Build and local validation

Use the installed Vita SDK and a Python environment containing `iced-x86`:

```sh
python games/halo2_5849/prepare_boot.py /path/to/owned/default.xbe
make -C games/halo2_5849 -j8
make -C games/halo2_5849 test-host
make -C recomp/host test
python -m unittest tools.test_game_profiles tools.test_loop_branches tools.test_sse_half_moves
```

For faster diagnostic compilation, `GUEST_OPT=-O0` compiles the generated guest
code without optimization; the runtime still uses `-O1`. The default remains
`-O1`. Changing optimization requires a fresh build directory or rebuilding
the generated objects; Make does not track command-line flag changes.

All default artifacts go under ignored `local/halo2_5849/boot`. `--out` selects
another private generation directory. Set `GENERATED`, `BUILD` and `IMAGE` on
the standalone Makefile together when using another directory. This does not
use the repository's normal CE build/output or generated CE source.

The VPK contains only this target's native executable, SFO and generated image.
It reads assets from `ux0:data/xita-halo2/game` and writes its own state under
`ux0:data/xita-halo2/save`. Its trace is `ux0:data/xita-halo2/boot.log`.
The lab uses a private reflink copy of the extracted game tree; original assets
and the user's existing emulator saves are untouched.

The tested lab uses Vita3K v0.2.1 build `4074-496939b6`, OpenGL with software
rendering, and Xvfb display `:111`. All three XDG roots and the Vita3K
`pref-path` point into the private lab. Firmware `vs0`/`sa0` is referenced from
an existing lab and `ur0` is copied privately. A copied configuration at
`$XDG_CONFIG_HOME/Vita3K/config.yml` is needed as well as `-c`: this Vita3K build
validates `-r` titles before loading the explicit config. With that in place,
`Vita3K -c /private/lab/config.yml -f -w -l 1 -r XH2B00001` starts the target.
Only one process may use this lab at a time; verify shutdown after firmware
exit because the GUI process can remain alive. Preserve each attempt's log
before another launch.

Synthetic tests cover FAT16/FAT32 empty-format layouts, malformed headers/FAT/
roots, short reads, geometry/partition output lengths and guest memory page
boundaries, volume allocation units/free space, bounded raw I/O and rejected
requests. The shared host tests cover both legacy CE recovery and the explicit
disabled mode, an absent optional map adapter, and diagnostic trap handling
through both direct traps and unresolved indirect calls.

## Explicit limits

The cache volumes are configured 750 MiB virtual devices with 512-byte sectors.
Their raw storage is sparse backing files. Only formatting an **empty** volume
and exposing its empty host directory namespace is implemented. The adapter
validates the guest-written FATX metadata at dismount; it rejects raw access to
mounted or populated volumes. It is not a general FATX driver, does not mirror
later namespace writes into raw FAT metadata, and does not yet enforce a full
per-volume quota on every namespace operation. Additional raw/filesystem
operations must be implemented from evidence, not returned as successful by
default. The partition-0 backing file covers only a 1 MiB header area.

Generated success-return kernel stubs are not linked. Missing kernel functions
and unsupported guest instructions stop with a diagnostic. The optional runtime
trap callback also stops on unresolved calls instead of entering the shared
runtime's cooperative trap loop. Existing shared kernel implementations remain
partial, and ten imported kernel data exports still lack real guest objects;
the initial report lists these gaps. Reaching XAPI initialization does not
validate those APIs or the data exports.

CE tag transforms, saved-profile/variant repairs and CE map-type tracking are
explicitly disabled for this target. Their existing default behavior is
preserved for CE. No CE renderer, input/title hooks or CE function addresses
are selected by the Halo 2 build. D3D8LTCG graphics translation, title-specific
rendering and audio remain necessary future work.

The device-control layouts follow Microsoft's primary documentation for
[DISK_GEOMETRY](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-disk_geometry),
[IOCTL_DISK_GET_DRIVE_GEOMETRY](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntdddisk/ni-ntdddisk-ioctl_disk_get_drive_geometry)
and [PARTITION_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-partition_information).
FATX format fields and the required cache cluster size were checked against the
owned executable's actual formatter and its native writes.
