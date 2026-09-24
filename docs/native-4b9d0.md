# Native collision work under f_0004B9D0 (`XV_NATIVE_4B9D0`)

Sept 24 2026. Branch `work/native-4b9d0-20260924` (not pushed). Status: implemented and verified; default off; not
built into a VPK or measured on the Vita.

Two subtrees of the biped physics update `f_0004B9D0` (9.1-9.8 ms per rendered frame on the Vita in the a10
checkpoint, perf187: ~19 calls per frame at ~1.7 ticks per frame) are native in `recomp/kernel/xk_native_4b9d0.c`:

1. **the BSP sphere query** `f_00088110` (+ `87EA0`, `87E10`, `86F50`, `B0CB0`), 41 % of 4B9D0 on the Pi 4 and 56 % on
   the x86 host, in place of the fused guest query (`recomp/query_fusion.c`);
2. **the solver's feature test** `f_000864C0` (+ `85D10`, `85A00`, `85720`, `11120`, `111A0`), 17 % of 4B9D0 on both,
   at the fused solver's call (`recomp/solver_fusion.c`).

Evidence (details below): 0 mismatches in 3,591,664 in-game calls compared in verify mode (x86 host and Pi 4, both
parts), in 360,000 differential-test cases against the lifted bodies of both stage flavours, and in 2,000 + 2,000
captured game calls replayed on x86 and on the Pi; 35 of 39 deliberately broken variants caught (the four left are
explained under "Mutants"). Speed against what the game runs today (the fused copies): the query 2.09x on x86 and
1.57x on the Pi 4 (replay of captured a10 queries against the fused query), 1.46x in an x86 lockstep game pair,
1.56x in a Pi game pair; the feature test 3.26x / 3.11x (x86 / Pi) against the plain translation, 1.94x in the x86
lockstep game pair against the fused solver copy (3.23x in the Pi pair, whose runs diverged). Expected on the Vita:
about 2.1-2.4 ms per frame less owner tick time inside 4B9D0, and up to ~1.6-1.9 ms more from the other objects'
collision moves through the same code (estimate, to be measured).

## Where the time goes

Probe stage: a phase-pair timer (ns, caller > callee) around every call in the chain below `f_0004B9D0` and around
the calls of the functions that hold the time (the `xv_phase` pair timers of docs/native-92330.md; the probe build is
a copy of the x87 stage, not in the repository). Host harness with the a10 checkpoint save and the look/move/fire pad
script, scene on the helper with overlap mode 2, windows after frame 1500. Inclusive us/frame (share of 4B9D0):

| function | role | x86 host (179 windows) | Pi 4 (706 windows) |
|---|---|---|---|
| `4B9D0` | biped physics update, 11.3 calls/frame | 296.0 | 2683 |
| `49600` | collision move | 273.5 (92 %) | 2310 (86 %) |
| `172BF0` | move with collision | 266.5 (90 %) | 2140 (80 %) |
| `171F10` | feature collection | 206.8 (70 %) | 1566 (58 %) |
| **`88110`** | **BSP sphere query** | **165.5 (56 %)** | **1096 (41 %)** |
| `868F0` | BSP features from the query's lists | 19.4 (6.5 %) | 256 (9.5 %) |
| `172040` | object walk (calls 87EA0 per object) | 20.2 (6.8 %) | 180 (6.7 %) |
| `170C10` | solver | 58.2 (20 %) | 541 (20 %) |
| **`864C0`** | **feature test, 14.4 calls/frame** | **52.5 (17.7 %)** | **445 (16.6 %)** |
| `8E970` | 4B9D0's other large child | 9.3 | 127 |

Inside the query (Pi, us/frame): `87EA0` BSP3D traversal 250 self (198 calls/frame, recursive), `87E10` BSP2D 97 self,
`86F50` surface test 627 self + `B0CB0` segment/sphere 150 (556 calls/frame). Inside the feature test: 864C0's own
loop 151, `85720` plane prisms 172 (92.6 calls/frame), `85A00` capsules 73 (68.7), `85D10` spheres 49 (62.9).
(The host runs one tick per frame; the Vita ~1.7, so its per-frame counts are ~1.7x these.)

## What was replaced, and why these boundaries

| function | role | disposition |
|---|---|---|
| `f_00172BF0`, `171F10`, `170C10`, `49600` | collision move, collection, solver: bodies small (1-8 % self), many guest calls | guest (fused copies) |
| **`f_00088110`** | **query set-up on its frame (query record, result lists +0/+404/+808/+C0C cleared), then 87EA0** | **native** |
| **`f_00087EA0`** | **BSP3D traversal (recursive): sphere vs node planes, leaves into the leaf list (<= 256), per leaf its BSP2D references, planes on the ancestor stack projected** | **native** |
| **`f_00087E10`** | **BSP2D traversal (recursive): circle vs 2D node lines** | **native** |
| **`f_00086F50`** | **surface test: tested bit, vertex ring (SSE distances, vertex list), edges (B0CB0, edge list), point in polygon (surface list)** | **native** |
| **`f_000B0CB0`** | **segment vs sphere** | **native** |
| `f_000868F0` | BSP features from the lists (9.5 % Pi) | guest: next candidate |
| **`f_000864C0`** | **loop over the collected features, earliest hit facing the motion, result record** | **native** |
| **`f_00085D10` / `85A00` / `85720`** | **swept sphere vs sphere / capsule / plane prism (polygon edges on the projected axes)** | **native** |
| **`f_00011120` / `000111A0`** | **normalize / t * a + b** | **native** |

Both subtrees call nothing else and no HLE; each is the smallest subtree holding its cost. The query is hooked where
`recomp/kernel/xk_query_reuse.c` calls the fused query `query_fused_172c95_171f94` (five sites: declined, busy,
unlocked lanes, original, failed capture), so the query-reuse and world-run admission around it stay; the feature test
at the fused solver's 170CD1 call. Existing collision natives are not duplicated: the fused query already carries
four partial natives (`XV_NATIVE_BSP_SPHERE`, `_COLLISION_TRAVERSAL`, `_COLLISION_VERTICES`, `_SEGMENT_SPHERE`: the
sphere-plane arithmetic, the scalar decisions of 87EA0/87E10, 86F50's vertex pass, B0CB0), which remain the path
whenever the native declines; the native replaces the whole fused subtree, and every speed figure here is against
the fused query with those four on (the game's configuration). The fused solver has no partial natives.

## Semantics reproduced (exact)

The unit is a transliteration of the generated code (`c_*` listings of both stages are identical for these ten
functions), not a re-derivation:

- every guest read and write happens at the same program point, in the same order, at the same address and through
  the same translation as the generated code: integer, SSE and push/pop accesses one translation (`X_M32`,
  `X_MF32`, `X_PUSH32`: a 4-byte access at a page end continues in the same host page), x87 float loads and stores
  page-split like `x87_load_f32` / `x87_store_f32`. Per record, frame and result-list page the translation is taken
  once when the record lies in one page (a cached page-table lookup, never a cached value);
- a value the guest reads back from memory comes from a local only where no store but the owner's can have reached
  it: the query's frame slots and its query record, until a store lands in the stack window or the lists run over
  (then `dirty` is set and every later such read comes from guest memory; the ancestor stack deeper than 88110's
  frame and list stores into frames are tested); the feature test's frame slots `[E-0x90, E+0x14)`, whose only other
  writer, the result record, is checked to lie elsewhere (else the guest runs). Guest data (BSP records, features,
  start, dir) are read from guest memory at the guest's points, so aliasing with frames reads what the guest reads;
  the exceptions are the BSP header and the image constants (0.0, 1.0, eps, the facing threshold, the axes table),
  read once per call after the layout check has shown that no store of the call can reach them; the feature test's
  tail from the first record store on is literal;
- registers: every guest register is a local assigned where the generated code assigns it (partial writes
  included: `fnstsw ax`, `setge al`, byte xors, `mov cx,[..]`); callee-saved registers come back from the pops;
  where a register is not modelled it is provably dead (the feature tests' eax/ecx/edx after return: every exit
  path of 864C0 reloads them);
- the lazy-flag record: kind, operands, result, width, both override cells and both stale `f_cf`/`f_of` cells,
  written by exactly the instructions that write it in the generated code (the emitter drops dead flags: `neg`,
  most `xor r,r`, `and`, `add`, `or edi,-1`), including `inc`/`dec` that keep only the carry override, `imul`
  (`x_imul32`: both overrides) and `shl` (`x_shl32`);
- x87: the slots below the entry TOP are depth-indexed locals updated like `st[]` (pushes, `fxch`, `fstp st(1)`,
  `faddp st(2),st`, dead values kept; the query writes depths 1-5, the feature test 1-6); doubles with the guest's
  operand order and rounding points (float loads widened, float stores rounded, `-ffp-contract=off`; the Vita object
  has no fused multiply-add - its `vmla`/`vmls` are the VFPv3 chained forms, separately rounded); the status word as
  `x87_compare` leaves it: condition codes of the last compare, the TOP of every compare OR-ed in, never cleared;
- SSE (the query's vertex pass): xmm0, xmm1, xmm2[0] in float arithmetic in the translation's lane order;
- the back-edge budget: `c->preempt` drops by exactly the guest's back-edge count and `xv_preempt()` is called the same
  number of times, at the end of the call instead of mid-loop (a scheduling point only, as in
  `xk_native_visibility.c`).

Not reproduced (unobservable in the game): NaN payload bits (which operand's payload an operation propagates is the
host compiler's operand order; guest bodies built -O0 and -O2 already differ). Such values only reach frames, x87
slots and result floats; the tests accept a NaN word against a NaN word and count them. Where frame words would be read
back as integers that steer the control flow (the feature block's counts or prism fields inside the frames: a
generated test layout that made the loop count depend on NaN payload bits) the feature test declines; in the game the
feature block is in the solver's frame above (features = E + 0xD4, start E + 0x68, dir E + 0x50, record E + ~0xADB0 in
all 2,000 captured calls). Declines (the fused code runs): the scene helper thread, a thread whose page table is not
the live one (the fused code translates integer accesses through the live table and x87 accesses through the
thread's), esp not 4-aligned or near the address-space ends, and the query's list/header layout checks
(`n4_layout`) or the feature test's (`n5_layout`). In the game runs below nothing was declined.

## Knobs and counters

- Build: `XV_NATIVE_4B9D0=1` (Makefile block after `XV_NATIVE_606B0`: `-DXV_NATIVE_4B9D0=1`,
  `XV_NATIVE_4B9D0_DEFAULT` (0), `-ffp-contract=off` for the unit; `games/halo_ce_3925/runtime.mk` adds the source;
  one weak report call in `recomp/kernel/xd3d.c`).
- Hooks: `python3 tools/patch_native_4b9d0_hooks.py <stage>/recomp` (idempotent): in `kernel/xk_query_reuse.c` after
  the fused query's declaration, `#define query_fused_172c95_171f94 xv_native_4b9d0_query` (5 call sites); in
  `solver_fusion.c` the declarations, at the 170CD1 call
  `if (xv_native_4b9d0_features_on()) { NS_PUBLISH(); if (xv_native_4b9d0_features(guest)) { NS_RELOAD(); goto NS_CONT_1; } }`
  plus a timing stamp, and a timing call after `NS_CONT_1:`. All inside `#if defined(XV_NATIVE_4B9D0) && XV_NATIVE_4B9D0`.
  `tools/install_native_4b9d0.py <stage>` does all of it (unit, Makefile, runtime.mk, xd3d.c, hooks).
- Env `XV_NATIVE_4B9D0`: 0 off (default), 1 verify, 2 native. `XV_NATIVE_4B9D0_PARTS`: 1 the query, 2 the feature test,
  3 both (default). Verify: the native runs with a write journal, its result is recorded (registers, flags, x87, SSE,
  back-edges, every byte it wrote, the regions the guest may write), undone from the journal, then the guest runs on
  the same state with an unbounded budget (the fused query; the translated `f_000864C0`), everything is compared
  (NaN words equal), the guest's result is kept and its budget applied.
- Env `XV_NATIVE_4B9D0_TIME=1`: ns clock per call (Vita: us clock) of the guest in mode 0 (the fused query; the fused
  solver's inline feature test, between the call and its continuation), the native in mode 2, both in 1; also the
  per-call detail counters.
- 60-frame lines from xd3d.c's report block: `[native-4b9d0] 60 frames: calls N verified N mismatched N (total
  mismatches N) declined N journal-fail N; nodes .. 2d-nodes .. leaves .. surfaces .. edges .. vertices ..
  back-edges .. scans .. scanned .. dirty .. layout-declined .. nan-words ..; us/call native X guest Y (N timed)` and
  `[native-4b9d0] features 60 frames: calls .. verified .. mismatched .. declined .. journal-fail ..; spheres ..
  capsules .. prisms .. hits .. edges .. back-edges .. layout-declined .. nan-words ..; us/call ...`. Mismatches
  print up to 12 `MISMATCH <what> native .. guest ..` lines per part.
- Host harness only: `XV_NATIVE_4B9D0_CAPTURE=<file>[:n[:skip]]` (query entry states: xctx, the stack +-64 KB, the
  arena once) and `XV_NATIVE_4B9D0_CAPTURE_FEATURES=...` (feature-test entry states: xctx, the stack +-8 KB, the
  feature block) for the replay tests. Captures are private (game memory) and stay out of the repository.

## Verification

**Differential tests** (`tools/test_native_4b9d0.py <stage>/recomp [cases] [--part query|features] [--seed N]
[--verify] [--mutants N]`, driver + `tools/tests/native_4b9d0.c`): the stage's lifted bodies of the ten functions
(extracted from the shards, only read) and the native built three ways (plain page table -O2, per-thread table +
render view -O2, -O0), randomized cases in an 8 MiB synthetic arena with shuffled tag pages and reversed stack pages;
per case guest and native from identical state, the whole arena, every xctx field and the xv_preempt call count
compared; `--verify` also runs mode 1 and requires the guest's result and no MISMATCH line.

- query scenes: random BSP3D heap trees (the ancestor stack running deeper than 88110's frame, both sides taken),
  leaves with BSP2D references whose plane words match the ancestor stack, BSP2D trees, surfaces with closed edge
  rings shared between surfaces, vertices inside / on / outside the sphere, NaN and infinite coordinates, random
  tested bits, "big" and "huge" scenes (radius 20-1000, every plane straddled: the four result lists reach their
  0x100 capacity), `wild` scenes (the ancestor stack runs over the frame into the result lists: negative surface
  counts, list stores into live frames, the `dirty` path), random zero constant and axes table, the result lists at
  the in-game place or elsewhere, and the declined layouts (esp or lists unaligned, lists in the frame);
- feature scenes: spheres, capsules and plane prisms placed across a random move (hits, grazes, misses, starting
  inside), zero / axis-parallel / NaN directions, zero and parallel capsule axes, planes parallel to the move,
  prisms with -1..12 vertices (beyond the record), axis words and side bytes outside the table (the table and the
  frame read at other indices), negative and large counts, random 0.0 / 1.0 / eps / facing constants; frames
  straddling a page end, start / dir inside the frames, the record over start / dir / the features, and the declined
  layouts (esp unaligned, the record or the feature block in the frames).

| part | stage (lifted bodies) | seed | variant | cases | mismatches | verify mode (mode 1) | NaN-payload words |
|---|---|---|---|---|---|---|---|
| query | `overlap-candidate/build-x87` (`--x87-regs`) | 1 | plain page table -O2 | 20,000 | 0 | 0 failures | 48 |
| query | same | 1 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 48 |
| query | same | 1 | plain -O0 | 20,000 | 0 | 0 failures | 50 |
| query | `overlap-candidate/build` (memory lowering) | 2 | plain page table -O2 | 20,000 | 0 | 0 failures | 3 |
| query | same | 2 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 3 |
| query | same | 2 | plain -O0 | 20,000 | 0 | 0 failures | 5 |
| query, with huge scenes | `build-x87` | 7 | plain page table -O2 | 20,000 | 0 | 0 failures | 6 |
| query, with huge scenes | same | 7 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 6 |
| query, with huge scenes | same | 7 | plain -O0 | 20,000 | 0 | 0 failures | 8 |
| query, with huge scenes | `build` | 8 | plain page table -O2 | 20,000 | 0 | 0 failures | 10 |
| query, with huge scenes | same | 8 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 10 |
| query, with huge scenes | same | 8 | plain -O0 | 20,000 | 0 | 0 failures | 10 |
| feature test | `build-x87` | 11 | plain page table -O2 | 20,000 | 0 | 0 failures | 1,350 |
| feature test | same | 11 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 1,350 |
| feature test | same | 11 | plain -O0 | 20,000 | 0 | 0 failures | 291 |
| feature test | `build` | 12 | plain page table -O2 | 20,000 | 0 | 0 failures | 1,170 |
| feature test | same | 12 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 1,170 |
| feature test | same | 12 | plain -O0 | 20,000 | 0 | 0 failures | 288 |

Per query run: ~2,000 big scenes (in seeds 7 and 8 a third of them huge), ~800 deep, ~400 wild; ~4,850 cases in layouts the native
declines (checked to leave the state untouched: the guest runs); 450-570 wild cases whose guest never terminates
(a corrupted ancestor stack loops the traversal) are skipped past a 1 s alarm. In the huge-scene runs the surface,
edge, vertex and leaf lists reached their 0x100 capacity in 377/592/287/277 (seed 7) and 416/618/318/306 (seed 8)
cases. Per feature-test run: ~16,300 cases run natively (and in verify mode), ~3,700 in declined layouts, ~11,300
hits; 0 guest timeouts. The two stage flavours' lifted bodies of the ten functions are identical (the runtime
headers differ in the parity helper only), so the second stage is a second seed more than a second reference.
The first seed-12 feature run found the NaN-payload dependence described above (1 case in 20,000, -O2 builds only);
the layout check that declines it came before the runs in this table.

**Mutants** (`--mutants 1500`, plain -O2, seed 5): each must make at least one case differ, a crash or
a hang counts.

- the query, 16 of 20 caught: plane distance association (637 of 1,500 cases differ), vertex distance association
  (47), vertex list capacity 0xFF (9), fast-scan back-edges off by one (97), vertex scan missing the last element (55),
  edge scan not-found back-edges (195), surface scan skipping the first element (45), ancestor side bit (177), fsw TOP
  replaced instead of OR-ed (657), segment test fcomp depth (17), segment test back-edge not counted (207), `neg`/`sbb`
  as real x86 instead of the translation's stale carry (132), dead slot `fstp st(1)` skipped (5), projected point u/v
  swapped (231), xmm0 lane order (226), lists allowed inside the frame (1). Not caught: two mutants of the `dirty`
  fallback (the pops of 87EA0 ignoring `dirty`; a stray list store not setting it), also not in 20,000 cases each
  (seed 21): the wild scenes do set `dirty` (~250 cases per 20,000-case run, all exact), but none of their stray list
  stores lands on a frame word that is read back afterwards, so the fallback reads only meet unchanged words - the
  game never sets `dirty` (0 in 1.7 M queries); the stale carry cell after the list-count increment (every generated
  query overwrites it with a later inc/dec before its exit); and the leaf-capacity compare signed vs unsigned
  (different only for a negative leaf count, which no generated scene produces).
- the feature test, 19 of 19 caught: sphere closing-speed sum association (3), radius squared rounded to float (45),
  closing speed not stored over the argument (25), `fxch` dropped (82), normalize eps compare at the wrong depth (6),
  short vector keeping its length (32), capsule t2 clamped to a literal 1.0 instead of [1F0A78] (5), capsule
  back-edge not counted (193), capsule normal scaled by A instead of aa (339), prism edge loop one short (622), last
  edge not wrapping (499), projected direction u/v swapped (751), second `shl` flags (31), parallel-edge sign test
  (270), x87 slot 6 not written (916), facing threshold 0 (118), inner back-edge not counted (1,178), capsule record
  base (244), imul OF override not set (440).

**Replay of captured game calls** (`--replay` / `--replay-features`, x86 and Pi): 2,000 a10 queries (captured while
moving, frames 2,640-2,880 of the scripted run) compared against the plain translation and against the game's fused
query (`--fused`: the harness's `query_fusion.o` and the collision natives it links), 0 mismatches; 2,000
feature-test calls (frames 2,460-2,580; 1,298 with a hit) against the translated `f_000864C0`, 0 mismatches.

**In-game verify** (`XV_NATIVE_4B9D0=1`, every call compared; a copy of the Vita checkpoint save, Continue at the a10
checkpoint, the look/move/fire pad script, scene on the helper with overlap mode 2):

| run | where | frames | query calls compared | feature-test calls compared | mismatches | declined | NaN words |
|---|---|---|---|---|---|---|---|
| ver3 | x86 host, query-only build (cfae659) | 20,640 | 191,070 | - | 0 | 0 | 0 |
| ver4 | x86 host, both parts (2cdd282 before its layout check) | 35,040 | 323,476 | 501,848 | 0 | 0 | 0 |
| ver5 | x86 host, final build | 26,040 | 240,305 | 367,088 | 0 | 0 | 0 |
| pver1 | Pi 4, cores 2-3, query-only build (cfae659) | 34,620 | 321,891 | - | 0 | 0 | 0 |
| pver2 | Pi 4, cores 2-3, both parts (as ver4) | 43,740 | 406,020 | 622,381 | 0 | 0 | 0 |
| pver3 | Pi 4, cores 2-3, final build | 25,860 | 240,567 | 377,018 | 0 | 0 | 0 |

Total 3,591,664 calls (1,623,787 x86, 1,967,877 ARM; with the final build 607,393 x86 and 617,585 ARM), 0 mismatches.
The query code has not changed since cfae659; the layout check ver4/pver2 lacked declines only layouts that do not
occur in the game. Per a10 call (Pi counters): the query 35 BSP3D nodes, 6.3 leaves, 17 surfaces, 73 edge steps,
~1,000 back-edges; the feature test 4.6 spheres, 4.7 capsules, 7.8 prisms, 8.4 polygon edges, 0.73 hits.

## Cost, guest vs native (host-side; not Vita numbers)

**In game** (`XV_NATIVE_4B9D0_TIME=1`, us per call, call-weighted over the windows after the first 20; mode 0 times
the game's fused code - the fused query call, the fused solver's inline 864C0 between the call and its continuation -
mode 2 the native including its hook; the two runs of a pair side by side):

| where | pair | part | calls | guest (fused) | native | ratio |
|---|---|---|---|---|---|---|
| x86 host (-O1 harness) | `XV_LOCKSTEP=2` (virtual clock: the same ticks and calls in both), CPUs 0-3 / 4-7 (one CCD, no SMT sibling busy) | query | 403,570 | 9.888 | 6.779 | **1.46x** |
| | same runs | feature test | 624,697 | 2.218 | 1.143 | **1.94x** |
| x86 host | the same, query-only build (cfae659), CPUs 12-17 / 18-23 | query | 411,250 | 10.333 | 6.825 | 1.51x |
| Pi 4 (-O1 harness, ARM) | `XV_LOCKSTEP=2`, mode 0 on core 2, mode 2 on core 3 (each run on one core with all its threads) | query | 596,675 / 987,336 | 79.99 | 51.17 | **1.56x** |
| | same runs | feature test | 939,190 / 1,607,339 | 24.45 | 7.56 | **3.23x** |

Calls per frame (x86 pair, all callers): 16.5 queries and 25.6 feature tests; the probe puts 9.2 and 14.5 of them
under 4B9D0 (the others are collision moves of other objects through the same 172BF0, which the hooks serve too).
Per call: the query 30 BSP3D nodes, 5.2 leaves, 16.7 surfaces, 69 edge steps, ~1,040 back-edges; the feature test
5.3 spheres, 6.6 capsules, 9.8 prisms (9.1 polygon edges), 0.74 hits, 34 back-edges. The Pi pair is not reproducible
call for call (streaming moves the level start; each run shares its single core with its own helper threads, which
inflates both columns alike). In this pair the two runs diverged early (the native run made 1.65x the calls per
frame: 29.8 queries against 18.0), so the Pi columns average different call mixes - its query ratio agrees with the
Pi replay (1.56x / 1.57x), its feature-test ratio is less certain (the native run averaged 3.1 spheres, 3.2 capsules,
6.5 prisms per call, the x86 runs 5.3 / 6.6 / 9.8). The x86 lockstep pairs are exact.

**Replay** (captured a10 calls, each from its own entry state, guest and native passes alternating; perf-counter user
instructions and cycles per call; the Pi build is `-O2 -mthumb -march=armv7-a` like the Vita's):

| where | part, guest | guest ns/call | native ns/call | ratio | instructions guest / native | cycles guest / native |
|---|---|---|---|---|---|---|
| x86 | query, the fused query (partial natives on) | 8,094 | 3,865 | **2.09x** | 175,114 / 76,345 (2.29x) | 43,454 / 20,931 (2.08x) |
| Pi 4 (A72) | query, the fused query | 64,738 | 41,314 | **1.57x** | 161,129 / 89,246 (1.81x) | 116,270 / 74,351 (1.56x) |
| x86 | feature test, the translated `f_000864C0` | 1,331 | 408 | **3.26x** | 27,984 / 9,370 (2.99x) | 7,477 / 2,537 (2.95x) |
| Pi 4 (A72) | feature test, the translated `f_000864C0` | 10,816 | 3,480 | **3.11x** | 30,314 / 10,856 (2.79x) | 19,909 / 6,773 (2.94x) |

Why the game's ratios are lower than the replay's: in the replay the 2,000 calls run back to back, so the BSP pages
and the code stay in cache; in the game the tick between calls evicts them, and the native saves instructions, not
memory accesses (every guest access stays one for one, the exactness bar). The Pi's A72 already shows it on the
query (cycles 1.56x against instructions 1.81x). For the feature test the game's reference is the fused solver copy
(registers in locals), faster than the plain translation the replay compares against.

**Expected on the Vita** (estimate, to be measured): 4B9D0 is 9.1-9.8 ms per rendered frame in
perf187 (9.07 ms in the windows quoted for this task). With the Pi probe's shares (the query 41 %, the feature test
16.6 %; the x86 probe's are higher, 56 % and 17.7 %) and the Pi 4's ratios (the query 1.56x in game and 1.57x in the
replay; the feature test 1.94x at the conservative end - the x86 lockstep pair against the fused copy - to 3.2x), the
saving inside 4B9D0 is 9.07 x (0.41 x (1 - 1/1.56) + 0.166 x (1 - 1/1.94 ... 1/3.2)) = 1.33 + 0.73 ... 1.04, about
**2.1-2.4 ms per frame**. The hooks also serve the collision moves of other objects through the same 172BF0 (per tick
on the host 16.5 queries and 25.6 feature tests, 9.2 and 14.5 of them inside 4B9D0): if those cost on the Vita what
4B9D0's calls do, another ~1.6-1.9 ms per frame. The A9 is narrower than the A72 with smaller caches and slower
memory; the query saves instructions (1.81x on ARM) while its memory accesses stay one for one, so its Vita ratio
should be near the Pi's cycle ratio (1.56x), not the instruction ratio.

## Vita integration (overlap-candidate stage)

Files (branch `work/native-4b9d0-20260924`, on top of 561185a):

1. `recomp/kernel/xk_native_4b9d0.c` (new; Vita object 76.1 KB text - the query ~39 KB, the feature test ~31.5 KB - and 188 B
   BSS; the verify journal and buffers are allocated on first use).
2. `Makefile`: the `XV_NATIVE_4B9D0` block (flag, `_DEFAULT`, the `-ffp-contract=off` rule).
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_4b9d0.c` under
   `ifeq ($(XV_NATIVE_4B9D0),1)`.
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_4b9d0_report(60)` next to `xv_native_92330_report`.
5. Shards: `recomp/kernel/xk_query_reuse.c` and `recomp/solver_fusion.c` hooks.

Steps for `overlap-candidate/build-x87` (for `overlap-candidate/build` the same; that stage has no 92330 integration and
the installer places its edits after the `XV_NATIVE_VISIBILITY` ones instead):

1. `python3 tools/install_native_4b9d0.py overlap-candidate/build-x87` from this checkout (copies the unit; adds the
   Makefile block after the stage's `xk_native_92330.o` rule, the runtime.mk line after the 92330 line, the xd3d.c
   call after the 92330 report call; prints `hook installed (5 call sites)` and `hooks installed (the 170CD1 call of
   f_000864C0, its continuation)`; idempotent). Tried here on copies of both stages' five files.
2. Add `XV_NATIVE_4B9D0=1` to `overlap-candidate/make-vars.txt` (and to the make variables in `build-command.json`
   if the build driver takes them from there). `xk_query_reuse.c`, `solver_fusion.c`, `xd3d.c` and the new unit
   rebuild by mtime (`solver_fusion.c` is compiled with `RECOMP_CFLAGS`, which carry `-DXV_NATIVE_4B9D0=1`).
3. Check: `arm-vita-eabi-nm build/recomp/solver_fusion.o | grep 4b9d0` (U `xv_native_4b9d0_features`, `_on`, `_t0`,
   `_t1`), `arm-vita-eabi-nm build/recomp/kernel/xk_query_reuse.o | grep 4b9d0` (U `xv_native_4b9d0_query`), the
   ELF has `T xv_native_4b9d0_query` and `T xv_native_4b9d0_features`; `arm-vita-eabi-objdump -d
   build/recomp/kernel/xk_native_4b9d0.o | grep -c vfma` is 0.
4. Runtime: first `XV_NATIVE_4B9D0=1 XV_NATIVE_4B9D0_TIME=1` (expect `mismatched 0` on both lines; the frame pays guest
   + native + the journal), then `XV_NATIVE_4B9D0=2` against `0` with `XV_NATIVE_4B9D0_TIME=1` (the us/call pair per
   part) or with `XV_SCENE_PHASES=1` (4B9D0 should drop by about the estimate above). `XV_NATIVE_4B9D0_PARTS=1` or `2` isolates
   one part. `XV_NATIVE_4B9D0_DEFAULT=2` in the make vars makes it the default once hardware agrees.

Checked here: the unit, the hooked `solver_fusion.c`, `xk_query_reuse.c` and `xd3d.c` compile with the Vita command
lines from `make -n` of the x87 stage (arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon, `-ffp-contract=off`
for the unit), the unit also against the memory-lowering stage's headers; `solver_fusion.o` references the four
feature-test entry points and `f_000864C0`, `xk_query_reuse.o` references `xv_native_4b9d0_query`, `xd3d.o` has the
weak `xv_native_4b9d0_report`; no `vfma` in the unit's object.

## Findings along the way

- A plain transliteration (the first commit) was no faster than the fused guest on the Pi (30 % slower) and 1.07x
  on x86: the fused query's locals and partial natives already remove most of the obvious overhead. The gain came
  from frame slots served from locals (with the `dirty` fallback), one translation per record and frame, list scans
  over one host page at a time with the exact exit state, and the frame's host pointer.
- The query is memory bound on the Pi 4 (a SIGPROF profile of the replay: 32-bit loads 17.6 %, float loads 9 %,
  record translations 11 %, 86F50 24 %): the A72's cycles ratio (1.56x) trails the instruction ratio (1.81x).
- The feature test is float arithmetic on a handful of records: the translation's x87 stack and flag traffic
  dominate, hence the larger ratio.
- The feature test's frame slots double as scratch for the callees' locals, and the solver's feature block sits right
  above 864C0's arguments; a synthetic layout with the feature counts inside the frames made the loop count depend on
  NaN payload bits - the only such dependence found, now a decline.
- Timing pairs on the x86 host need matched physical cores: CPU n and n+16 are SMT siblings and 0-7 / 8-15 sit on
  different CCDs (L3). A first pair with mode 2 on CPUs whose siblings ran the differential tests showed the query
  native slower than the guest (11.1 against 8.4 us/call); on CPUs 0-3 against 4-7 it is 1.46x faster.

## Not done / next

- `f_000868F0` (BSP features from the query's lists, 9.5 % of 4B9D0 on the Pi) and `172040` (object walk, 6.7 %)
  are the next candidates inside 171F10; 49600/172BF0 bodies are small.
- A compile-time copy of the natives without the verify journal checks would shave the per-store test.
- A targeted test scene for the query's `dirty` fallback (a stray list store onto a frame word read back later,
  e.g. a saved register of a live 87EA0 frame) would let its two mutants be caught; the generated wild scenes do not
  produce one.
