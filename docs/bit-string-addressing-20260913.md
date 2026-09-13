# Correct memory bit-string addressing

Halo 2 native attempt 36 read the correct font table but split `fixedsys-9` into
`f`, `xedsys` and `9`. The lifted CRT bitmap operations at `0x0032190D` and
`0x00321928` masked every bit index to 31 while always accessing the bitmap's
first word. Character 105 (`i`) therefore collided with tab (9), and 45 (`-`)
with carriage return (13). This was a recompiler addressing error, not missing
font assets or corrupted input: the private cache copy matched the source text
with one appended NUL byte.

Memory BT/BTS/BTR/BTC now use the signed register index to select the containing
word, then the bit within that word. Immediate indices remain modulo operand
width, as do all register-destination forms. Reads and writes use the existing
page-aware guest-copy helpers. Other flag behavior stays unchanged.

This follows the memory bit-string and immediate-offset rules in
[Intel's instruction reference, BT](https://cdrdv2-public.intel.com/868140/253666-089-sdm-vol-2a.pdf).
The fix applies to generic recompilation; existing emitted code changes only
when regenerated.

`tools.test_bit_strings` compiles and executes 1,760 synthetic cases covering all
four operations, 16/32-bit operands, positive and negative indices (including
signed extremes), immediate 255, register-destination modulo behavior, aliased
address/index registers, unaligned operands and nonadjacent mapped pages. It
checks the whole memory buffer and CPU state against a byte-level reference,
including carry, untouched flags, register high halves and stack cleanup.
Normal and AddressSanitizer/UndefinedBehaviorSanitizer builds pass. All 17
combined bit-string, Halo 2, profile, guest-pointer, LOOP and SIMD tests pass.

Native attempt 36 ELF SHA-256:
`2258550818b29494a6e01af68a9072ddeeb639e11ba3a7d2d687d9f45635e971`.
EBOOT: `db257d1e3fa93a8a9e11275399e959578f8819fe1d2221ec80c59d7c7ed6c80f`.
That attempt predates the fix. Subsequent native validation is recorded
separately; the host fixture does not establish game rendering or a main menu.
