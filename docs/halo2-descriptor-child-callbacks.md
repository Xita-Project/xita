# Halo 2: direct descriptor child callbacks

Native153 reaches `108A40`, which selects one of 13 catalog parents, walks
that parent's child array at `84h` until a null entry, and calls each child's
field `28h` when nonnull. The observed target is `DC4C0`, return `108A6F`.

The adjacent original walks at `108960`, `1089D0` and `108A90` use the same
child arrays for fields `20h`, `24h` and `2Ch`. All four complete functions,
plus the catalog selector `1088A0`, are fingerprinted. The selector searches
exactly 13 catalog entries. In the owned image each parent's child array ends
after two or three entries; there are 17 distinct child records.

These walks differ from the linked initialization chain. Preparation reuses
the existing descriptor span, overlap and initial-link validation, then reads
the direct child arrays for all catalog parents. It does not use the filtered
linked chain as the list of children, recurse into a child's own array, or
include a parent's callback unless the parent is itself a listed child.
Every array must terminate within its 16 slots, before the mutable `C4h`
link. Null callbacks are preserved; every nonnull callback must be title code.
Original iteration order, calls, argument handling and field-2Ch failure
short-circuit behavior remain in the executable's translated code.

All 41 callback/sparse-jump/profile/loop checks pass. Synthetic coverage checks
shared children, duplicate and null callbacks, ignored adjacent fields and
data after the terminator, no recursion, code validation, each caller
fingerprint and a full unterminated array. Extraction leaves image bytes
unchanged. Regeneration adds 28 reachable original functions, from 11,944 to
11,972 total; these are automatic counts, not validated runtime coverage.

Owned audit data is private under `audio-host/native153-child-*`; generated
output and build are `descriptor-children`. Native154 is the corresponding
replay. No game data, generated code, traces or packages are tracked. The
diagnostic package embeds owned content and must not be distributed.

Native154 passes `DC4C0` and reaches the next original indirect callback,
`28C470`, through slot `504484` at `27AB51`, return `27AB57`. The target is a
packed signed-vector conversion/normalization routine; its binding and SIMD
instructions need separate validation. Nonblack movie frames have matching
input/output, but the sampled window immediately before Start is black;
this run did not capture the visible intro moment. Native153 remains the
latest direct visible-intro capture. The terminal frame is black and the
original main menu is still absent.

All 171 completed dependency targets are verified. Native154 ELF SHA-256 is
`5695081e32024cad80f3942b0715857e18bea9e6527155a67c4d3bf7584873a8`,
EBOOT `f7e2e4e8e14786619affcd91583945b3ea9b99ecfe1248c0d002047ee4e46308`,
trace `3f3f657e73ea854193074308484ffe484552c66b64cf1f453d7f2afe20fc021a`.
Exact package, captures, shader evidence and normal Start receipt are archived
privately under `native-154-artifacts` and `native-154-view`. The owned emulator
has exited and no native build remains active at this checkpoint.
