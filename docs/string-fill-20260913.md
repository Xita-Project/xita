# Bulk guest memory clears — September 13, 2026

Repeated word and dword stores containing one repeated byte now use `memset`
within each mapped guest page. This includes zeroing and `0xFFFFFFFF` fills.
Previously only byte stores had a bulk path; word/dword fills translated and
checked every element separately. The change is in the shared x86 runtime and
does not require regenerating a game's lifted functions.

The destination is still guest memory. Each bulk operation stops at a page
boundary because adjacent guest pages can map to unrelated host addresses.
An individual word/dword crossing that boundary uses the existing split-write
helper. Backward and nonuniform fills retain scalar stores. Single-instruction
stores, register updates, flags and the memory-watch call retain their semantics.
This changes neither GPU buffer ownership nor the scheduler.

`python3 tools/test_string_fill.py` passes 49,152 fixtures with an independent
byte-level reference, both optimized and under ASan/UBSan. Comparisons cover
the entire CPU context and arena, widths 1/2/4, all supported repetition modes,
both directions, eight patterns, empty and large counts, unaligned page splits,
aliased mappings and 32-bit address wrap. The existing host runtime suite also
passes.

`tools/test_arm_string_fill.py` executes the old and new production function
bodies compiled for Cortex-A9, checking 504 additional cases. A forward clear
of 150 dwords falls from 4,073 counted instructions to 78 plus one modeled
600-byte `memset`. These are instruction counts, not CPU cycles: the fixture
models imported memory routines and disables the unrelated watch logger.
This is not a universal speedup. A nonuniform 150-dword fill increases from
4,073 to 4,237 instructions, and tiny/byte-only fills incur extra dispatch work.

A private Vita3K census confirms gameplay coverage. Over Blood Gulch frames
7,304–7,908, all 291,627 repeated word/dword calls qualify for the new path:
about 483 calls and 33,453 bytes per frame. This does not count single stores
as eligible; eligible calls can include zero repetition counts, so this is
not a count of actual `memset` invocations or a hardware timing measurement. The instrumented
build renders Blood Gulch, camera movement/firing, the campaign cryo room and
menu transitions. Diagnostic counters are absent from the final build.

The native build passes package integrity checks and its embedded EBOOT matches
the archived executable. That exact build also passes normal solo Blood Gulch
startup, camera movement/firing, movement and pause/leave in Vita3K. Final EBOOT
SHA-256:
`311b01a424ccf6483372189ca6bc95ff308b9de95d222c7bde0513ef63000e9e`.
Vita3K logs write-protection/SIGSEGV messages while continuing to render and
return to the menu; the preceding candidate logs the same message types.
This is a functional smoke check, not a clean emulator-error-log claim.
The private combined candidate also enables the previously tested object-basis
and model-palette experiments. Evidence is under `audit/string-fill/` in the
phase-followup directory. At that validation stage, no Vita write was attempted.

The September 13 [hardware capture](hardware-20260913-gameplay.md) now confirms
this executable is installed and runs Blood Gulch. The native object-basis and
model-palette experiments were compiled in but disabled in the retained runtime
configuration. The capture compares the existing vertex worker, not this bulk
clear change against its predecessor; its individual hardware benefit remains
unmeasured.
