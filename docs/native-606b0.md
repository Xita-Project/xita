# Native lens-flare scene work: the per-flare region of `f_000606B0` and `f_000602F0` (`XV_NATIVE_606B0`)

Sept 24 2026. Branch `work/native-606b0-20260924`. Status: implemented and verified: differential tests (fuzzed calls vs
the lifted guest bodies, 3 builds, 0 mismatches, 41/41 mutants caught), in-game verify mode on the x86 host harness and
the Raspberry Pi ARM harness (0 mismatches; see the tables). Default off. The unit compiles for the Vita with the exact
Vita command line; not yet linked into a VPK or measured on hardware.

## What was replaced, and why these boundaries

The Vita's a10 cinematic (perf181t, `[scene-phases]`, ms/frame) puts the scene helper's two lens-flare functions at:

| function | Vita cost | calls | what it is |
|---|---|---|---|
| `f_000606B0` | 5.89 incl, **3.61 self** | 1/frame | render the frame's lens flares: for each row of the flare list (0x2C76D0, 40 bytes, count at 0x2E34E0) test view/area/alpha, build the camera-relative position and its reflection about the view axis, read the brightness byte, fade by distance, rotation (`60000`), screen angle (`fpatan`), normalize the offset (`11120`), three edge fades clamped to [0,1], then per reflection (0x80-byte records of the definition) a brightness product; a reflection with brightness > 0 is drawn (texture/state setters, color animation, `63E80` the quad) |
| its children | `63E80` 0.63, `60000` 0.55, `60E90` 0.42, `11120` 0.29, `5FE30` 0.19, `64480` 0.12, `64070` 0.05, `11B60` 0.04 | `60E90` 183/frame, `60000`/`5FE30`/`11120` 174/frame, `63E80` ~15/frame | |
| `f_000602F0` | **4.05 incl** (of `f_00092890`'s 5.27) | 5/frame | collect one BSP cluster's lens-flare markers into the flare list: per marker (16 bytes: position, int8 direction, flare index) the direction /127, a perpendicular (`B1260`), both normalized (`11120` x2), both packed 11/11/10 (`61270` x2, each with three CRT `floor` calls `19E7B` -> `1EC1F` x2 + `1EABA`), and the 40-byte record appended (`5FE80`) |

(The timers themselves cost ~1 us per timed call on the Vita, so 606B0's "self" includes roughly 0.7 ms of child-timer
overhead for its ~700 timed calls per frame.)

**`f_000606B0`: a region, not the function.** Its draw path is D3D work (`64070` render state, `64480`/`61FE0`/`7F210`
setters, `63E80` the quad, `71D00` in the final loop): an entry-hook native would have to call those as guest code, and a
verify mode that runs native and guest on the same state would issue every draw twice. But the draw path runs only for
reflections that are drawn (~15/frame) while ~174 flares/frame go through the pure per-flare math. So the native covers
exactly the pure part, as a **region between label hooks** in the guest body:

- entry labels `L_000606FC` (first flare), `L_00060700` (next flare), `L_00060AC0` (next reflection after a drawn one),
  `L_00060DA6` (next flare after a drawn one / after `64480` rejected the draw);
- the native runs from there, flare after flare, reflection after reflection, until the guest would reach
  `L_00060B13` (a reflection to draw) or `L_00060DC0` (the flare loop is done); the hook then `goto`s that label and the
  guest body continues with the draw path / the final loop;
- inlined pure callees: `f_00060E90` (unpack an 11/11/10 normal), `f_0005FE30` (brightness byte address, with its
  flare barrier call), `f_00060000` (rotation: four modes through the constant jump table, `fpatan`), `f_00011120`
  (normalize).

The region holds 606B0's self time plus `60E90`/`5FE30`/`60000`/`11120` (1.4 ms). What stays guest: the prologue and
`64070`, the draw path per drawn reflection, the final loop (`7F210`, `71D00`).

**`f_000602F0`: the whole function.** All its callees are pure computation; its only guest-visible effects are its
stack, the flare-list rows, the count 0x2E34E0 and the list-full byte 0x2E34E4 (plus `5FE80`'s flare barriers). One
entry hook (called from `f_00092890`).

## Semantics reproduced (exact)

`recomp/kernel/xk_native_606b0.c`, both natives:

- **The guest stack.** A host shadow of a window around esp is read once per region/call: `[E-0x40, E+0xC8)` for 606B0
  (its 0xB8-byte frame, the 4 pushes and the callee frames down to `60000 -> 60E90` at E-0x3C), `[S-0x54, S+0x5C)` for
  602F0 (its locals incl. the 40-byte flare record, and the CRT frames down to `61270 -> 19E7B -> 1EC1F/1EABA` at
  S-0x4C). Every stack store the guest performs is performed in the shadow, in order - locals, return addresses, pushes,
  callee locals, the dead ones included (e.g. `_controlfp`'s write-back of its argument slot with the pushed ecx's high
  half, `1EABA`'s rounded double, `60000`'s three-float frame) - and the window is written back at the end. Stack reads
  come from the shadow.
- **Other guest memory** translates like the shards: the thread's page table (`X_PT`), `X_IMG` rules of the build (image
  globals through the page table under `XV_RENDER_VIEW`). Integer loads are single-translation like `X_M8/16/32`, float
  loads page-split like `x87_load_f32`, the string copies element-wise like `x_str_movs`. Any access whose bytes fall in
  the shadowed window - by guest address, or, for a single-translation access straddling a page, by host address - is
  served from the shadow, so data aliasing the stack (tested) behaves like the guest. Reads happen at the guest's points
  in the sequence (a flare definition read inside the window sees the region's own earlier stack stores).
  The image constants (page 0x1F0000) and the render camera (page 0x2FC000) are read once per region/call (the window is
  checked not to overlap those pages; nothing in the region or in 602F0 writes them).
- **x87.** The eight slots are depth-indexed locals (`X[d]` = slot `(top - d) & 7`) updated like the emulator's `st[]`:
  pushes, `fxch` swaps, `fstp st(n)`, dead slots included; all are written back. `fsw` from every compare with the TOP
  field the emulator stores; float math in doubles with the guest's operand order and association, loads widened from
  float, stores rounded to float; `fpatan` is the same libm `atan2` call the guest makes, `fsqrt` is `vsqrt.f64`
  (`-fno-math-errno`), `frndint`/`fistp` round under `fcw` like `x87_round` with the `x87_store_i32` range rule - for
  |v| < 2^31 without the libm calls (int32 truncation, per-mode adjustment, the sign of a zero result kept; ARMv7 has no
  rounding instruction and `nearbyint` saves and restores the FP environment), libm otherwise; the 602F0 test checks it
  against libm on 1.6 M values x 4 modes (halves, zeros, signs, 2^31 edges, NaN/inf). The CRT floor sets `fcw` from
  [0x1F2840] and restores it exactly as `1EC1F` does. The unit is compiled `-ffp-contract=off`: no fused
  multiply-add (GCC's VFPv3 `vmla.f64` is the non-fused, separately rounded form, the same the guest shards get). The top
  of stack is written back masked to 0..7 only where the lifted code masks it (the build-x87 spliced `60E90` leaves it
  alone).
- **Registers and flags.** eax/ecx/edx/ebx/ebp/esi/edi as the lifted code leaves them (including partial writes:
  `fnstsw ax`, `mov di,..`, `mov ax,..`), esp as at the exit label (606B0 region) / after `ret` (602F0). The lazy-flag
  record of the last flag-setting statement *the lifted code emits* (the recompiler drops dead flag writes, e.g. `xor
  ebx,ebx` at 60A9F, `add edi,ecx`, `and edx,0FFFFh`; the native drops the same ones) and the carry/overflow cells of the
  last `inc`/`dec`/shift/`imul` that wrote them (stale cells included).
- **Budget.** `c->preempt` drops by exactly the guest's back-edge count (606B0: the reflection loop, the flare loop,
  `60000`'s mode-3 `jmp 600F2`; 602F0: the marker loop and the CRT floor's inexact `19F23 -> 19F15` jump, taken six
  times per marker) and `xv_preempt` is called the same number of times, at the end of the region/call (a scheduling
  point only; the scene helper never yields there).
- **Flare barriers.** `5FE30`'s `xv_flare_barrier(2)` per flare and `5FE80`'s `xv_flare_barrier(6)` per marker are
  called at the same points (they only wait for another fiber's drain; nothing is pending at these points in practice).
  `5FE80`'s surface-slot path and its draining `xv_flare_barrier(3)` are unreachable from 602F0 (602F0 always stores
  0xFFFF at [S+50h], the record's +1Ch, right before the call; the window is checked not to overlap the flare rows).

**Declined** (the hook returns 0 before touching anything, the guest body runs): 606B0 - an esp not 8-aligned (the frame
is `and esp,-8`), a stack window overlapping the constant/camera pages or the flare count; 602F0 - the precision
exception unmasked in `fcw` (the CRT floor would raise through `_except`), an esp not 4-aligned, a window overlapping the
fixed-address data (constants, 0x1F2840, 0x39BE58, 0x39CE24, 0x2FEB8A, the camera page, 0x2E34E0/E4) or the flare rows.
In-game every region and call was accepted (0 declines).

**Reference semantics for `61270`.** The 602F0 native reproduces the *lifted* `61270` and the lifted CRT floor
(`19E7B` / `1EC1F` / `1EABA`), i.e. the true guest. The stage also has runtime shortcuts for those callees:
`XV_NATIVE_PACK=2` (the Vita runs use it: 61270 as a native pack that skips the CRT frames) and `XV_NATIVE_CRT_FLOAT`
(default on: 19E7B/1EC1F/1EABA natives that skip their dead stack stores). Both are exact in what their callers read
but not in dead state (dead stack bytes below esp, the floor's `fsw`/eax), so **verify 602F0 with
`XV_NATIVE_PACK=0 XV_NATIVE_CRT_FLOAT=0`** (the guest reference is then the lifted code; all in-game verify runs below
do). In production (mode 2) 602F0's calls use the native's exact CRT path whatever those knobs say; 61270's other
callers keep the knobs' behaviour.

**Not reproduced:** which NaN payload / sign a two-NaN operation keeps (the host compiler's operand order for a
commutative add/multiply; the guest body built -O0 and -O2 already disagrees). Verify mode reports such stack words as
`nan-only` separately, not as mismatches. Map data has no NaNs (0 nan-only in every in-game run).

## Knobs and counters

- Build: `XV_NATIVE_606B0=1` (Makefile; adds the unit via `games/halo_ce_3925/runtime.mk`, `-DXV_NATIVE_606B0=1`,
  `-ffp-contract=off -fno-math-errno` for the unit). `XV_NATIVE_606B0_DEFAULT` / `XV_NATIVE_602F0_DEFAULT` (0).
- Hooks: `python3 tools/patch_native_606b0_hooks.py <stage>/recomp` (idempotent; `code_011.c`: the six label hooks and
  the entry bookkeeping hook in `f_000606B0`, the entry hook in `f_000602F0`).
- Env `XV_NATIVE_606B0` (the region) and `XV_NATIVE_602F0`: 0 off (default), 1 verify, 2 native.
  Verify for 606B0: at an entry label the native runs on the real state, its result (context, window) is kept aside,
  the state restored, and the guest runs the same region with an unbounded budget; the exit probe at `L_00060B13` /
  `L_00060DC0` compares exit label, registers, flags incl. the stale cells, x87 slots/fsp/fsw/fcw, back-edge count and
  the whole window; the guest's result is kept. Verify for 602F0: native (without the barrier calls), restore window,
  rows and count, guest, compare context, back-edges, window, count bytes and every row from the old count on.
- Env `XV_NATIVE_606B0_TIME=1` / `XV_NATIVE_602F0_TIME=1`: microseconds per region / call (`xk_os_monotonic_us`);
  mode 0 times the guest (606B0: entry hook to exit probe), 2 the native, 1 both.
- Host harness only: `XV_HOST_VISIBILITY_PIXELS=N` answers the flare visibility queries with a pseudo pixel count in
  [0, N) instead of 0 (`recomp/host/runtime_stubs.c`): without it every flare's brightness is 0 on the host and 606B0
  never reaches its reflection loop or the draw path.
- 60-frame lines from xd3d.c's report block:
  `[native-606b0] 60 frames: calls N regions N native N verified N mismatched N (total mismatches N) nan-only N; flares N
  reflections N draws N; declined N stale N; us/region native X guest Y; ms/frame native X guest Y` and
  `[native-602f0] 60 frames: calls N native N verified N mismatched N (total mismatches N); markers N added N floors N;
  declined N; us/call native X guest Y; ms/frame ...`. Mismatches print up to 12 detail lines
  (`MISMATCH <field> native .. guest ..`).

## Verification

Stage: a copy of `overlap-candidate/build-x87` (Sept 24 morning: aim blend and native visibility installed) plus
`tools/install_native_606b0.py`; scene on the helper thread (`XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1
XV_RENDER_VIEW_THREAD=1 XV_RENDER_VIEW_ALL=4 XV_RENDER_VIEW_SPLIT=1 XV_RENDER_VIEW_EARLY=1`) unless noted.

**Differential tests** (`tools/test_native_606b0.py <stage>/recomp [cases] [--seed N] [--only 606b0|602f0] [--mutants]
[--bench]`): the guest bodies are extracted from the stage (606B0 suite: `f_000606B0` with its hooks, `60E90`, `5FE30`,
`60000`, `11120`, `61560`, `11B60`; 602F0 suite: `f_000602F0`, `B1260`, `11120`, `61270`, `19E7B`, `1EC1F`, `1EABA`,
`1EAF7`, `5FE80`) and built with `tools/tests/native_606b0.c` / `native_602f0.c` three ways (plain page table -O2,
per-thread table + render view -O2, -O0; the native with its production flags). Every case runs the whole function
three times from the same synthetic state: guest (hooks off), native (mode 2) and verify (mode 1), and compares the whole
8 MiB arena, every `xctx` field, the `xv_preempt` and flare-barrier call counts; verify must equal the guest and log no
MISMATCH; a decline must leave everything untouched.
606B0 scenes: 0..48 flares with every rejection (view stage, area, alpha, reflection count, brightness, distance fade
<= 0 / NaN / inf / 0/0), all rotation modes (1..4, 0, out of range, mode 3's back-edge), both brightness-byte paths,
0..8 reflections with fade indices in and out of 0..3, the draw path in between (stubs with the real stack conventions
log every call into guest memory; `64480` sometimes rejects, so regions re-enter at `L_00060AC0` and `L_00060DA6`),
NaN/inf/tiny data, stale x87 top bits, every lazy-flag kind, windows across pages, and aliasing: reflection records and
flare definitions inside the stack window, a record whose single-translation read straddles into a stack page.
602F0 scenes: 0..40 markers (and 0), zero / tied / axis-aligned int8 directions (every `B1260` branch, the `11120`
zero-length path), markers in front of / behind the camera, the list near and at its 1024 limit, flares disabled,
the CRT floor's control word with any rounding field, five caller control words (precision exception unmasked ->
declined), unaligned esp (declined), markers and definitions inside the stack window, a cluster count word straddling
into a stack page.

| suite (final code) | cases | compared per build | mismatches |
|---|---|---|---|
| 606B0, 5 seeds x 5,000 x 3 builds | 75,000 | 254,226 regions (127,113 native-mode + 127,113 verify-mode): 1,075,652 flares, 1,229,146 reflections, 210,190 draw exits | 0 (1,062 NaN-only words in NaN-data scenes at -O2, 48 at -O0; 1 case where such a NaN's bits reached the draw path through an aliased record - both accepted only when the region-by-region verify of the same case differs in nothing else) |
| 602F0, 4 seeds x 5,000 x 3 builds | 60,000 | 34,304 calls (17,152 + 17,152): 647,430 markers, 391,108 list appends, 3.88 M CRT floors; 2,848 declined cases (0 changed state) | 0 |
| rounding self-test (602F0 harness) | 1.6 M values x 4 modes per build | against libm | 0 |
| Pi 4 (ARM -O2, 2 builds x 3,000 per suite) | 12,000 | both suites + the rounding self-test | 0 |

Mutants (`--mutants`, 2,000 cases each; each must be caught): 41/41 caught (22 region, 19 602F0) - among them a
reassociated reflection vector, a partial sum rounded to float, swapped `fpatan` operands, `60000`'s mode-3 back-edge,
one `fxch` of a pair (dead slot only), `60E90`'s return address (dead stack), `11120`'s compare TOP field, the shadow
not consulted for aliased data, the straddling read not mapped to the shadow, the stale carry of the flare-loop `inc`,
`shl edi,7`'s flag record, fsp masked where the lifted code does not, the flare barrier call, the reflection count
cached; the CRT floor rounding to nearest, its back-edge, `_controlfp`'s dead argument write-back and the pushed ecx's
high half in it, its return value, the 11-bit mask, a `B1260` tie branch, 9 of 10 dwords copied, the list-full byte,
the precision-exception decline removed, the straddling count word read page-split. Mutants that turned out equivalent
for this code (a `<=`/`<` clamp at exactly 1.0, `fistp` rounding of an already integral value, the alpha test of 602F0's
always-0xFFFFFFFF color) were replaced.

**In-game verify mode** (`XV_NATIVE_606B0=1 XV_NATIVE_602F0=1 XV_NATIVE_PACK=0 XV_NATIVE_CRT_FLOAT=0`, every region /
call compared; host runs from a copy of the Vita checkpoint save (Continue: mid-a10 gameplay) with the scripted
look/move/fire pad, or a new game into the a10 cinematic; `vis` = `XV_HOST_VISIBILITY_PIXELS=300`, without which no
flare on the host reaches the reflections):

| run | code | where | frames | 606B0 regions | flares | reflections | draw exits | 602F0 calls | markers | mismatches |
|---|---|---|---|---|---|---|---|---|---|---|
| ver1 | A | x86, save | 26,640 | 26,639 | 623,891 | 0 | 0 | 32,727 | 536,623 | 0 |
| ver2 | A | x86, new game, cinematic | 26,760 | 26,518 | 9,606,396 | 0 | 0 | 124,698 | 13,147,421 | 0 |
| ver3 | A | x86, save, vis | 35,640 | 284,430 | 783,332 | 277,557 | 248,791 | 41,794 | 646,432 | 0 |
| ver4 | A | x86, new game, vis | 35,760 | 6,913,904 | 12,705,395 | 8,442,494 | 6,878,373 | 178,766 | 16,136,731 | 0 |
| ver-arm | A | Pi 4 (cores 2-3), save, vis | 35,040 | 348,532 | 3,541,207 | 320,691 | 313,493 | 38,965 | 3,515,972 | 0 |
| fver1 | B | x86, save, vis | 35,640 | 320,246 | 1,072,968 | 286,655 | 284,607 | 42,413 | 1,061,100 | 0 |
| fver2 | B | x86, new game, vis | 35,760 | 6,908,511 | 12,697,308 | 8,435,579 | 6,873,011 | 178,610 | 16,130,539 | 0 |
| fin-ver1 | C | x86, save, vis | 35,640 | 388,922 | 1,070,210 | 355,289 | 353,283 | 42,362 | 1,059,119 | 0 |
| fin-ver2 | C | x86, new game, vis | 35,760 | 6,915,094 | 12,707,925 | 8,443,773 | 6,879,560 | 178,780 | 16,142,367 | 0 |
| fin-ver3-owner | C | x86, save, vis, **scene on the owner** (`XV_SCENE_THREAD=0`) | 26,640 | 336,512 | 845,301 | 311,882 | 309,873 | 33,366 | 834,206 | 0 |
| comb-ver | C | x86, save, vis, **63C00 and 92330 natives on** (`XV_NATIVE_63C00=2 XV_NATIVE_92330=2`, the current build-x87 + this installer) | 26,640 | 359,330 | 917,654 | 335,165 | 332,691 | 32,536 | 799,982 | 0 |
| fver-arm | C | Pi 4 (cores 2-3), save, vis | 35,400 | 177,357 | 578,400 | 143,360 | 141,958 | 38,751 | 566,914 | 0 |
| final-ver1 | D | x86, save, vis | 35,640 | 321,989 | 1,072,875 | 288,402 | 286,350 | 42,439 | 1,061,963 | 0 |
| final-ver2 | D | x86, new game, vis | 35,760 | 6,913,694 | 12,706,095 | 8,442,242 | 6,878,167 | 178,747 | 16,140,603 | 0 |
| final-arm | D | Pi 4 (cores 2-3), save, vis | 35,280 | 3,382,057 | 4,405,178 | 3,352,941 | 3,346,778 | 70,971 | 4,313,902 | 0 |

Code: A = `f65d083` (as first committed), B = A + one-translation constants, aligned shadow access, single-vcmp
compare (uncommitted intermediate), C = `4a8c3df` (B + libm-free rounding), D = `4a78f4f` (C + in-window fast path; the
final code - later commits only change comments, tests and the host knob). Every run: 0 declines, 0 `nan-only`, 0 stale
records. Total: 498,000 frames (392,280 x86, 105,720 ARM); 33.6 M regions compared (75.3 M flares, 39.4 M reflections,
33.1 M draw exits); 1.26 M 602F0 calls (92.1 M markers, 553 M CRT floors); 0 mismatches. Final code alone: 10.6 M
regions and 292,157 602F0 calls.

Also run with the stage's shortcuts on (`XV_NATIVE_PACK=2`, CRT floats default on) in verify mode, to time native and
guest on the same work (below): the 606B0 region again 0 mismatches (201,235 + 67,281 regions); 602F0 then differs in
exactly the pack's dead state - stack bytes below esp in the CRT frames and the back-edge count (the pack runs none of
the CRT floor's code) - as expected.

## Cost, guest vs native (host-side; not Vita numbers)

`XV_NATIVE_606B0_TIME=1 XV_NATIVE_602F0_TIME=1`. The meaningful in-game comparison is **paired**: verify mode runs the
native and then the guest on the same region / call (the native first, with cold caches, the guest second - so the
ratio is conservative). Separate mode-0 / mode-2 runs are not a same-work comparison on the host: the simulation diverges
between runs (e.g. 5.3 vs 19 regions/frame, different marker counts, in two Pi runs of the same save and pad script).
Gameplay windows from frame 1500 (Vita save, pad script, `vis`); final code (D).

| where | guest reference | 606B0 region: guest | native | ratio | 602F0: guest | native | ratio |
|---|---|---|---|---|---|---|---|
| Pi 4 ARM (-O1 harness), paired | production: `XV_NATIVE_PACK=2`, CRT floats on | 0.0351 ms/frame (19.27 us/region) | 0.0180 (9.85) | 1.95x | 0.0565 ms/frame (51.74 us/call) | 0.0364 (33.70) | 1.55x |
| Pi 4 ARM, paired | lifted (`XV_NATIVE_PACK=0 XV_NATIVE_CRT_FLOAT=0`) | 0.3595 (5.00) | 0.1770 (2.51) | 2.03x | 0.6491 (329.98) | 0.0981 (50.13) | 6.6x |
| x86 host (-O1 harness), paired | production | 0.0076 (0.83) | 0.0035 (0.40) | 2.2x | 0.0075 (6.00) | 0.0070 (5.69) | 1.07x |
| x86 host, paired, new game (cinematic) | lifted | 0.1438 (0.75) | 0.0809 (0.43) | 1.78x | 0.3095 (131.96) | 0.0880 (36.19) | 3.5x |

Micro-benchmarks (the test programs with `--bench`, -O2, one typical scene repeated, hot caches; 606B0: the whole
function with the draw path stubbed, "region only" = every reflection rejected so the flare list is one region; 602F0:
the whole function against the lifted guest):

| where | 606B0 region only | region timers | fixed cost (one rejected flare) | whole 606B0 incl. draw stubs | 602F0 vs lifted |
|---|---|---|---|---|---|
| Pi 4 (A72) | 54.86 -> 21.66 us, 2.53x (38 flares) | 69.46 -> 22.81 us, 3.05x (47 flares) | guest 0.32, native 0.42 us | 92.37 -> 61.63 us, 1.50x | 132.59 -> 20.75 us, 6.39x (38 markers) |
| x86 | 10.05 -> 4.02 us, 2.50x (47 flares) | 8.45 -> 2.37 us, 3.57x | guest 0.03, native 0.06 us | 17.17 -> 11.53 us, 1.49x | 15.03 -> 3.63 us, 4.14x (37 markers) |

Where the native time goes: the region is bounded by the two `atan2` calls per flare (the guest's two `fpatan`s: the same
libm function, ~94 ns each on the A72 - both sides pay them) and its fixed cost per region (the 0x108-byte window in and
out, marshaling registers, flags and x87 slots: ~0.4 us on the A72), which matters when draws split the flare list into
short regions. 602F0 against the production path: the pack skips the CRT floor's frames, its control-word dance and its
back-edges (six floors per marker) - work the exact native still does (cheaply: stores into the shadow). Tried and kept:
one translation per constant page, aligned shadow access, one-compare x87 codes, libm-free exact rounding, in-window
fast path (Pi 602F0 bench 5.1x -> 6.4x vs the lifted guest; region fixed cost 0.52 -> 0.42 us).

Expected on the Vita (not measured): perf181t's `[scene-phases]` put the region's parts at 606B0 self 3.61 + 60000 0.55 +
60E90 0.42 + 11120 0.29 + 5FE30 0.19 ms (the timers add ~1 us per timed call, ~0.8 ms of that) minus the guest parts
that stay (prologue, draw path, final loop), i.e. roughly 4 ms, and 602F0 at 4.05 ms (with the pack). At the Pi's paired
in-game ratios (region ~2x, 602F0 ~1.55x against the pack) that is about -2 ms and -1.4 ms of scene-helper time per frame;
the A9's in-order core pays the emulation's memory traffic more (the lifted 602F0 costs ~22 us per marker on the Vita,
~2.2 on the Pi), which should favour the natives further. To be measured with the hardware steps below.

## Vita integration (overlap-candidate/build-x87)

Files (branch `work/native-606b0-20260924`):

1. `recomp/kernel/xk_native_606b0.c` (new; Vita object 29.4 KB text, 16 B data, 828 B BSS; the region and 602F0 use
   ~0.5 KB of host stack each; 602F0's verify mode mallocs two 40 KB row buffers on its first verify call).
2. `Makefile`: the `XV_NATIVE_606B0` block after the `XV_NATIVE_AIM_BLEND` block (flag + both `_DEFAULT`s + the
   `-ffp-contract=off -fno-math-errno` rule for `$(RECOMP_BUILD)/kernel/xk_native_606b0.o`).
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_606b0.c` under
   `ifeq ($(XV_NATIVE_606B0),1)` (after the aim-blend entry).
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_606b0_report(60)` next to `xv_native_aim_blend_report` (prints both
   lines).
5. Shards: `python3 tools/patch_native_606b0_hooks.py <stage>/recomp` (`code_011.c`: `f_000606B0`'s entry bookkeeping
   hook, four entry-label hooks and two exit probes; `f_000602F0`'s entry hook; idempotent).
6. Host harness only: `recomp/host/runtime_stubs.c` gains `XV_HOST_VISIBILITY_PIXELS`.

   Steps 1-6 in one command: `python3 tools/install_native_606b0.py <stage>` (idempotent; anchors: the aim-blend
   Makefile rule, runtime.mk entry and report call, which the stage has). Applied to a copy of the current
   `overlap-candidate/build-x87` (which already carries the 63C00 and 92330 natives, also in `code_011.c` / the Makefile /
   runtime.mk / xd3d.c) it composes with them; the result was compiled with the Vita command lines and run on the host
   (combined run in the tables).
7. Build with `XV_NATIVE_606B0=1` added to `make-vars.txt`. `code_011.c` (~4 min), `xd3d.c` and the new unit are
   rebuilt by mtime; no other unit depends on the flag. Check: `arm-vita-eabi-nm build/recomp/code_011.o | grep
   xv_native_60` (U `xv_native_602f0`, `xv_native_606b0_enter`, `xv_native_606b0_probe`) and the ELF has
   `T xv_native_606b0_enter`.
8. Runtime:
   - verify on hardware once: `XV_NATIVE_606B0=1 XV_NATIVE_602F0=1 XV_NATIVE_606B0_TIME=1 XV_NATIVE_602F0_TIME=1
     XV_NATIVE_PACK=0 XV_NATIVE_CRT_FLOAT=0` - expect `mismatched 0`, `nan-only 0`, `declined 0` in both lines (the
     frame pays guest + native + the window/row copies; `XV_NATIVE_PACK=0 XV_NATIVE_CRT_FLOAT=0` only so that the 602F0
     reference is the lifted CRT path);
   - then native against off in the production config (`XV_NATIVE_PACK=2`), both with the `_TIME` knobs:
     `XV_NATIVE_606B0=2 XV_NATIVE_602F0=2` vs `=0` gives the per-frame pair (`ms/frame native` / `guest`), or with
     `XV_SCENE_PHASES=1` (`606B0` self and `92890 -> 602F0` should drop; note the phase timers of the inlined children
     disappear with the region);
   - `XV_NATIVE_606B0_DEFAULT=2 XV_NATIVE_602F0_DEFAULT=2` in the make vars makes native the default once hardware
     agrees.

Checked here: the new unit, the hooked `code_011.c` and `xd3d.c` compile with the Vita command lines from `make -n`
(arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon), no fused multiply-add in the native, `atan2` stays the libm
call.

## Not done / next

- 606B0's draw path (per drawn reflection: `173F20` color animation with the CRT fmod, `1260F0`, `11B60`, the
  setters and `63E80`) and its final loop (`7F210`, `71D00` per flare meeting its test) stay guest; ~15 draws/frame.
- The region's remaining native cost on ARM is dominated by the two libm `atan2` calls per flare (the guest's
  `fpatan`s; ~94 ns each on the Pi's A72) - both sides make them, so they bound the region's speedup; an exact faster
  `atan2` would have to match newlib's bit for bit.
- 61270's other callers (e.g. `f_00092890` directly) keep the `XV_NATIVE_PACK` path.
