# Halo 2 packed vector decoder

Native154 calls `28C470` from `27AB51` through global `504484`, returning to
`27AB57`. Original binders `279BA2..279BCE` and `279C6F..279CB4` select a
40-byte record at `47FB24 + format * 40`, then copy one of its two callback
triplets into `504484/504488/50448C`. The second binder selects the triplet
at offset 0 or 12; both triplets in the observed format-1 row contain
`28C470`, `28C4E0`, and `28C510`.

Preparation fingerprints both binding sequences and roots only the six
callback fields at `47FB4C..47FB63`. It requires nonnull title-code targets.
It does not infer the number of other formats, scan metadata, replace a
callback, or change the original selection/dispatch. Synthetic tests cover
both triplets, duplicate targets, adjacent data exclusion, invalid/null
callbacks, both fingerprints and unchanged source bytes. The usual whole-XBE
revision check remains mandatory. The owned row SHA-256 is
`db9a47f6841be97b45b341a50f0534b96b632fb65b41f38255ab7f18970f10e1`.

The original decoder reads four signed 16-bit components, sign-extends them
through MMX unpack/shifts, converts them with `CVTPI2PS`, sums their squared
values, and normalizes using `RSQRTPS`. Its two conversion inputs are exactly
representable as single-precision floats. Private instruction listings and
binding inspection are under `packed-decoder`; no executable bytes are
included here.

## New instruction support and limits

`CVTPI2PS` reads two signed dwords from MMX or page-aware guest memory, writes
only the low two float lanes, and preserves the upper quadword bit for bit.
It uses the existing native floating-point model used by `CVTSI2SS`. Host x86
and compiled Cortex-A9 tests check native rounding and sticky precision
flags. Halo 2's existing guarded FP environment still permits only masked
exceptions and round-to-nearest. The runtime's existing separation of MMX
from x87 remains: x87 tag/TOP transitions, pending x87 exceptions and unmasked
SIMD exceptions are not newly emulated or claimed by this change.

`RSQRTPS` snapshots all source lanes before writing any destination, including
same-register operations, and reads memory through the existing page-aware
128-bit load. Its integer Q30 approximation preserves host FP control and
status bits, treats signed denormals as signed zero, preserves/quietens NaN
payloads, and handles infinities and negative inputs explicitly. Two Newton
steps from 16 mathematically generated seeds meet the instruction's relative
error bound; this does not reproduce a specific Pentium III estimate bit
pattern. Existing scalar `RSQRTSS` and all other SSE paths are unchanged.

The primary reference is the [Intel Software Developer's Manual, volume 2,
CVTPI2PS and RSQRTPS](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).
RSQRTPS is independent of rounding control and raises no SIMD FP exceptions;
ordinary square-root/divide would not preserve those properties for all
inputs. The approximation uses integer arithmetic only. Its intermediates
are bounded below 2^62, and exponent scaling is exact.

## Validation

The 44 focused Python checks include callback, sparse-jump, profile, LOOP,
SSE half-move and packed-decoder tests. The new synthetic fixture checks
52,992 emitted full-context/memory/native-x86 FP comparisons: every register
pair, same-register source/destination, every destination with a memory
source, unaligned and split-page inputs, upper-lane payloads, exceptional
values, rounding modes, DAZ/FTZ combinations and sticky exception flags.
ASan/UBSan passes the same fixture.

All 16,777,216 mantissas across both exponent parities meet the RSQRTPS bound;
maximum measured relative error is `2.2310150416e-7`, versus the architectural
limit `1.5 * 2^-12`. Another 130,302 checks cover every finite exponent and
mantissa-bin boundaries. The separate Cortex-A9 test executes 4,608 synthetic
compiled cases with complete guest-context, memory and FPSCR checks. This
validates emitted ARM behavior; it does not establish game compatibility or
an emulator/hardware performance gain.

Reproduce from the source root (Python needs iced-x86, pyelftools and Unicorn):

```sh
python -m unittest tools.test_sse_packed_decode tools.test_halo2_callback_roots tools.test_halo2_sparse_jump tools.test_game_profiles tools.test_loop_branches tools.test_sse_half_moves
XITA_TEST_SANITIZE=1 python -m unittest tools.test_sse_packed_decode
python tools/test_arm_packed_decode.py --output-dir /absolute/private/arm-packed-check
```

Native155 uses private `packed-decoder` generated output. Its package embeds
owned game code and data and must not be distributed. The original main menu
has not yet been visibly verified; native replay results are recorded below.

Native155 passes the first decoder and reaches `28CDB0` through the same
`504484` dispatch, again returning to `27AB57`. That target belongs to another
format's callback triplets and remains a strict missing-function stop; no
result was substituted. The original Microsoft Game Studios intro is directly
visible in `native-155-view/window-first-nonblack.png` (SHA-256
`1d19de947c33aea5bd4cdab01224a458628ffd5b544332197c26a13155b3a623`).
The terminal frame135 is black and the decoded channel snapshot is unchanged
from154. No original main-menu frame is visible.

All171 build dependency targets were retargeted and then verified after a
successful build. Native155 ELF SHA-256 is
`02e19a7b35a090cb10d48efbb9b77d105f8d6a622127e1bc64fec0217cdfd5b9`,
EBOOT `be988d39e67b7c6e77f1ba6626824ae4f5016c2e4bc52b8ce1f10b2856aa7afb`,
trace `322ef9f58156cbc39fa4a8dfc98c9511486ac6ad81649491f3f55f0b6443a092`.
Private artifacts are `native-155-artifacts` and `native-milestone-155.json`;
`native-155-view/startup-drive.json` records the normal Start input. The lab
emulator is stopped and no native build remains running at this checkpoint.
Regeneration adds three original functions (11,975 total), with3,825 emitted
unsupported instructions; these are automatic metrics, not compatibility.
All44 separate Halo2 host executables also pass with the new runtime header.
