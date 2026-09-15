# Halo 2 vertex-array setup

Native179 passes the scalar comparison in widget/menu setup and stops at
`SET_VERTEX_DATA_ARRAY_OFFSET` (`1720`): source/GET `03B7A45C`, input
`03131000`, PUT `03B80158`. The pending incrementing packet contains 16 offsets,
followed by 16 format words at `1760`. Slot 0 is FLOAT3 at offset `03131000`,
slot 1 is packed signed 11/11/10 at offset `0313100C`, both with 16-byte stride;
other slots use disabled float arrays (component count zero).

The [pinned xemu method implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
and [register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h)
separate these state assignments from draw execution. The offset's high bit
selects DMA B and its remaining bits give a byte offset. Format words contain
type, component count and byte stride. The command state retains all offset
bits and the exact accepted format word, with a validity bit for each slot.
Only the observed float and packed types are admitted: float counts 0 through 4
and packed count 1. Other type/count combinations remain unsupported.

No array data, DMA object or guest allocation is read by these assignments.
Address validity and complete input spans must be checked by an implementing
draw backend before resource access. The current immediate movie renderer
compares the entire setup bank and its validity, so these new words cannot
silently pass an old movie contract. While a movie draw is active, its consumer
owns dispatch and rejects array setup without falling back to state storage.
Array/indexed draws and other unimplemented execution remain strict stops.

Tests cover all 16 slots in each bank, repeated writes, both DMA selectors,
unaligned/unmapped/extreme offsets, disabled arrays, each supported count,
zero/max stride and every rejected type/count combination. Exact command,
clear and guest-memory comparisons verify only the selected word/validity bit
changes; resource-read and mapping counters remain unchanged. Wrong-class,
misaligned, neighboring and draw commands reject. The active movie tests also
verify each new method rejects without mutation or resource/render callbacks.
All 44 host executables and the command-state and movie-renderer ASan/UBSan
tests pass. Private evidence is under `vertex-array/`.

The pending packet contains numerous later setup updates before a seven-slot
vertex program and a quad draw. Initial array bindings alone do not establish
the eventual vertex layout or displayed content. Pending command bytes are
read-only evidence; they are not proof of consumed commands or a rendered menu.
The next native replay captures the exact state at that execution boundary.

## Native180 validation

The next native build passes all 171 dependency-target checks using unchanged
Native179 generated code/image. Native180 consumes both array banks: all 32 raw
words and validity bits match the captured inputs. It reaches quad begin
`17FC = 7` at source/GET `03B7B424`, header `03B7B420`, PUT `03B80158`, with
one unconsumed word. The program load cursor is 7 and the seven uploaded slots
match the prior pending capture. No draw is accepted with the movie contract.

The original intro was viewed in `native-180-view/early-movie-middle.png`
(SHA-256 `3103c5d1df277fa82e31719dbbe3866f47a666a5cf3f8aac30b3d0a53adaaa8d`).
The full original map copy precedes normal Start. The transition recording
contains 77.9 seconds; the final frame remains black frame 135. **There is no
visible original main menu yet.** Native/capture processes have stopped;
only the isolated `:111` X server remains.

| Private artifact | SHA-256 |
| --- | --- |
| ELF | `c558f20c7167806a1be49b73e60baf9fd5c3b8bdf1d4bf4366781c4835565abc` |
| EBOOT | `d220679226b2c1aa32f86969cbd434589d7d8a94829c0d8a15940b91cc83571e` |
| Guest trace | `ca334d9a84aa4b255084f0bd9094e6bdb7c061bad1a4d580ccda861885189218` |
| Decoded channel | `8ef2cb740df793e5de1e8c02345f82c7c76e21913c4fbbfa3d5c8ab856406d6f` |
| Raw push | `a24b9280775e42ec78525ce9426b4de01b666fedad9688b900704fc6a12e5d38` |
| Last presented frame | `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893` |

Replay from the private directory with the owned emulator stopped:
`python3 preserve_fresh_cache.py native180-replay`, then
`python3 capture_run.py 180-replay native-180-artifacts`, then
`python3 drive_startup.py 180-replay native-180-artifacts`.
The package embeds owned game image/code and must not be distributed as a
release. Captures, generated code and artifacts remain private and untracked.
