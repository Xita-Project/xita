# Native BSP segment cast under f_001721B0 (`XV_NATIVE_1721B0`)

Sept 24 2026. Branch `work/native-1721b0-20260924` (not pushed). Status: implemented and verified. It is off by
default and has not been built into a VPK or measured on the Vita.

`f_001721B0` is Halo's collision vector test: a segment through the structure BSP and the objects near it. In a30 on the
Vita it costs about 300 us per call, and it is the biggest tick cost found so far. The looping-sound obstruction pass
(`2BB70 -> 2B700 -> 27700 -> 26A50 -> f_0002B460`, flags C0E1, one ray per looping sound, ~59 per frame) costs ~28
ms/frame. Particle collision (`10E240 -> f_00080720`) costs ~4.5 ms/frame, and projectiles and AI lines of sight add
more. Host sampling in a30 puts **59 % (x86) / 54 % (Pi 4)** of the subtree in one self-contained piece, the BSP segment
cast `f_00088E90`. That cast is native in `recomp/kernel/xk_native_1721b0.c`:

| function | role |
|---|---|
| `f_00088E90` | set-up: the traversal record S on its frame (flags, BSP, caller's words, point/delta pointers, result record), t limit (0, 1 or the caller's), leaf count cleared; then 88B80 on [0, t1] |
| `f_00088B80` | recursive segment traversal: the node's plane interval (the `xv_bsp_plane_interval` arithmetic the generated code calls), near child first, split at t = -origin/delta, far child if the hit so far lies beyond the split; leaves: the solid/empty leaf-kind rules, the leaf list (<= 256 entries, +0x414 overflow slot), the leaf test 889E0, the hit record |
| `f_000889E0` | a leaf's BSP2D references on the crossed plane: dominant axis, the point at t0 projected with the axes table, 17ADD0, then 86E20 when the hit byte is set |
| `f_0017ADD0` | BSP2D point location (inline in 889E0) |
| `f_00086E20` | point in the surface's edge ring (2D cross products), the surface's bit in the caller's mask |

The 88E90 hook serves every caller. 1721B0 calls it once for its structure-BSP ray. `f_001731D0`, under the object walk
`f_00171AF0`, calls it once per object collision region the ray reaches: in a30 that is 55-78 % of all casts, depending on where the player is. Seven
other callers make < 0.02 % of the calls.

Evidence, with details below:
- **0 mismatches in 87,828,407 in-game casts** compared in verify mode on the x86 host and the Pi 4 in a30 with
  movement. 70,080,835 of them were on the final build.
- **0 mismatches in 240,000 differential-test cases** against the lifted bodies of both stage flavours, three builds
  each, verify mode included.
- **0 mismatches in 2,000 captured a30 casts** replayed on x86 and on the Pi.
- **33 of 34 deliberately broken variants caught.** The remaining one is near-equivalent (explained under "Mutants").

Speed against the translation:
- In game: **1.81x on the x86 host** (lockstep pair, 7 M casts each) and **2.21x on the Pi 4** (pair, 7.1-7.3 M casts
  each).
- Replay of the captured a30 casts: **2.35x on x86 and 1.72-1.80x on the Pi** (A72, `-O2 -mthumb`), with 2.45x / 2.0x
  fewer instructions.

Expected on the Vita (an estimate, to be measured): **~6-9 ms per frame less in a30**, most of it in the sound pass.

## Where the time goes

**Callers.** Probe stage: a copy of the x87 stage with `xv_phase` pair timers around every call of 1721B0 (keyed by
caller) and around the calls inside its subtree. Host x86, a30 from its start, 121 windows after frame 1500. Per
frame, 1721B0 was called 241-331 times:

| caller | calls/frame |
|---|---|
| `2B460` (sound obstruction) | 51 |
| `80720` (particles) | 188-274 |
| `523D0` | ~3.4 |
| others | < 1 |

The sound rays are long, with 94 % of 1721B0's time in the probe. The particle rays are short, at 0.38 us/call on
x86. The pair timers inflate the deep recursion: 88B80 runs ~8,800 times per frame. The probe was therefore used for
structure only. Shares come from the sampler.

**Shares.** The in-process sampler (`XV_HOST_SAMPLE`, 1 ms SIGPROF, owner thread) on the plain x87 stage in the same
a30 run. x86: 5,520 frames. Pi 4: 25,260 frames at ~30 fps. Samples in the 1721B0 subtree:

| part | x86 | Pi 4 |
|---|---|---|
| **88E90 cast** (88B80 + `xv_bsp_plane_interval` + 88E90 + 889E0 + 17ADD0 + 86E20) | **2,524 (59.0 %)** | **20,786 (54.4 %)** |
| object walk 171AF0 and its other callees (B0CB0 segment/sphere, B6210/B5E40 transforms, 1731D0 set-up, B6560, 172DE0, ...) | 961 (22.5 %) | 10,632 (27.8 %) |
| 1721B0's own body (cluster walk, object list, serials) | 460 (10.8 %) | 3,614 (9.5 %) |
| 17A8B0 (point location; 53 other callers share it) | 331 (7.7 %) | 3,193 (8.4 %) |

Totals were 0.77 ms/frame (x86) and 1.51 ms/frame (Pi). These are host figures: the Vita spends ~300 us per sound ray.

Per cast in the game, from the final build's verify counters on the Pi: 33.6 BSP nodes, 5.8 leaves, 0.63 leaf tests
with a hit, recursion at most 64 levels deep, and 0.63 back-edges. 86E20 (the polygon test) was never reached in a30.

## What was replaced, and why this boundary

| function | disposition |
|---|---|
| `f_001721B0` body (clusters, object list, qserial serials, 17A8B0, 58C20) | guest: 10-11 % self, many callees, the qserial private-serial hooks |
| `f_00171AF0` object walk + 81900/81A10/B0CB0/B6560/172DE0/1731D0/12340/118F0 | guest: 22-28 % spread over ten functions and a recursion over child objects; B0CB0 already has the `XV_NATIVE_SEGMENT_SPHERE` native; next candidate |
| **`f_00088E90` + `88B80` + `889E0` + `17ADD0` + `86E20`** | **native**: 54-59 % of the subtree, self-contained, no other calls, no HLE |

88E90 is the smallest subtree that holds most of the cost. It calls nothing outside itself, so an entry hook in
`f_00088E90` covers it whole. Because the hook sits at 88E90's entry and not at 1721B0's, it also serves the model casts
of 1731D0 and the few other callers.

No existing native covers this code. The fusions (`XV_NATIVE_QUERY_FUSION`, `_SOLVER_FUSION`), the collision natives
(`_SEGMENT_SPHERE`, `_COLLISION_TRAVERSAL/VERTICES`, `_BSP_SPHERE`), `xk_query_reuse.c` and `XV_NATIVE_4B9D0` all work
on the sphere query 88110, the solver, or B0CB0. qserial (`X_QS*`) touches 1721B0's own body only.

The generated 88B80 already calls one kernel helper, `xv_bsp_plane_interval` (`recomp/kernel/xk_geometry.c`, the
88BA5..88BF8 arithmetic). The native reproduces that helper exactly, and the tests compile it from the stage.
`XV_SOUND_OBSTRUCTION` (`xk_sound_obstruction.c`) is independent of this native: it skips whole 2B460 calls whose
sound and listener did not move. With it on, fewer sound rays remain to be sped up.

## Semantics reproduced (exact)

The unit is a transliteration of the generated code. The five bodies are identical in `overlap-candidate/build` and
`build-x87` (memory x87 lowering in both).

- **Memory.** Every guest write happens at the same address, in the same order, through the same translation (X_PT,
  taken once per call like the shards' per-function `xpt_`). Integer accesses use one translation, like X_M32. x87
  float loads and stores are page-split, like `x87_load_f32` / `x87_store_f32`. Per frame and record, the translation
  is taken once when the frame or record lies in one page.
- **Values read from locals** (a value the guest reads back from memory):
  - Frame slots, which only their owner writes (callee-saved registers, arguments, stored floats, 889E0's projected
    point).
  - S's words that 88E90 stores and nothing rewrites.
  - S+0x1C/+0x20/+0x24 and the record's t and leaf count, which only the cast's own stores change.
  - The BSP header, the point and delta floats, and the image constants 0.0, 1.0 and the axes table 0x1EAF30.

  The layout check (`nr_layout`) proves no store of the call can reach any of these. Everything else (nodes, planes,
  leaves, references, BSP2D nodes, surfaces, edges, vertices, the stale upper bytes of byte-written slots) is read from
  guest memory at the guest's point.
- **Registers.** Every guest register is a local assigned where the generated code assigns it, partial writes
  included: `fnstsw ax`, `setcc`, `mov al/bl/cl/dl/ax/cx/dx`, `movzx`/`movsx`. eax/ecx/edx come back from the cast
  exactly, and ebx/ebp/esi/edi from its pops.
- **Lazy flags.** The whole record: kind, operands, result, width, both override cells, and both stale cf/of cells.
  It is written by exactly the instructions that write it in the generated code: `inc` keeps only the carry override;
  `shl`/`shr` set both overrides; `neg`, `or r,-1`, most `xor r,r` and `and r,imm` write nothing. So `sbb ecx,ecx`
  after `neg` reads the carry of the previous record, as the translation does. Across a call only the stale cells
  travel: every function writes the whole record before it reads a flag, and every call returns to a flag-writing
  instruction. The cast's exit record is the top-level 88B80's.
- **x87.**
  - Slots d1..d4 below the entry TOP are locals updated like st[]. Dead values are kept: the plane interval's unrounded
    `last` at d2, `fstp st(2)`, `fstp st(0)`.
  - Arithmetic is in doubles with the translation's operand order and rounding points: float loads widened, float
    stores rounded, `-ffp-contract=off`. The Vita object has no `vfma`.
  - The status word is what `x87_compare` leaves: the condition codes of the last compare, with the TOP of every
    compare OR-ed in and never cleared.
- **Back-edge budget.** The loops of 889E0, 17ADD0 and 86E20 are the only back-edges. `c->preempt` drops by exactly
  the guest's count, and `xv_preempt()` is called the same number of times, but after the cast instead of mid-loop.
  That is a scheduling point only (as in `xk_native_visibility.c` / `xk_native_4b9d0.c`). A preemption inside an
  object job therefore stops the job after the cast, not in it.
- **Recursion.** 88B80's recursion runs as an explicit stack of levels (`nr_lvl`: entry esp, arguments, frame
  translation, the caller's registers it restores, the split t, which call returned). The guest state stays in one
  set of locals across levels, as it is one machine state in the guest. A level deeper than 1,024 hands its subtree to
  the translated `f_00088B80` on the guest context: the state is the guest's at every call boundary, so this is exact,
  and the cached values are read again afterwards. The in-game maximum was 64 levels.

**Not reproduced** (unobservable in the game):
- NaN payload bits. Which operand's payload an operation propagates depends on the host compiler's operand order, and
  the guest bodies built at -O0 and -O2 already differ. Such values only reach frames, x87 slots and the result t. The
  tests accept a NaN word against a NaN word and count them.

**Declines** (the translation runs):
- esp not 4-aligned, or near the ends of the address space.
- The result record overlapping W = [esp - 64 KB, esp + 0x18) (every frame of the cast, S and 88E90's arguments), or
  its t straddling a page end.
- The BSP header, the point or delta vector, or the image constants overlapping W or the record.

In the game runs below nothing was declined.

## Knobs and counters

- Build: `XV_NATIVE_1721B0=1`. The Makefile block after `XV_NATIVE_4B9D0` sets `-DXV_NATIVE_1721B0=1`,
  `XV_NATIVE_1721B0_DEFAULT` (0), and `-ffp-contract=off` for the unit. `games/halo_ce_3925/runtime.mk` adds the source,
  and `recomp/kernel/xd3d.c` makes one weak report call.
- Hook: `python3 tools/patch_native_1721b0_hooks.py <stage>/recomp` (idempotent). At the top of `f_00088E90`, inside
  `#if defined(XV_NATIVE_1721B0) && XV_NATIVE_1721B0`, it adds
  `{ extern int xv_native_1721b0_ray(xctx *); if (xv_native_1721b0_ray(c)) return; }`. It returns 0 (the translated
  body runs) when the knob is off without timing, when the layout is declined, and for its own call of the translation
  (verify, timing). `tools/install_native_1721b0.py <stage>` does all of it: unit, Makefile, runtime.mk, xd3d.c, hook.
- Env `XV_NATIVE_1721B0`:
  - 0: off (default).
  - 1: verify. The native runs with a write journal. Its result is recorded (registers, flags, x87, back-edges, every
    byte it wrote, the regions the guest may write) and undone from the journal. Then the translation runs on the same
    state with an unbounded budget and everything is compared (NaN words equal). The guest's result is kept and its
    budget applied.
  - 2: native.
- Env `XV_NATIVE_1721B0_TIME=1`: ns clock per call (Vita: us clock) of the translation in mode 0 (timed by the hook),
  the native in mode 2, and both in mode 1. It also turns on the per-call detail counters.
- Report, every 60 frames:
  `[native-1721b0] 60 frames: calls N verified N mismatched N (total mismatches N) declined N journal-fail N; from 1721B0 N 1731D0 N other N; nodes .. leaves .. refs .. polygons .. edges .. bsp2d-steps .. hits .. back-edges .. delegated .. max-depth .. nan-words ..; us/call native X guest Y (N timed)`.
  Mismatches print up to 12 `MISMATCH <what> native .. guest ..` lines.
- Host harness only: `XV_NATIVE_1721B0_CAPTURE=<file>[:n[:skip]]` writes the arena and page table once, then n entry
  states (xctx and the stack within 64 KB of esp) for `--replay`. Captures are private (game memory) and stay out of the
  repository.

## Verification

**Differential tests.** `tools/test_native_1721b0.py <stage>/recomp [cases] [--seed N] [--verify] [--mutants N]
[--bench N] [--replay FILE --reps N]`, with the driver `tools/tests/native_1721b0.c`. The test takes the stage's lifted
bodies of the five functions and `xv_bsp_plane_interval` from `kernel/xk_geometry.c` (read only). It builds the native
three ways: plain page table -O2, per-thread table + render view -O2, and -O0. The randomized cases run in an 8 MiB
synthetic arena with shuffled tag pages and reversed stack pages. Per case, the guest and the native (or the guest,
when the native declines) start from identical state, and the whole arena, every xctx field and the xv_preempt call
count are compared. `--verify` also runs mode 1 and requires the guest's result and no MISMATCH line. The scenes
include:
- Random BSP3D trees whose planes straddle the segment, and chains of planes across it: more than 256 leaves reach the
  overflow slot, and more than 1,024 levels reach the translated subtree.
- Leaves of both kinds and -1, with random S flags (the three leaf-kind rules).
- References on the crossed plane and on others, BSP2D trees, and surfaces whose closed edge rings lie around the
  crossing point (either orientation) or beside it. The surface filter bits and the caller's mask are random.
- Zero, axis-parallel and NaN/infinite coordinates. Mixed magnitudes (~1e16 next to 1) that make the plane sums'
  association visible.
- Random 0.0/1.0 constants and axes table (indices outside 0..2 read other frame slots), and random t limits
  (FLT_MAX, < 1, negative, 0, NaN).
- Layouts:
  - The game's: the record and vectors in the caller's frame.
  - The record elsewhere, or with its t or leaf count straddling a page end.
  - The vectors straddling page ends.
  - The declined ones: the record over S or the frames, the vectors over the record or S, esp unaligned.

| stage (lifted bodies) | seed | variant | cases | native | declined (guest) | mismatches | verify mode | NaN-payload words |
|---|---|---|---|---|---|---|---|---|
| `overlap-candidate/build-x87` | 11 | plain -O2 | 20,000 | 14,304 | 5,678 | 0 | 0 failures | 79 |
| same | 11 | thread table + render view -O2 | 20,000 | 14,304 | 5,678 | 0 | 0 failures | 79 |
| same | 11 | plain -O0 | 20,000 | 14,304 | 5,678 | 0 | 0 failures | 36 |
| same | 12 | plain -O2 / thread table -O2 / -O0 | 3 x 20,000 | 14,316 | 5,662 | 0 | 0 failures | 71 / 71 / 35 |
| `overlap-candidate/build` | 13 | plain -O2 / thread table -O2 / -O0 | 3 x 20,000 | 14,373 | 5,613 | 0 | 0 failures | 74 / 74 / 64 |
| same | 14 | plain -O2 / thread table -O2 / -O0 | 3 x 20,000 | 14,393 | 5,587 | 0 | 0 failures | 75 / 75 / 25 |

Each run of 20,000 also skipped 14-22 cases whose guest never finishes (a ring the fix-up cannot close) past a 6 s
alarm. Per -O2 run, the native's own counters show: ~330,000 BSP nodes, ~240,000 leaves, ~9,000 matching references,
~8,500 polygon tests with ~15,000 edge steps, ~950 hits, 164-183 leaf-list overflows, 60-72 subtrees handed to the
translation, and max depth 1,024. Line coverage of the unit (gcov, 20,000 cases with `--verify`) was 96 %. Every line
of the five transliterated functions ran. The unexecuted lines are report and log formatting, the timing branch of the
hook, and allocation failures.

**Mutants.** `--mutants 6000`, plain -O2, seed 5. Each mutant must make at least one case differ; a crash or a hang
counts.

32 of 34 were caught at 6,000 cases:
- plane-interval delta from the point (3,630 of 6,000 cases differ)
- `last` from the unrounded delta (1,667)
- dead slot d2 not written (1,647)
- split t not negated (2,241)
- far-child test `je` for `jnp` (2,241)
- near child `sete` inverted (2,178)
- fsw TOP replaced instead of OR-ed (2,974)
- fcom at the wrong depth (206)
- leaf list capacity 0xFF (46)
- `inc al` without its carry override (1)
- leaf-list compare operands swapped (3,158)
- leaf-kind rule 2 dropped (281)
- hit record flags byte skipped (145)
- hit plane stride 8 (136)
- S+0x20 wrong kind (3,591)
- S+0x1C not tracked (7)
- leaf count not tracked (1,671)
- level stack: the caller's t1 not restored (2,239)
- `sbb` as real x86 instead of the translation's stale carry (165)
- projected v from the u slot (6)
- 17ADD0 back-edge not counted (81)
- 17ADD0 side test inverted (160)
- 86E20 cross product operand swap (94)
- 86E20 `fstp st(2)` not modelled (140)
- 86E20 back-edge not counted (129)
- 889E0 reference back-edge not counted (528)
- 889E0 dominant-axis tie (133)
- t limit 1.0 literal off by one ulp (2,989)
- layout: record allowed in the upper frames (94)
- layout: vectors allowed over the record (122)
- layout: the record's t across a page end (2)
- delegation: back-edges not counted (1)

The plane-distance association (`(a + b) + c` as `a + (b + c)`) was caught in a targeted 20,000-case run (1 case). The
sums of exact float products only matter through the float the guest stores, so only the mixed-magnitude scenes show
it.

Not caught: the stale carry cell not passed into a translated subtree. Every translated subtree rewrites the cell (the
plane interval's `shl` at every node) before anything can read it or it can reach the cast's end, so the mutant is
equivalent in all but constructed cases.

Two earlier mutants were equivalent by construction and were replaced:
- `and edx,1` without operands: that record is always overwritten before the cast ends, and its operands cannot reach
  CF or OF of a logic record.
- The hit's `add esi,edi` without flags: the parent's `test al,al` overwrites the record, and the cells are the `shl`'s
  either way.

**Replay of captured game calls** (`--replay`, x86 and Pi). 2,000 a30 casts captured while moving: 186 from 1721B0,
1,814 from 1731D0, 505 with a hit. Each was compared from its own entry state (xctx, the stack within 64 KB of esp,
the result record): 0 mismatches on x86 and on the Pi.

**In-game verify.** `XV_NATIVE_1721B0=1`, every cast compared. The runs load a30 from its start
(`XV_LEVEL=a30`, Campaign -> New game) with the scene on the helper (overlap mode 2).

| run | where | pad | frames | casts compared | mismatches | declined | NaN words |
|---|---|---|---|---|---|---|---|
| ver1 | x86 host, first version | moves to frame ~12,000 | 26,760 | 7,215,466 | 0 | 0 | 0 |
| pver1 | Pi 4, cores 0-1, first version | same | 53,640 | 10,532,106 | 0 | 0 | 0 |
| ver3 | x86 host, final build | same | 71,700 | 14,462,153 | 0 | 0 | 0 |
| pver3 | Pi 4, cores 0-1, final build | same | 80,400 | 21,522,127 | 0 | 0 | 0 |
| ver4 | x86 host, final build | walks the whole run | 71,700 | 15,453,212 | 0 | 0 | 0 |
| pver4 | Pi 4, cores 0-1, final build | walks the whole run | 51,120 | 18,643,343 | 0 | 0 | 0 |

Total: 87,828,407 casts, all of them compared. On the final build: 29,915,365 on x86 and 40,165,470 on ARM.

In the first four runs the pad moved the player until ~frame 12,000. The harness takes at most 64 pad events, and those
scripts spent them early. ver4/pver4 walk forward for the whole run, with turns, jumps and fire at intervals.

In pver4 the Pi's run took the player elsewhere, and 4,011,845 casts (22 %) came from 88E90's other callers (363C0,
49170, 13A190, 1580A0, 159EB0, 168460), not from 1721B0 or 1731D0.

Per cast over the final runs: 20-34 BSP nodes, 3.5-6.4 leaves, 0.27-0.63 hits, at most 64 levels. No cast was
declined, none needed the translated deep subtree, and no NaN words appeared.

## Cost, guest vs native (host-side; not Vita numbers)

**In the game** (`XV_NATIVE_1721B0_TIME=1`: an ns clock around every cast, call-weighted over the windows after the
first 20). Mode 0 times the translation: the hook calls the translated `f_00088E90` between two clock reads. Mode 2
times the native, including the hook. The pad walks forward for the whole run, with turns, jumps and fire. The two runs
of a pair run side by side.

| where | pair | casts timed | guest (translation) | native | ratio |
|---|---|---|---|---|---|
| x86 host (-O1 harness) | `XV_LOCKSTEP=2` (virtual clock: the same ticks and casts in both runs; the camera paths matched), mode 0 on CPUs 0-3, mode 2 on CPUs 4-7 (one CCD, no SMT sibling busy), 1,500 s, 741 windows | 6,982,635 / 6,983,060 | 2.231 us | 1.233 us | **1.81x** |
| Pi 4 (-O1 harness, ARM) | `XV_LOCKSTEP=2`, mode 0 on core 0, mode 2 on core 1 (each run with all its threads on its core), 1,800 s, 864 windows | 7,138,926 / 7,282,868 | 14.692 us | 6.657 us | **2.21x** |

Per frame that is 161 casts on x86 (0.36 ms of translation, 0.20 ms native) and 141 on the Pi (2.07 ms, 0.94 ms). The
Pi pair is not reproducible call for call (the runs share each core with their own helper threads, and the camera
paths drifted apart slightly). Its call mix matched to 2 %: 32.3 nodes, 5.5 leaves and 0.53 hits per cast in the
native run. Of those casts, 45 % (Pi) / 41 % (x86) came from 1721B0 and the rest from 1731D0.

**Replay** (the 2,000 captured a30 casts, each from its own entry state, guest and native passes alternating; perf-counter
user instructions and cycles per call). The Pi build is `-O2 -mthumb -march=armv7-a`, like the Vita's.

| where | guest ns/call | native ns/call | ratio | instructions guest / native | cycles guest / native |
|---|---|---|---|---|---|
| x86 (50 reps) | 905 | 386 | **2.35x** | 14,500 / 5,916 (2.45x) | 5,289 / 2,458 (2.15x) |
| Pi 4, A72 (50 reps) | 7,509 | 4,364 | **1.72x** | 14,856 / 7,450 (1.99x) | 13,988 / 8,348 (1.68x) |
| Pi 4, A72 (1,000 reps, -g build) | 7,918 | 4,410 | **1.80x** | 14,926 / 7,452 (2.00x) | 14,604 / 8,359 (1.75x) |

The game's Pi ratio is higher than the replay's. In the replay the casts run back to back, so the BSP data and the code
stay in cache, and both sides wait on the same node and plane loads. In the game, the tick between casts evicts them,
and the translation's code (five large translated functions, the xctx traffic) misses more than the native's 24 KB.

## Expected on the Vita (estimate, to be measured)

On the Vita in a30 (perf192), 1721B0 costs ~28 ms/frame in the sound pass and ~4.5 ms/frame in particle collision,
~32.5 ms together. The sound rays are 94 % of 1721B0's time in the probe.

The estimate combines:
- The cast's share of the subtree: 54 % (Pi sampler; 59 % on x86).
- The cast speed-up: 1.8x at the conservative end (the Pi replay and the x86 pair) to 2.2x (the Pi's in-game pair).

The saving is 32.5 x 0.54 x (1 - 1/1.8 ... 1/2.2) = 7.8 ... 9.6 ms, about **6-9 ms per frame** allowing for the A9 being
narrower than the A72. Projectiles, AI lines of sight and the biped physics call 1721B0 inside the tick too, and the
same hook serves them. With `XV_SOUND_OBSTRUCTION` on, only the sound rays that are recomputed remain, and the sound
pass's part of the saving scales with them.

The A9 has smaller caches and slower memory than the A72, and the cast's node and plane loads stay one for one (the
exactness bar). Its ratio should therefore be nearer the cycle ratios above than the instruction ratios. A Vita
measurement would settle this: `XV_NATIVE_1721B0=2` against `0` with `XV_NATIVE_1721B0_TIME=1`, or the 2B460/108FD0
tick timers.

## Vita integration (overlap-candidate stage)

Files on branch `work/native-1721b0-20260924`, on top of `0ed288e`:

1. `recomp/kernel/xk_native_1721b0.c` (new). The Vita object is 24.7 KB of text and 108 B of BSS. Per thread there is
   a TLS journal, and the level stack (1,024 x 40 B on the Vita) is allocated on first use.
2. `Makefile`: the `XV_NATIVE_1721B0` block (flag, `_DEFAULT`, the `-ffp-contract=off` rule).
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_1721b0.c` under
   `ifeq ($(XV_NATIVE_1721B0),1)`.
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_1721b0_report(60)` after `xv_native_4b9d0_report`.
5. The shard that defines `f_00088E90` (code_013.c in this stage): the entry hook.

Steps for `overlap-candidate/build-x87` (the same for `overlap-candidate/build`):

1. Run `python3 tools/install_native_1721b0.py overlap-candidate/build-x87` from this checkout. It copies the unit and
   adds the Makefile block after the stage's `xk_native_4b9d0.o` -ffp-contract rule (falling back to the 92330 or
   visibility one), the runtime.mk line after the 4B9D0 line, and the xd3d.c call after the 4B9D0 report call. It
   prints `hook installed in f_00088E90`, and it is idempotent. It was tried here on a copy of the x87 stage, twice.
2. Add `XV_NATIVE_1721B0=1` to `overlap-candidate/make-vars.txt`, and to the make variables in `build-command.json` if
   the build driver takes them from there. `code_013.c`, `xd3d.c` and the new unit rebuild by mtime (`code_013.c` is
   compiled with `RECOMP_CFLAGS`, which carry `-DXV_NATIVE_1721B0=1`).
3. Check:
   - `arm-vita-eabi-nm build/recomp/code_013.o | grep 1721b0` shows `U xv_native_1721b0_ray`.
   - `arm-vita-eabi-nm build/recomp/kernel/xd3d.o | grep 1721b0` shows `w xv_native_1721b0_report`.
   - The ELF has `T xv_native_1721b0_ray` and `T xv_native_1721b0_report`.
   - `arm-vita-eabi-objdump -d build/recomp/kernel/xk_native_1721b0.o | grep -c vfma` is 0.
4. Runtime:
   - First run `XV_NATIVE_1721B0=1 XV_NATIVE_1721B0_TIME=1` and expect `mismatched 0`. The frame pays for the guest, the
     native and the journal.
   - Then compare `XV_NATIVE_1721B0=2` against `0`, either with `XV_NATIVE_1721B0_TIME=1` (the us/call pair) or with the
     tick timers: 2B460 / 108FD0 should drop by roughly the estimate above.
   - Once hardware agrees, `XV_NATIVE_1721B0_DEFAULT=2` in the make vars makes it the default.

Checked here with the Vita command lines from `make -n` of the installed x87-stage copy (`arm-vita-eabi-gcc -O2
-mthumb -mcpu=cortex-a9 -mfpu=neon`, `-ffp-contract=off` for the unit):
- The unit, the hooked `code_013.c` and `xd3d.c` compile.
- `code_013.o` references `xv_native_1721b0_ray` from `T f_00088E90`.
- `xd3d.o` has the weak `xv_native_1721b0_report`.
- There is no `vfma` in the unit's object.

## Findings along the way

- The pair timers the earlier natives used for attribution are too coarse here. 88B80 runs ~8,800 times per frame on
  x86, and two clock reads per call doubled the probe's 1721B0 time. The shares above come from the sampler.
- The first transliteration (recursion as C recursion, everything saved and restored around every call, S and the BSP
  header read from memory at every node) was 1.8x on x86 and 1.55x on the Pi on the replay.
  - Proving the layout (record, header, vectors and constants apart from W and each other) and caching those values
    gained a little.
  - Turning the recursion into an explicit level stack, so the guest state stays in one set of locals, gained most.
  - Cutting the flag record's live ranges to its stale cells across levels lowered register pressure; on the A9
    build, the spills to the C stack had been a sixth of the code.
- The cast is memory bound on the Pi 4. In a SIGPROF profile of the replay, 41 % of the native's samples are the loads
  of BSP nodes and planes, which the guest makes one for one (the exactness bar). The A72's cycle ratio (1.75x) trails
  the instruction ratio (2.0x). The in-game ratio is lower than the replay's: in the game, the tick between casts
  evicts the BSP data.
- The record's t (written as a float by 88E90, as an integer at a hit, read as a float by the far test) is read back
  exactly only while it does not straddle a page end. That case is now declined; one delegated subtree in the tests
  showed it.
- The harness's scripted pad takes at most 64 events. An a30 script that spends them in the first 12,000 frames leaves
  the rest of a long run standing still.

## Not done / next

- Not built into a VPK, not run on the Vita or in Vita3K. No Vita timing.
- The object walk `f_00171AF0` and its callees (22-28 % of the subtree; B0CB0 already native), and 1721B0's own body
  (10 %), are the next candidates. 17A8B0 (8 %) is shared with 53 callers.
- 86E20 (the polygon test) was never reached in a30. The differential tests cover it (~8,500 polygon tests per run),
  but the game has not exercised it.
- A version compiled without the verify journal checks would save a test per store.
