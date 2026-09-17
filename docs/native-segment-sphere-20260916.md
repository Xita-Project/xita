# Optional native segment/sphere test (B0CB0)

Status: an exact candidate is implemented, OFF at both build and runtime. It is
qualified by a fresh lift from the owned image, host and ASan/UBSan differential
tests, and Cortex-A9 ARM differential tests with full FPSCR. The ARM lane found one
ARM-only correctness bug, which is fixed and re-qualified (see "ARM-only defect").
Instruction costs are reported below as an emulator distribution, not as
performance. No hardware or FPS claim is made. Startup selection belongs to root.

## Target

`0xB0CB0` (275 bytes, 93 instructions, `ret 4`) tests a segment against a
sphere. ECX points to the segment start, EAX to the sphere center and EDX to the
direction; the float radius is the stack argument. `86F50`'s edge pass calls it,
along with about 30 other functions (object movement, projectiles and others).
It had no native replacement before this change (checked by repository search).

The lifted body keeps all of its effects:

- **Stack stores:** with S = ESP−10h, float spills at `[S]` (q, then the
  discriminant), `[S+14h]` (t, overwriting the caller's radius argument), and
  three direction words at `S+4/8/C`. Each is reloaded later.
- **x87 comparisons:** up to five against the guest constant at `1F0A68`. Each
  clears C0–C3 and **ORs** the current TOP into FSW; the TOP bits accumulate.
  FNSTSW writes AX, and the final lazy flags come from `test ah,…`.
- **Four returns:** inside (AL=1), leaving (AL=0), no root (AL=0), and a
  discriminant path that returns 0 or 1 in full EAX.
- **One taken back edge:** `B0D99 → B0CF2`, with `X_PREEMPT`, when the root lies
  behind the start.

The separately emitted overlapping entry `f_000B0CCC` is unchanged.

## Candidate

`recomp/kernel/xk_segment_sphere.h` is inlined at `f_000B0CB0` entry through the
game profile (`games/halo_ce_3925/segment_sphere.py`). The hook is emitted only
when the whole-image hash and the 275-byte span hash (`f6e24841…`) match. It
computes the x87 program as C doubles, in the original operation order, and
publishes the exact final state:

- every guest store in original order;
- the x87 slots below TOP and the status word, with accumulated TOP bits;
- EAX, ECX, EDX and ESP;
- the lazy flags.

It uses the unit's captured `xram_`/`xpt_` for word accesses and the original
global-root float helpers. Comparisons use `x87_compare`'s quiet Thumb-2 VFP
sequence and operand order, so FPSCR NZCV matches.

On Thumb-2 VFP, product-accumulating additions and subtractions go through inline
`vadd.f64`/`vsub.f64` (`xv_ss_add`/`xv_ss_sub`). This prevents GCC from forming
`VMLA`/`VMLS`/`VNML*`, which the translated original never uses (see below).

- **Taken back edge:** the context is published, `X_PREEMPT` runs, and the
  original `B0CF2` instructions then run on the resumed context.
- **Return 2:** a NaN direction word publishes the exact `B0CFC` state and
  continues the lifted body there.
- **Admission** declines before any effect unless all of these hold:
  - the mode bit is set;
  - no ARM trap enables are set;
  - S..S+18h lies in one guest page, so stack reloads can be computed natively;
  - the constant's physical page differs from the stack page;
  - none of the start, center, radius or constant floats is NaN.

  With no NaN inputs, every NaN produced is a default NaN, so operand-order
  differences cannot change a payload. The negative control shows that admitting
  NaNs does change guest state.

Control (`xk_segment_sphere_control.c`): an atomic mode word, default OFF, with
`xv_segment_sphere_override`, `_enabled`, and `_calls` (take/reset admitted
calls). Either route is exact on every call, and nothing is held across the only
yield, so mode changes need no drained boundary.

Build with `XV_NATIVE_SEGMENT_SPHERE=1`. For a fresh-launch gameplay candidate,
also select `XV_NATIVE_SEGMENT_SPHERE_DEFAULT=1`. Both repository defaults remain
OFF. The mode initializer runs before any thread starts, and ordinary gameplay
does not invoke a live override. Startup and periodic passive logs report the
effective mode and cumulative admissions without resetting the counter.

Feature/default stamps track incremental builds separately. A feature change
rebuilds the hooked guest unit, control, main and archives; a startup-default
change rebuilds control and dependent archives. The generated unit explicitly
depends on the inline header because generated objects do not use automatic
header dependencies. This prevents packaging an older ON/OFF implementation.

## Validation

Receipts are under `validation/.../direct-cluster-query/claude-collision-edge/`.
The summary is in `receipt-arm-qualification.json`.

**Oracle.** `--lift` independently discovers, lifts and emits `f_000B0CB0` from
the owned XBE (image `4094e994…`) with the recompiler. It then requires the result
to be byte-identical to the staged unit's function (`code_016.c`, `bb850abf…`).
Both are `4c8e4398…`: identical. The candidate is produced by applying the game
profile's own `function_entry`/`transform_body` to that original.

The test also checks:

- span drift removes the hook;
- the interior entry `B0CCC` is untouched;
- compile-OFF preprocessing is identical;
- the unit uses the production captured-root preamble.

**Fixture** (`tools/tests/segment_sphere.c`, shared by the host and ARM lanes):

- **Values:** a random guest arena on permuted pages. Floats are drawn from a
  grid (exact boundaries), random exponents, subnormals, ±0, ±Inf,
  quiet/signaling NaN payloads and FLT_MAX, and the constant sometimes changes.
- **Aliases:** direction = S, S+4 or S+10h; start = S+14h; center = S+8; a
  direction physically aliased to the stack page; the constant page aliased to
  the stack page; a page-crossing start.
- **Stack frames:** frames at page ends, and frames whose word stores straddle
  a page.
- **Budget and yields:** most cases start with a budget of 1, so the taken back
  edge yields. Yield callbacks hash the full context and stack, then change EAX,
  ECX, FSP, x87 slots, FSW, lazy flags, the budget and a stack byte.
- **Comparison:** each case runs three lanes (original, candidate ON, candidate
  OFF). It compares the full context, the whole 256 KiB arena, yield observations
  and the expected admission count. The host lane also compares floating-point
  exception flags across four rounding modes. The ARM lane compares the
  **complete FPSCR**, including NZCV, sticky exception bits and FZ/DN/rounding
  controls.

Final runs (header `62648a62…`, test `99562f55…`, fixture `c58e393b…`):

| Run | Result |
| --- | --- |
| `host-final-100000/`, O2 `-frounding-math`, fresh lift | PASS: 63,601 admitted, 1,053 yields; paths: inside 7,226, leaving 26,833, no-root 19,058, behind/yield 771, discriminant 7,295, NaN direction continuation 2,418, declined 36,399 |
| `host-final-asan/`, ASan+UBSan, 30,000 cases | PASS, no diagnostics |
| `negatives-post-fix/`, 10 single-defect header controls | All detected (`negative-controls.json`) |
| `arm-final-12000/`, VitaSDK GCC 15.2 with Makefile recomp flags, Unicorn Cortex-A9, 12,000 cases | PASS on context, arena, yields and full FPSCR, with ON/OFF admission counts. 12 FPSCR modes: 0, the three directed rounding modes, sticky `0x9F`, FZ, DN, FZ\|DN\|sticky, preset NZCV `0xF0000000`, NZCV with rounding, all combined, and FZ\|rounding with sticky IDC. 7,708 admitted, 123 yields. |
| `arm-trap-model/`, 1,200 cases | PASS. Unicorn keeps FPSCR trap enables read-as-zero, so the test ORs each enable bit (`0x100`…`0x8000`) into the register read by the candidate's single admission `vmrs`. Every case declined (admitted 0), state was identical, and the injection was confirmed on every ON lane. |

An earlier trial showed that reordering the three direction-word stores is
unobservable inside one page, so that is not a defect. The page-check control is
detected on state because the fixture includes page-straddling frames.

## ARM-only defect found and fixed

The first ARM lane (`arm-lift-final/`, header `142497a3…`) failed at seed 804
(FPSCR 0, admitted, discriminant path). The seed-804 reproduction with dumps
(`arm-failure-seed804/`) shows one arena byte differing: `[S]+3` is `0x7F` in the
original and `0xFF` with the helper.

- **Stored value:** the spilled discriminant is a NaN with the wrong sign
  (`0x7FC00000` vs `0xFFC00000`).
- **Inputs:** start.y and center.y are both +Inf, so the path generates NaNs.
- **Why the final context matched:** the spilled slot is overwritten afterwards.
- **Why host missed it:** x86 has no multiply-accumulate instructions.
- **Cause:** GCC contracted the helper's arithmetic into 5 `VMLA` and 1 `VNMLS`
  (disassembly in that directory). The lifted reference unit contains none.
  ARMv7 `VMLS`/`VNML*` apply FPNeg to the product, which flips a NaN's sign.
  Non-NaN results and exception flags are unaffected.
- **Fix:** explicit inline `vadd.f64`/`vsub.f64` at the eight
  product-accumulating operations. The rebuilt candidate has none of these
  instructions (`arm-fix-seed804/`). Seed 804 passes, and the full host, ASan,
  negative, ARM and trap-model runs above were repeated after the fix.

The failure receipts are preserved unchanged.

## Fresh-launch integration checks

Independent review verified the final source/oracle hashes and saved ARM
disassembly, with no additional semantic blocker found. Root's integration
retains the qualified helper header byte-for-byte. The owned-image fresh lift
matches the existing staged `code_016.c` function; only that complete function
is replaced, and the whole unit's compile-OFF preprocessed output is unchanged.

Host ASan/UBSan compares 4,096 cases for each fixed startup mode, 0 and 1,
without any mode writes or counter resets; startup ON admits 2,621 calls. A
further 4,096-case legacy run keeps both ON/OFF lanes. Each checks full context,
arena, yields and host FP exceptions. The startup helper build wiring passed
12 actual incremental Makefile builds and four invalid-default checks using
real compilation, dependency files and archives. These are correctness/build
checks, not hardware performance measurements.

## Instruction-cost distribution (not performance)

These are executed ARM instructions in `arm-final-12000/distribution.json`,
counted in Unicorn over the synthetic fixture. They include mode read, trap
check, page and alias checks, input reads, NaN checks, the atomic admission
counter and publication. Fixture callbacks and firmware copies are excluded.
They are not CPU time, cache behaviour or Vita FPS. The synthetic path mix is not
the gameplay mix.

The path classes are approximate labels assigned before execution, rather than
a precise trace of the branch ultimately taken. Firmware copy bodies are
excluded; 80 of the 12,000 rows have two additional copy calls in the candidate.
Use the distribution as an investigation aid, not a cycle estimate.

| Path | n | Original median | ON median | ON−original (median; p10..p90) |
| --- | ---: | ---: | ---: | ---: |
| inside | 848 | 279 | 256 | −23 (−23..−23) |
| leaving | 3,291 | 487 | 356 | −131 (−131..−131) |
| no root | 2,269 | 732 | 400 | −332 (−336..−332) |
| behind start (yield) | 84 | 812 | 435 | −377 (−381..−377) |
| discriminant | 914 | 864 | 433 | −428 (−431..−332) |
| NaN direction continuation | 302 | 865 | 873 | +8 (−3..+18) |
| declined | 4,292 | 865 | 886 | +17 (+14..+116) |

- **Declines** cost +9 over the OFF lane for early declines (mode/trap/page). Late
  declines after input reads and NaN checks cost up to +163.
- **Build ON, runtime OFF:** +3..+8 per call against the original.
- **Trap-enable declines:** +8..+11.
- **Text size:** the candidate function is 5,402 bytes against 3,520 for the
  original, because it keeps the full fallback.

Whether gameplay gains depends on which paths real `B0CB0` calls take and how
often they decline. That is unmeasured.

## Remaining limits

- **Emulator, not hardware:** Unicorn is QEMU softfloat, not Vita VFP silicon.
  Trap-enable declines are modelled by register injection, not by real trap
  delivery.
- **Synthetic fixture:** callers, path mix and admission rate in gameplay are
  unknown. The callers' own lifted code is unchanged and was not re-validated here.
- **Contraction outside this helper:** the defect shows that any native helper
  written as C floating-point arithmetic can differ from lifted code in NaN sign
  under VFP contraction. This helper is fixed by explicit instructions. Other
  helpers were not audited here.
- **Declines:** page-straddling stack frames, NaN inputs, and a constant sharing
  the stack's physical page.
- **Integration:** default OFF in the repository; startup ON is an explicitly
  selected gameplay candidate. Physical performance remains to be established.
