# Object collection helper: Cortex-A9 qualification

Helper under test: `recomp/kernel/xk_object_collect.c`, SHA-256 `e17bc621…`, unmodified.
Test: `tools/test_arm_object_collect.py`. Receipts:
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/claude-collision-validation/arm-qualification/`.
Everything here is ARM instruction counts in Unicorn's Cortex-A9 model. None of it
is a cycle, cache or Vita frame-time measurement, and no FPS change is claimed.

## Method

- **Oracle:** the original `171F10`, `1716F0` and `1D130`, lifted from the owned
  XBE with the production `code_*.c` mapping macros. The candidate is the hooked
  `171F10` emission plus the helper.
- **Build:** all units use VitaSDK GCC 15.2 with the Makefile's recomp flags
  (`-O2 -fno-strict-aliasing -mthumb -mcpu=cortex-a9 -mfpu=neon -w -std=gnu11`,
  `-DXV_NATIVE_OBJECT_COLLECT`). The generated original stays private in the
  receipt directory (`original-private.c`).
- **Lanes:** each fixture runs three times: the translated reference, the
  candidate with the helper enabled, and the candidate with the helper disabled.
  Configuration is the resolved atomic word, and the atomic tallies are included
  in the counts. First-call environment resolution costs 18 extra instructions
  plus one modeled `getenv`/`atoi` pair, once.
- **Modeled work:** `88110`, `868F0`, `487E0`, `855F0`, `81900`, `81770`,
  `172DE0`, `172F40`, `xv_preempt` and libc copies are modeled in Python and
  excluded from counts. The translated `1716F0`, including its children and
  siblings, is counted.
- **Comparison:**
  - At every modeled call and yield: the full context, the callee's arguments and
    the walk frame window `[ESP−0xA0, ESP+0x20)`.
  - At the end: the full context, the whole arena and FPSCR.
  - FPSCR NZCV is counted separately (see the finding below).
- **Fixtures:**
  - 18 designed walks × 8 FPSCR modes: four rounding modes, sticky `0x9F`, FZ, DN,
    and FZ|DN|sticky.
  - 400 randomized worlds ported from the host test.
  - Coverage includes sphere, flag, selector and exact-boundary rejects; biped,
    scenery and sibling fallbacks; NaN, Inf and subnormal bounds; center and
    physical-page frame aliases; register-changing yields; and all-stamped walks.

## Result

PASS on 544 fixtures. Context, events and arena match exactly in both the helper-on
and helper-off lanes, as do FPSCR exception, rounding, FZ and DN bits.

Limits:

- Unicorn keeps native trap-enable bits as read-as-zero, so trap-enabled declines
  are not dynamically tested.
- Its flush-to-zero modeling is QEMU's.

### ARM-only finding: FPSCR NZCV after a native sphere test

After a skipped object that needed the sphere test, final FPSCR NZCV differs. On
the minimal witness `reject-sphere-1` at FPSCR 0, the reference ends at
`0x80000000` and the helper at `0x20000000`. This happened in 288 of 544
helper-on fixtures and 0 helper-off fixtures.

- **Cause:** the helper's `limit < distance` compiles to `vcmpe.f64 d20,d21; bgt`,
  with reversed operands. The original uses `x87_compare`'s quiet
  `vcmp.f64 limit, distance`.
- **Exception flags:** no difference. All eight inputs are checked finite first,
  so the signaling compare cannot raise IOC.
- **Observability:** every current consumer of FPSCR condition bits performs
  `vcmp` immediately before `vmrs APSR_nzcv`, so no guest-visible effect is known.
  It still breaks exact-FPSCR parity.
- **Proposed diff:** `claude-collision-validation/arm-qualification-nzcv-proposal/proposal.diff`.
  It uses `x87_compare`'s inline Thumb-2 VFP sequence with operands
  `(limit, distance)`. The shared helper was not edited.
- **Proposal validation:** the patched private copy passes `--strict-nzcv` on all
  544 fixtures (`arm-qualification-nzcv-proposal/result.json`). Witness details are
  in `witness.json` there.

## Instruction counts

"OFF" is the hooked emission with the helper disabled. Separate compilation of the
hooked copy changes codegen around yields. Without yields, OFF costs +4 to +14
instructions over the reference; with yields it costs about −35 per yield.
Compare **ON vs OFF** to isolate the helper.

| Designed walk (FPSCR 0) | Reference | Helper on | Helper off | ON − OFF |
| --- | ---: | ---: | ---: | ---: |
| cluster already stamped (0 objects) | 699 | 761 | 717 | **+44** |
| object already stamped | 830 | 846 | 844 | +2 |
| 1 flag reject (bit 0) | 1,118 | 1,129 | 1,132 | −3 |
| 1 sphere reject | 1,368 | 1,313 | 1,382 | −69 |
| 1 selector reject (type 3) | 1,435 | 1,360 | 1,449 | −89 |
| 1 exact boundary (C3) then type skip | 1,415 | 1,351 | 1,429 | −78 |
| 1 subnormal bound reject | 1,414 | 1,351 | 1,428 | −77 |
| 1 biped fallback | 1,476 | 1,811 | 1,490 | **+321** |
| 1 scenery fallback | 1,573 | 1,908 | 1,587 | **+321** |
| 1 object with sibling (fallback) | 1,700 | 1,835 | 1,714 | **+121** |
| 1 NaN bound (fallback) | 1,368 | 1,591 | 1,382 | **+209** |
| 1 Inf bound (fallback) | 1,368 | 1,585 | 1,382 | **+203** |
| 16 sphere rejects | 10,788 | 9,668 | 10,802 | −1,134 (−10.5%) |
| 64 sphere rejects | 40,932 | 36,404 | 40,946 | −4,542 (−11.1%) |
| 128 mixed (109 skipped) | 85,475 | 83,455 | 85,489 | −2,034 (−2.4%) |
| 32 mixed, yields every back edge | 21,739 | 22,203 | 21,753 | **+450** |
| 8 rejects, center aliasing frame | 6,132 | 5,516 | 6,146 | −630 |
| 4 objects, physical-page frame alias | 3,265 | 3,289 | 3,279 | +10 |

Over 320 walked random worlds, ON totals 13,076,567 instructions against 14,078,654
for OFF (−7.1%) and 14,179,644 for the reference (−7.8%). Twenty-five worlds lose
(worst +7,529 ON − OFF). A least-squares fit of ON − OFF gives:

| Term | Instructions |
| --- | ---: |
| Per skipped object | −76 |
| Per translated fallback object | +233 |
| Per leaf iteration | −19 |
| Per walk | −44 |

The maximum residual is 1,691, so these values are indicative
(`analysis-on-vs-off.json`). The random worlds visit up to 299 leaves, which
flatters the per-leaf term.

### Interpretation

- The saving per rejected object is modest, about 70–90 instructions (about 11%
  of a translated reject). The skip still performs the 12 call-frame stores,
  about 15 paged loads, full register sync and the atomic tallies.
- Each object the helper cannot skip costs 120–340 extra instructions. The helper
  runs its prologue and prefilter, then the translated callee repeats everything.
  Break-even is roughly 3–4 skipped objects per fallback object.
- Small walks lose: a walk with no unvisited object costs about +44, and
  yield-heavy walks lose.
- Whether gameplay walks gain depends on the real reject/fallback mix and leaf
  counts. That remains unmeasured; only the physical A/B the coordinator owns can
  decide it.

### Possible reductions, not implemented

These are for the owner to consider; neither has been qualified.

- Cache `g_xram`/`g_xpt` in locals, as generated code does.
- Order the decline checks for accepted object types to avoid float work before
  a certain fallback. Care is needed: a sphere miss must still skip.

## Receipts

| Path under `claude-collision-validation/` | Content |
| --- | --- |
| `arm-qualification/result.json`, `cases.json`, `analysis-on-vs-off.json` | Authoritative run: helper `e17bc621`, test `6a9ca6e0`, ELF `2e1bc125`, private original `afde869b` |
| `arm-qualification-nzcv-proposal/` | Proposed diff, patched copy, strict run, `witness.json` |
| `arm-qualification-nzcv-witness/` | Build outputs of the strict run of the unmodified helper; it aborts at the first NZCV mismatch, so no `result.json` |
| `arm-qualification-smoke/` | Early 8-random-case smoke run; superseded |
