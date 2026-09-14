# Halo 2 original display-image copy

Native80 executes the original display-preservation SRCCOPY: 1,228,800 bytes
from guest 03A14000 to 0347C040, using a newly allocated buffer with a 64-byte
header. The stream completes through GET=PUT=03B745EC. The next original call
tries to select that copy with AvSetDisplayMode at 3F8BE8 and stops at the existing
initialization-only guard. The displayed image remains black; no menu is visible.

## Supported copy

Class 62 retains the explicit surface format, equal source/destination pitches
and offsets. Class 9F retains zero input/output points. SIZE is an execution
boundary, accepted only for the observed 640x480 A8R8G8B8 SRCCOPY with pitches 2560.
Original method ordering and values are preserved. Pinned
[xemu method decoding](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
and its [SRCCOPY implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/blit.c)
confirm that this format copies four bytes per pixel without alpha replacement.

Before writing, the consumer checks explicit state validity, original graph
contexts, six null auxiliary contexts, surface-object selection, both DMA
objects/permissions/inclusive limits, complete physical mappings and tile
metadata. Physical overlap, returned host-span overlap and pointer wrap reject.
Completed GXM work has already committed its RGB into guest RAM, so the copy
reads the actual original framebuffer including its preserved alpha bytes.
No scaling, clipping, format conversion, overlapping copy or other operation
is supported. Rejection leaves state and guest bytes unchanged. Completion
counters are published after the copy and serialized in the stop-only JSON.

## Validation and observed limit

All 24 host executables plus timed/active runtime cases pass. The dedicated copy
test also passes ASan/UBSan. It checks exact RGBA and surrounding-byte preservation,
all missing state/point fields, graph metadata, failed DMA reads, permissions,
one-byte-short source/destination limits, whole-span mapping failures, physical
and host aliases, pointer overflow, incompatible tiles and counter overflow.
The source alpha is deliberately varied rather than assumed opaque.

Native80 JSON reports one completed blit, copied_bytes 0012C000,
source 03A14000, destination 0347C040, surface format 0A and pitch 0A000A00.
All affected ABI readers were rebuilt after the command-state structure changed.
The native framebuffer capture has header `{960,544,3840,117}`; all 522240 pixels
are FF000000. The actual :111 window is black. This establishes copying of the
original game data, not a visible movie or menu.

The original caller 3F8AD0 belongs to display persistence. Its known enclosing
call paths prepare launch data and call 2D07F2. This suggests a title/dashboard
exit path rather than normal menu loading, but the actual parent caller and
reason need runtime capture. Do not treat a new unsupported boundary here as
proof the menu is closer. Before expanding persistence, trace the actual caller
and original mainmenu-header validation/failure path. Shared AvGet/SetSavedDataAddress
and MmPersistContiguousMemory are legacy stubs; this milestone stops before
relying on the setter/persistence stubs and does not claim reboot persistence.

| Private native80 artifact | SHA-256 |
| --- | --- |
| halo2-boot.elf | `675f9481c703f2535a306feeba1cb67eba597abdd7d1f83c579f149b5309310b` |
| eboot.bin | `9ebc6f2b71f36b137281602c8e593f845babf7e6ff14b5aeb58ae5a9fd4acbe3` |
| halo2-boot.vpk | `488edb65349fc5f7f6cdc2ef3d9e92bd58e08c3be1871576c8f34d4058e52f3b` |
| boot.log | `43c4e033366516f9997fcad52f05d319e8d9928a5b45b47035cd697e02a684e3` |
| channel-at-stop.json | `a19ba0cd33c6691cf07619581a7ded853f74a84911e0c0cb10d13317272794a5` |
| last-presented-at-stop.bin | `ccb66dca7da72bd944ab00f3921cdebe36e3f8c1ede153e5e459ede927d4ca14` |

Replay the archived package from the isolated source directory:

```sh
python3 ../private/run_lab.py replay80 ../private/native-80-artifacts/halo2-boot.vpk
```

After the movie loop and menu-map header reads begin, press/release Enter
(mapped to Start) in :111, or use the private `press_start.py` helper. Timing
changes the buffer parity and completed-frame counts. Frozen generated source
is still `private/inline-queue-status/generated`; no guest regeneration was
needed for this copy. The VPK embeds owned code/image and must remain private,
never an uploaded distributable release. Owned assets and all generated code,
shader data, buffers and emulator captures remain outside Git.
