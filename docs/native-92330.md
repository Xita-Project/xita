# Native light cluster query under f_00092330 (`XV_NATIVE_92330`)

Sept 24 2026. Branch `work/native-92330-20260924`. Status: implemented and verified: 6.0 M in-game calls compared
on the x86 host and the Pi 4 (0 mismatches), 180,000 differential-test cases against the lifted bodies of both stage
flavours (0 mismatches), 16 of 16 deliberately broken variants caught. About 2.1-2.2x faster than the guest per
query in the game (x86 lockstep pair with identical calls: 2.14x; Pi 4: 2.16x). Default off. The units compile for
the Vita (the memory-lowered stage and the `--x87-regs` stage with its `XV_QSERIAL` hooks); not linked into a VPK or
measured on hardware.

## What was replaced, and why this boundary

The target was `f_00092330` (light update, called per attached light from `f_0008D760` in the per-object update
`8FB70`): 8.88 ms/frame inclusive, "6.96 ms self" in the Vita tick timers (perf177t, a10 cinematic, 87 calls/frame).
Its own body is about 100 x86 instructions with ~20 x87 operations. The "self" time is a callee the timers do not
wrap: `f_00056670`, the cluster query at `925AB` (every connected light, every tick). Measured with extra timers
around `56670`, `52240`, `51E90`, `B77C0`, `A9330` (probe stage, a10 checkpoint save with the movement pad):

| ms/frame (calls/frame) | x86 host, 179 windows | Pi 4, 560 windows |
|---|---|---|
| `8D760` inclusive | 0.131 (33.5) | 2.106 (48.2) |
| `92330` inclusive | 0.114 (39.8) | 1.834 (65.6) |
| **`56670` inclusive** | **0.084 (39.8) = 74 %** | **1.315 (65.6) = 72 %** |
| `52240` (flood) | 0.049 | 0.832 |
| `51E90` (portal test) | 0.040 (196) | 0.674 (330) |
| `B77C0` (circle vs polygon) | 0.015 (45) | 0.231 (101) |
| `A9330` (datum_new) | 0.025 (182) | 0.314 (335) |
| `8D650` (92330's other big child) | < 0.015 (not in the top 30) | ~0.2 (61) |

So the cost of "92330 self" is `f_00056670` and its leaf subtree; 92330's body and its other children
(`8D650` 1.55, `8B0F0`/`8B910`/`8B220` 0.37 ms/frame on the Vita) are separately timed and small. On the Vita the
query is therefore ~6.5 of the 6.96 ms (by the host/Pi shares). The smallest subtree that holds it:

| function | role | disposition |
|---|---|---|
| `f_00092330` | light update: position/direction from the object marker, radius from the light tag, then the query | guest |
| **`f_00056670`** | **cluster query: flood from the light's cluster within its radius (epoch `2D2FAC`, stamps `2D2FB0[cluster]`, in-use byte `2D2FA9`), then two reference datums per found cluster (at most 64) linked into the light's and the cluster's lists** | **native (after its preamble)** |
| **`f_00052240`** | **recursive flood over the cluster's portals, records up to `count` clusters** | **native** |
| **`f_00051E90`** | **portal test: sphere vs portal plane, vs the portal's bounding sphere, projection onto the plane's dominant axes, 2D circle vs polygon** | **native** |
| **`f_00011840`** | **dominant axis of a normal** | **native** |
| **`f_000B77C0`** | **2D circle vs convex polygon (edge half-planes)** | **native** |
| **`f_000A9330`** | **datum_new: first free slot from the array's hint, zero it (`rep stos`), salt, counters** | **native** |

No other calls, no HLE. The hook sits inside `f_00056670` after its preamble (phase scope, object-math guard, light
census token, typed worker query), so it serves every caller (`92330` and `8E970`) and keeps those mechanisms.
Porting 92330's own body as well was not done: it would save part of its ~0.5 ms/frame share (estimate: 8.88
inclusive minus the query and the timed children) at the price of guest
calls to `8B220`/`8D650`/`8B910`/`8B0F0`/`17A8B0`/`B5EA0`... from native code; the query is where the time is.

## Semantics reproduced (exact)

`recomp/kernel/xk_native_92330.c` is a transliteration of the generated code, not a re-derivation:

- every guest read and write happens at the same program point, in the same order, through the same translation as
  the shards (the thread's page table; image globals through it under `XV_RENDER_VIEW`); integer accesses are single
  translations like `X_M32`/`X_W32` (a 4-byte access at a page end continues in the same host page), float loads and
  stores page-split like `x87_load_f32`/`x87_store_f32`; translations of a record (portal, plane, position, cluster,
  BSP header), of a stack frame and of a salt page are reused only for addresses inside that page (a page-table
  lookup cached, never a value); `datum_new`'s `rep stos` is `x_str_stos` itself, or one `memset` for the same bytes
  when the element lies in one page, `df = 0` and no `XV_WATCH_ADDR` is set;
- the dead guest stack is written through (pushes, return addresses, locals, projected polygon), so every byte below
  esp is the guest's; stack slots are served from locals only while nothing can have hit them: a visited-stamp write
  into the live stack window, or a portal with more than 128 vertices (the projected polygon runs over the frame into
  the callers' frames), sets `dirty` and every later stack read comes from guest memory (both paths are in the tests);
- floating point: doubles with the guest's operand order and rounding points (float loads widened, float stores
  rounded, `-ffp-contract=off`, no fused multiply-add in the Vita object); the final value of every x87 scratch slot
  `st[(fsp0-k)&7]` the subtree writes (k = 1..6), and the status word exactly as `x87_compare` leaves it: condition
  codes of the last compare and the TOP field of every compare OR-ed in (`x87_compare` never clears it);
- registers on exit: eax/ecx/edx as the guest computes them, ebx/ebp/esi from the guest's pops (read from the frame),
  esp + 0x14 (`ret 10h`); the lazy-flag record of the last flag-writing instruction including `f_cf` when its
  override is set; internal register liveness is used only where the guest provably overwrites a register before
  reading it (documented at `n9_circle`);
- the back-edge budget: `c->preempt` drops by exactly the guest's back-edge count (the flood loop, the polygon copy,
  the edge loop, the datum scans, the link loop) and `xv_preempt()` is called the same number of times, at the end of
  the query instead of mid-loop (a scheduling point only, as in `xk_native_visibility.c`);
- each `datum_new` takes and releases the object-math guard like the guest's (recursive on a worker lane, parks
  there like the guest).

Not reproduced (unobservable): the stale `f_cf`/`f_of` cells while their overrides are 0 (no flag read consults them
for the SUB/LOGIC records left here), and NaN payload bits (which NaN an operation propagates is the host compiler's
register choice; such floats only reach dead stack words and scratch slots; the test accepts a NaN word against a
NaN word and counts them: 9 in 20,000 cases, 0 in-game). The native declines (the guest runs) while the light census
has a token open or is on, and on the scene helper (`XV_QSERIAL` gives the helper private serials the native does not
model; the query is tick work anyway).

## Knobs and counters

- Build: `XV_NATIVE_92330=1` (Makefile block after `XV_NATIVE_VISIBILITY`: `-DXV_NATIVE_92330=1`,
  `XV_NATIVE_92330_DEFAULT` (0), `-ffp-contract=off` for the unit; `games/halo_ce_3925/runtime.mk` adds the source).
- Hooks: `python3 tools/patch_native_92330_hooks.py overlap-candidate/build/recomp` (idempotent). In `f_00056670`: a token
  `void *xn92_` after the local-cache preamble, the call `if (xv_native_92330(c, &xn92_)) return;` just before
  `L_00056670` (skipped while `xv_query_work_.lane` holds a census token), and a post hook at both `ret 10h` sites.
- Env `XV_NATIVE_92330`: 0 off (default), 1 verify, 2 native.
  Verify: run the native with a write journal, record its result (registers, flags, x87, back-edges, every byte it
  wrote, the regions the guest may write), undo it from the journal, let the guest body run on the same state with an
  unbounded budget, compare at its return (NaN words equal), keep the guest's result and apply its budget.
- Env `XV_NATIVE_92330_TIME=1`: ns clock per call (Vita: us clock), us/call of the guest body (mode 0), the native
  (mode 2), both (1); also turns on the per-query detail counters (each is an atomic).
- Phase timing (`XV_PHASE_TIMING`): the `f_00056670` scope still covers the query; the scopes of 52240/51E90/A9330
  are not recorded while the native runs.
- 60-frame line from xd3d.c's report block: `[native-92330] 60 frames: calls N verified N mismatched N (total
  mismatches N) declined N journal-fail N overlap-skipped N; floods N clusters N portals N full N datums N back-edges
  N dirty N nan-words N; us/call native X guest Y`. Mismatches print up to 12 `MISMATCH <what> native .. guest ..`.

## Verification

**Differential test** (`tools/test_native_92330.py <stage>/recomp [cases] [--seed N] [--verify] [--mutants N]`):
the stage's lifted bodies of the six functions (extracted; the hook applied to the copy when the stage has none) and
the native built three ways (plain page table -O2, per-thread table + render view -O2, -O0), randomized queries in a
24 MiB synthetic arena with shuffled pages: random cluster/portal graphs with convex polygons in random planes,
axis-aligned planes and ties between normal components, degenerate and empty polygons, 129-134-vertex portals
(frame overrun: the `dirty` path), the stack placed inside the stamp range with portals aimed at live frame words,
floods of more than 64 clusters, a stack straddling address 0 (the link pointer carries), NaN/zero/negative radii and
eps, cluster 0xFFFF, full/fragmented/negative-hint datum arrays, salt wrap, element sizes that are not multiples of 4,
both direction flags, a small back-edge slice. Per case: guest vs native from identical state, the whole arena, every
xctx field, the xv_preempt call count; `--verify` also runs mode 1 and requires the guest result and no MISMATCH.

| stage (lifted bodies) | seed | variant | cases | mismatches | verify mode (mode 1) | NaN-payload words |
|---|---|---|---|---|---|---|
| `overlap-candidate/build` (memory lowering) | 1 | plain page table -O2 | 20,000 | 0 | 0 failures | 9 |
| same | 1 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 9 |
| same | 1 | plain -O0 | 20,000 | 0 | 0 failures | 12 |
| `overlap-candidate/build-x87` (`--x87-regs`, `XV_QSERIAL` hooks) | 2 | plain page table -O2 | 20,000 | 0 | 0 failures | 9 |
| same | 2 | per-thread table + render view -O2 | 20,000 | 0 | 0 failures | 9 |
| same | 2 | plain -O0 | 20,000 | 0 | 0 failures | 0 |
| `overlap-candidate/build` | 7 | plain page table -O2 (no verify) | 20,000 | 0 | | 11 |
| same | 7 | per-thread table + render view -O2 | 20,000 | 0 | | 11 |
| same | 7 | plain -O0 | 20,000 | 0 | | 14 |

Each seed's 20,000 cases include ~14,900 floods, ~840 scenes with the stack inside the stamp range, ~2,300 queries
that take the `dirty` path, 30-40 cases whose guest never terminates (a frame overrun that leaves garbage loop
counts; skipped when the guest passes the 4 s alarm; with the x87 stage one verify run per variant also passed it
and is skipped) and ~315 whose verify run
cannot journal the element clear (direction flag set: the native result is kept, counted `journal-fail`).
Mutants (`--mutants 3000`, plain -O2, seed 5), all caught: d2 summation association (1/3000 cases differ), circle
test `<` vs `<=` (89), edge back-edge not counted (934), dead projected z not stored (1039), datum hint off by one
(2660), fsw TOP replaced instead of OR-ed (2143), list write for count >= 0 (236), dirty reload skipped (48),
slot 6 value (690), plane distance stored after fabs (1303), salt wrap to 0x8000 missing (413), exit carry flag
(3: the stack-straddling-0 scenes), 11840 tie to y (46), cluster record not re-translated after a child (102),
scan back-edges off by one when full (485), stamp kept when it holds the previous epoch (the mutant never
terminates its recursion: crash).

**In-game verify** (`XV_NATIVE_92330=1`, every call compared; a copy of the Vita checkpoint save, Continue at the a10
checkpoint, the look/move/fire pad script, scene on the helper with overlap mode 2 unless noted):

| run | where | frames | calls compared | mismatches | overlap-skipped |
|---|---|---|---|---|---|
| fver | x86 host, overlap mode 2 | 35,640 | 1,754,019 | 0 | 0 |
| fown | x86 host, scene on the owner (`XV_SCENE_THREAD=0`) | 26,640 | 2,050,029 | 0 | 0 |
| Pi fver | Pi 4, cores 0-1, overlap mode 2 | 44,340 | 2,196,232 | 0 | 6,509 |

Total with the final code (17d422b): 6,000,280 calls (3,804,048 x86, 2,196,232 ARM), 0 mismatches, 0 NaN words,
0 `dirty` queries (the frame overrun and stamp aliasing never occur in the game). Before the last speed changes the
same exactness logic had compared another 8.95 M x86 calls and 0.75 M ARM calls, all 0 mismatches. Per a10 query:
2.1 flood calls, 3.9 portal tests, 1.1 full tests, 4.2 `datum_new`, ~106 back-edges (x86 counters).

A first Pi verify run reported mismatches on the epoch `2D2FAC`, the in-use byte and a few stamps only (12 lines in
296 windows, 0 on x86): the overlapped scene's render-view merge copies every word the scene changed into live memory
where live still equals its pristine copy. Between the native's undo and the guest run the tick's epoch looks
untouched, and the scene's own cluster traversals (51A47, 51D20, ...) change the same words, so the merge overwrote
them under the check. Verify mode now runs the guest alone (counted `overlap-skipped`) while an overlapped scene has
not signalled done; after that no merge can happen until the owner dispatches the next scene. (The same shared words
are what `XV_QSERIAL`, being added to the x87 stage, gives the helper private copies of.)

## Cost, guest vs native (host-side; not Vita numbers)

**In game** (`XV_NATIVE_92330_TIME=1`, us per `f_00056670` call, call-weighted over the windows after the load;
mode 0 times the guest body, mode 2 the native; the two runs of a pair side by side on separate cores):

| where | pair | guest (mode 0) | native (mode 2) | ratio |
|---|---|---|---|---|
| x86 host (-O1 harness) | `XV_LOCKSTEP=2` (virtual clock: the same ticks, the same 1,500,870 calls in both, window by window), cores 8-13 / 14-19 | 0.906 | 0.423 | **2.14x** |
| x86 host | real time, 793 k calls each, cores 20-25 / 26-31 | 1.687 | 0.783 | 2.15x |
| Pi 4 (-O1 harness, ARM) | `XV_LOCKSTEP=2`, mode 0 on core 0, mode 2 on core 1, 406/405 windows (medians 12.43 / 5.77) | 12.48 | 5.77 | **2.16x** |
| Pi 4 | real time, core 0 / core 1, 319 windows each (medians 12.40 / 5.53) | 12.77 | 5.70 | 2.24x |
| Pi 4 | sequential on cores 0-1, pairs "tf" (final) and "tc" (same native object) | 12.65 / 13.09 | 4.60 / 6.23 | 2.75x / 2.10x |

The Pi harness is not reproducible run to run even with the virtual clock (streaming during the load moves the
level start, handoff of the x87-regs work), so its pairs do not see the same call mix (the Pi lockstep pair differs in
407 of 426 windows; 2.5 clusters, 4.2 portal tests, 1.5 full tests, 5.0 `datum_new` per call in one Pi run against
2.1 / 3.9 / 1.1 / 4.2 on x86), and it was shared with another agent's harness runs; the guest cost per call is stable
there (12.4-13.1 us), the native's moves with the mix (4.4-6.2 us medians). On x86 the lockstep pair is exact: same
calls, 2.14x.

**Microbenchmark** (`tools/tests/native_92330.c --bench-game`, user-space instructions and cycles per call from the
perf counters, -O2 -mthumb like the Vita build, on the Pi's A72; guest and native alternate on the same query, the
datum arrays and list heads restored between calls outside the timed region):

| mix | guest instructions | native instructions | ratio | cycles ratio |
|---|---|---|---|---|
| a10-like: 1 flood, 3.4 portal tests, 0.2 full, 2.1 `datum_new`, 55 scan steps | 8,807 | 4,529 | 1.94x | 1.63x |
| full-test heavy: 3.4 floods, 4.5 portal tests, 2.8 full, 6.6 `datum_new`, 188 scan steps | 30,735 | 14,211 | 2.16x | 2.07x |
| random scenes (`--bench`, large floods) | | | | 2.27x (time) |

Knock-outs on the a10-like mix: the datum scans are 4,258 guest vs 1,131 native instructions (3.8x), the flood with
its portal tests 4,065 vs 2,336 (1.74x), the fixed part of a call 368 vs ~576 (the hook, the state set-up, the x87
slot and flag write-back). On x86 the a10-like mix is 8,914 vs 4,291 instructions (2.08x, 1.57x cycles).

Why not more: the recompiled integer code (flood loop, list linking) is already close to hand-written C (registers
live in `c` under `restrict`, dead flag stores are eliminated), and every guest memory access stays one for one with
its translation. The native removes the x87 stack traffic, the lazy flags, the `rep stos` call and per-access
translations inside one page. The next factor would need either a different exactness contract (caching tag data
across queries) or fewer accesses (the scans).

**Expected on the Vita** (estimate, to be measured): `92330` is 8.88 ms/frame inclusive in perf177t and the query
72 % of it on host and Pi, ~6.4 ms; at the measured 2.1-2.2x it would cost ~3.0 ms, i.e. about -3.3 ms of owner tick
time per frame. The A9 is closer to an in-order core than the A72, so the instruction ratio (1.9-2.2x) is the better
guide than the A72's cycles.

## Vita integration (overlap-candidate stage)

Files (branch `work/native-92330-20260924`, on top of 9bc0be7):

1. `recomp/kernel/xk_native_92330.c` (new; Vita object 22.0 KB text, 92 B BSS; the verify journal/buffers are
   allocated on first use).
2. `Makefile`: the `XV_NATIVE_92330` block after the `XV_NATIVE_VISIBILITY` block (flag + `_DEFAULT` + the
   `-ffp-contract=off` rule for `$(RECOMP_BUILD)/kernel/xk_native_92330.o`).
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_92330.c` under
   `ifeq ($(XV_NATIVE_92330),1)` (next to the visibility line).
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_92330_report(60)` next to `xv_native_visibility_report`.
5. Shards: `python3 tools/patch_native_92330_hooks.py overlap-candidate/build/recomp` (hooks `f_00056670` in
   `code_010.c`; prints `hooks installed in f_00056670 (2 return sites)`).
6. Build with `XV_NATIVE_92330=1` added to `make-vars.txt`. `code_010.c`, `xd3d.c` and the new unit rebuild by mtime.
   Check: `arm-vita-eabi-nm build/recomp/code_010.o | grep xv_native_92330` (U, U) and the ELF has
   `T xv_native_92330`, `T xv_native_92330_post`.
7. Runtime: `XV_NATIVE_92330=1 XV_NATIVE_92330_TIME=1` once (expect `mismatched 0`; the frame pays guest + native
   + the journal), then `XV_NATIVE_92330=2` against `0` with `XV_NATIVE_92330_TIME=1` (us/call pair) or with
   `XV_SCENE_PHASES=1` (`92330` self should drop from ~7 ms). `XV_NATIVE_92330_DEFAULT=2` in the make vars makes
   native the default once hardware agrees.

The same steps apply to `build-x87` (the `--x87-regs` splice does not convert `f_00056670`; its shards as of Sept 24
01:00 carry the `XV_QSERIAL` hooks, whose owner-thread `X_QS32` is `X_G` like the render-view `X_IMG32` the native
uses). Checked here: the new unit, `xd3d.c` and the hooked `code_010.c` of both stages compile with the Vita command
lines from `make -n` (arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9); `code_010.o` references `xv_native_92330` and
`xv_native_92330_post`; no fused multiply-add in the native object.

## Findings along the way

- The datum scans are long: ~25 used slots stepped over per `datum_new` in the a10 checkpoint (106 back-edges and
  4.2 `datum_new` per query), where the native is ~3.8x fewer instructions than the guest. Every tick each light's
  references are removed (`565E0`) and re-added here, so the free slots the hint finds are scattered. A free list or
  a lower hint would shorten the scans but changes guest-visible state (datum indices): not an exact port.
- `f_0008D760` self (1.71 ms/frame on the Vita) includes `f_000565E0`, the removal of every connected light's
  cluster references (linked-list searches) before 92330 re-adds them; a native of it is the next candidate here.
- The recompiled integer code (flood loop, list linking) is already close to hand-written C once registers live in
  `c` under `restrict`; the gain comes from the x87 work (portal and circle tests), the lazy flags and the translated
  memory accesses, which the exactness bar keeps one for one.

## Not done / next

- 92330's own body (see above), `565E0` (removal), `8D650`.
- Compile-time removal of the verify journal checks from the native path (one per guest store, ~5 % of its
  instructions on ARM) would need the core compiled twice.
