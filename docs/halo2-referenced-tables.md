# Halo 2 reference-driven function-pointer table discovery

The strict `--no-data-roots` recompiler never scans data sections for code
pointers, so every C++ virtual table in `.rdata`/`.data` had to be admitted one
at a time: `prepare_boot.py` fingerprints the caller and constructor that install
each table, validates its slots, and adds them as roots. That kept translation
bounded and verified, but each build advanced the boot by a single vtable, and
native220 had reached only the motion-object construction factory before it
stopped at an untranslated configuration method.

`--referenced-tables` generalises that step without weakening its invariant. As
the lifter decodes a function it records every *initialized-data* address the
code itself names:

- a vtable install, `mov dword ptr [reg], imm32`;
- a table address loaded or pushed as an immediate, `mov reg, imm32` / `push imm32`;
- an indexed pointer array, `[index*4 + table]`;
- an absolute call/jump slot, `call/jmp dword ptr [table]`.

Between discovery rounds each named table is walked: consecutive aligned words
are admitted as roots only while they point at executable title `.text`, and the
walk stops at the first word that does not. Absolute call/jump slots admit only
the single word they dereference. No data section is scanned on its own, so the
set of new roots is still bounded by what translated code references, and the
walked tables are written to `referenced-tables.json` for audit. The runtime is
unchanged: an indirect call to a still-untranslated address, a missing kernel
export, an unsupported instruction or an MMIO access all still stop explicitly.

The flag is opt-in (`Discovery.referenced_tables`, default off) and
`prepare_boot.py` passes it only for the Halo 2 host-channel startup target;
Halo CE and every other profile are unaffected. `tools/test_referenced_tables.py`
covers each reference form, the null-terminated walk, the code-only invariant,
unaligned/implausible rejection, the disabled-by-default behaviour and tables
first named inside a newly admitted function.

## Result

With the same `--host-channel --audio-host` inputs, one build lifts 16,925
functions / 1,536,327 instructions (from 13,590 / 1,241,245), walking 2,222
referenced tables and admitting 18,960 code words. The whole motion-object graph
that blocked native220 now constructs: the factory `311940` builds all six
motion types, the rigid-body and associated-object destructors run, and the
game advances into per-frame animation sampling.

The strict stop moves from the motion configuration setter to an animation codec
decoder:

```
[h2/blocked] guest trap address=0028C710 fn=00279860 eax=00000000 ecx=0055EEB4 esp=005E1BDC return=00284AD2
```

`28C710` is the ninth animation-codec row's first decode function. Unlike a
dense vtable, that codec table (`47FB24`) has 40-byte rows with callbacks at
fixed offsets and is indexed as `[format*8*5 + 47FB24]`; its first row begins
with null fields, so the linear code-word walk does not admit its later rows.
`game_animation_codec_roots` now extracts it: it fingerprints the two binders,
the dispatcher and the alternate caller, then walks all nine rows (every codec
row shares decode callback `28D170` at `+0x18`, so the walk runs while that
field is title code and then requires exactly nine rows) and roots every
non-null callback. With it, the boot clears `28C710` and reaches an ordinary
vtable method, `2243A0`, whose class table the reference walk has not yet named
— the next bounded step on this path.

The original Microsoft Game Studios intro remains visually verified; the last
presented frame is still frame 136 and no main-menu frame has rendered. This is
startup progress, not menu compatibility. Generated code, image bytes and all
capture/diagnostic packages remain private and outside Git.

## Note on wider translation coverage

A separate private experiment enabled the full `.data` code-pointer discovery
(the same roots Halo CE uses) alongside the referenced-table walk. It lifts
21,106 functions and boots considerably further — the real audio device is
created, thousands of stream packets are processed, and `mainmenu.map` is copied
and loaded.

That deeper path exposed a real memory-manager defect, now fixed here. The audio
adapter allocated the DirectSound device (and its buffers, streams and packet
mirrors) with `xk_mem_alloc`, which draws from the same virtual pool the game's
own `NtAllocateVirtualMemory`/free cycle uses. Traced with a device-page write
watch and `map_page` logging, the failure was exact: the game freed virtual
`0x936000`, `DirectSoundCreate` immediately reused that just-freed address for
the long-lived device, and the game then wrote through a stale pointer to
`0x936000`, zeroing the device's vtable so the identity check failed. The fix
mirrors real hardware, where kernel/DirectSound objects come from a kernel heap
distinct from the game's address space: `xk_mem_alloc_high` places kernel-owned,
game-visible objects just below the kernel area (top-down on the same 64 KB
grid), away from the game's low, reuse-prone heap. The adapter's ten allocations
now use it. With the fix the device is created at `0x3CF6008`, the watch never
fires, and the deeper path advances past the audio device to a later
stream-completion callback (`335D38`) that invokes a still-absent virtual
method. The strict no-data-roots default and every audio guard are unchanged.
