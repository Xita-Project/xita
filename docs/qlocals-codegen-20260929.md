# Guest registers and flags in C locals (qlocals), 2026-09-29

Status: v2 is qualified on the x86 host harness (b30, object workers on and off) and was measured on the
Pi. perf323 (perf322 + this pass only) is built privately in `../qlocals-323/`. Hardware results are
recorded below as they arrive. No 20 FPS claim.

## Why a program-wide code-generation change

Twenty-plus hardware builds (perf300-322) targeted individual engine functions while b30 stayed at
123-128 ms/frame. The Pi flat profile is flat (largest self share ~8.6% "guest reads", the rest 1-4%),
so replacing single functions cannot close a 2.5x gap. The Vita disassembly of any generated function
(for example `f_0004C980` in perf322 `code_008.o`) shows the common cost instead:

* every guest register lives in `c->r[]` in memory: GCC stores each result and reloads operands after
  every guest memory access, because guest accesses use `may_alias` types and `c` escapes into callees,
  so `restrict` on `c` does not give GCC the disambiguation the recompiler comment expects;
* every live flag write stores eight or nine `c->f_*` fields, and every flag read re-derives the
  condition from memory through the lazy-flag kind switch. The recompiler's dead-flag analysis is
  block-local, so most flag writes survive.

An earlier hand-made "registers in locals" solver copy was already ~1.65x faster than the plain
translation of the same function (docs/native-4b9d0.md).

## The transform (`tools/qlocals.py`, `tools/apply_qlocals.py`)

A text post-pass over the generated shards after all game hooks (so hook text is seen as final C):

* `c->r[N]` becomes a local `qN`; `X_R16/X_R8L/X_R8H`, push/pop and the back-edge budget use them.
* Lazy flags live in a local `xqf qf`; the flag helpers get generated `xq_` variants (mechanically
  derived from xv_x86rt.h/xv_x87reg.h: only `c->f_*` accesses are redirected).
* Barriers are calls that may observe or change guest registers (guest calls, indirect calls, HLE,
  string ops, mul/div helpers that write EAX/EDX, and every unknown hook call). Before a barrier only
  registers that may differ from `c->r` are stored (forward dataflow over the function's labels and
  gotos, conservative across `#if` branches and conditional blocks); after it all eight are reloaded
  (GCC removes dead reloads).
* Flags: published at every return and reloaded after guest calls, because the base code can leave a
  callee's flags in `c->f_*` for its caller (the dead-flag pass only strips writes in the block that
  ends in `ret`). Other barriers store and reload all flags.
* Locals are declared and loaded at the very top of the function; every entry from the untransformed
  prologue into the body (fallthrough and prologue `goto`s) reloads them.
* Anything unrecognised excludes the whole function (6 of 8,020 excluded in perf322).

## Bugs found during qualification (kept as regression knowledge)

1. v1 declared the locals at the first label. `f_00056670`'s prologue worker-query hook does
   `goto L_000566DE`, skipping the initialisation: object workers ran with garbage ESP
   (`STOP guest stack allocation exceeds worker capacity`, esp 0x515E). Found by an automated
   18-step function bisection (`../qlocals-20260929/qbisect.py`).
2. v1 treated `x_imul1_8/16` and `x_mul_8/16` as register-neutral; they write EAX/EDX through
   `X_R16/X_R8L`. Now barriers.
3. v2 first skipped flag synchronisation at returns. The b30 menu never started the level (ticks
   cycled in the attract loop): callee flags did reach callers in the base code.

## Qualification evidence

* x86 host harness, b30, `XV_LOCKSTEP=2`, scripted menu input, 300 s, object workers on and off:
  no faults, normal tick progression.
* Page-state oracle (`XV_STATE_PAGES`, host-only knob in the private stages): 64-bit hashes of every
  4 KB guest page at equal game ticks. Identical base runs differ on ~1,250-1,420 of 16,384 pages
  (render/audio/timing buffers); v2 differs from base by the same amount and on no page set that is
  stable across base runs. Simulation divergence would spread across the game state within a few
  hundred ticks; none was seen.

## Measurements

Pi (Cortex-A72, ARM mode, GCC 13.3 `-O2`, static; b30 lockstep, workers off, cores 0-1), per-60-frame
`[owner-phase]` elapsed medians over matching frame windows:

| build | FA920 (tick) | BCB30 (scene) |
|---|---|---|
| v1 (flush all 8 at every barrier, flags in memory) | +9.7% vs base | +4.4% vs base |
| v2 | **-10.1%** (18.33 -> 16.48 ms, 58 windows, frames 3000-6420) | **-8.2%** (25.51 -> 23.41 ms) |
| v2, loading/cinematic windows 600-3000 | -8.8% | -8.7% |

Vita perf323 (perf322 + this pass only; runtime SHA c5a9bb0d...; installed slot 1, boot confirmed), same
isolated b30 save, normal environment (vertex experiment off), same launch/fresh-start/600 s capture scripts:

| | perf322 | perf323 |
|---|---|---|
| aligned in-level windows 0-20 / 20-40 after level start | 132.56 / 124.56 ms | 126.93 / 118.22 ms (-4.3% / -5.1%) |
| final 10 windows (600 intervals) mean | 123.43 ms (8.10 FPS) | **117.57 ms (8.51 FPS)** |
| p50 / p95 / p99 / max | 121.5 / 136.7 / 172.2 / 227.1 | 115.7 / 131.3 / 169.2 / 199.6 |
| intervals > 200 ms | 1 | 0 |
| owner tick FA920 (tick-phases) | 115.65 ms/frame | 109.21 (-5.6%) |
| object pass 900E0 | 96.07 | 91.66 (-4.6%) |
| AI 14A162 | 16.59 | 14.70 (-11.4%) |

Stationary beach capture; not a combat, checkpoint or 15-minute qualification. perf323 then kept running
b30 for ~27,600 more frames (tens of minutes, stationary) without a crash before the next update.

The Vita gains about half the Pi's tick gain. The object pass is less guest-instruction-bound on the A9
(natives, locks, cache misses). A Pi -g profile of v2 (owner thread, frames >= 1200) shows guest float
loads through `x_guest_read` at ~16% self and 90% of generated-code time in 296 of ~8,000 functions,
which motivated perf324 (page-check branch hints + hot-function layout, tools/hot_layout.py) and
perf325 (page-table host-bias array, tools/patch_xpt_bias.py).

## perf324 and perf325 (same b30 procedure, each compared with its predecessor by aligned windows)

| build | change | final 10 windows mean | p95 / p99 / max | owner FA920 | objects 900E0 | AI |
|---|---|---|---|---|---|---|
| perf322 | baseline | 123.43 ms (8.10 FPS) | 136.7 / 172.2 / 227.1 | 115.65 | 96.07 | 16.59 |
| perf323 | qlocals v2 | 117.57 (8.51) | 131.3 / 169.2 / 199.6 | 109.21 | 91.66 | 14.70 |
| perf324 | + `__builtin_expect` on guest float access page checks, hot layout (693 functions, `.text.sorted`) | 111.47 (8.97) | 125.7 / 174.0 / 191.3 | 104.62 | 87.77 | 14.29 |
| perf325 | + page-table host-bias array (`tools/patch_xpt_bias.py`) | **110.36 (9.06)** | 124.4 / 176.8 / 202.0 | 103.15 | 85.64 | 14.83 |

Aligned-window ratios: perf324/perf323 0.924 / 0.952 / 0.948; perf325/perf324 1.004 / 0.983 / 0.987.
Pi A/B for the bias array alone: tick -2.2%, scene flat. perf325 is installed (slot 1; perf324 in slot 0).
Receipts: `../qlocals-32{3,4,5}/` (package receipts, update results, captures, final-summary.json).
Cumulative: -10.6% frame time on stationary b30. Still far from 50 ms; not a combat/checkpoint/15-minute
qualification.

### perf326: x87 register-stack splice, rejected on hardware

perf325 + 147 hot functions spliced into the x87 register lowering (tools/splice_x87_regs.py from the
Sept 23 regeneration), selected by per-function Pi samples (37 measured winners + 110 unmeasurable).
Pi: tick -6.8% median, scene -1.6%. Vita: aligned windows 1.030 / 1.004 / 1.004, final 10 windows
111.09 ms vs perf325 110.36, owner 104.63 vs 103.15 ms. No gain; rolled back to perf325 (slot 1,
verified). Likely causes: the register lowering carries a memory-lowering copy (larger hot code on an
I-cache-sensitive A9), and the Pi runs used a minimal environment with several Vita natives off, which
over-weights translated bodies that are native on the Vita. Pi A/Bs are directional only.

### perf327: all generated shards at -Os, rejected

Text 25.85 -> 19.16 MB (-26%), but -Os outlines the x87 stack and guest float access helpers (~11K
extra calls in code_008 alone). Vita: aligned windows 1.060 / 1.070 / 1.071, final 10 windows 117.22 ms,
owner 111.05 ms: 7% slower. Call overhead dominates; the A9 is not purely instruction-cache bound.
perf328 (-Os with those helpers forced inline via recomp/xv_os_inline.h, text 24.25 MB) was tested next.

### perf328: -Os with inline helpers, rejected; state at end of session

perf328 (text 24.25 MB): aligned windows 1.053 / 1.039 / 1.030, final 10 windows 113.68 ms, owner
106.95 ms: slower than perf325. -O2 code quality beats the smaller code; the code-size direction is closed.

Device left on perf325 (slot 1, runtime SHA b7be0ef9..., dashboard, keep-awake renewed); the other slot
holds the rejected perf328. Best measured: perf325, 110.36 ms / 9.06 FPS stationary b30.

Next candidates, by measured size:
* Native BSP sphere query (88110 native, `xk_native_4b9d0.c`): perf313 measured 17.5 ms/frame for ~128
  calls (~137 us, ~60K cycles per call for ~110 BSP elements), i.e. memory-latency bound. A compact,
  validated, host-native snapshot of the static collision BSP (or at least a contiguous-region fast path
  instead of per-access page-table translation) is the largest single owner-path target found.
* PSVshell per-game CPU profile at 500 MHz (user decision; Xita's own helper is capped at 444 MHz):
  up to +12.6% clock.
* Pi measurements must use the Vita normal environment's host-applicable settings; with natives off the
  Pi over-weights bodies that are native on the Vita (perf326 lesson).

## BSP sphere query: cold/warm probe on hardware (perf329, diagnostic)

`tools/patch_native_4b9d0_warm_probe.py` adds `XV_NATIVE_4B9D0_WARM=K`: every K-th native query runs once with
the write journal (cold), undoes every journaled write, restores the context and runs again (warm), keeping the
second result (host check: game progresses, page-state oracle within run-to-run variation). perf329 = perf325 +
this probe; run with XV_NATIVE_4B9D0_TIME=1 XV_NATIVE_4B9D0_WARM=8, same b30 procedure. Settled windows:

* ~138 queries/frame at ~115 us/call = **15.7-16.2 ms/frame** (~15% of the owner tick); per call ~21 BSP nodes,
  ~10 surfaces, ~30 edges and vertices: ~600 cycles per element at 444 MHz.
* probe: cold ~245 us, warm ~230 us (journaled both): **warm is ~94% of cold**. Data-cache misses are ~6% of
  the query; it is instruction-bound. The journal alone roughly doubles a call, i.e. the transliteration
  writes a great deal of (mostly dead stack-frame) memory.

Conclusion: a compact BSP copy would not pay. The lever is doing less work per element: a semantic native
with the same traversal order and float arithmetic for the live results (result lists, return registers,
callee-saved registers, stack pointer, back-edge budget), without re-enacting dead stack writes and lazy-flag
records. Verification then compares live outputs against the exact transliteration on captured queries.

## Semantic BSP sphere query: perf330 (installed)

`tools/patch_native_4b9d0_semantic.py` + `tools/n6_query.c.in`: XV_NATIVE_4B9D0=3 computes the query's live results
with the transliteration's traversal order, arithmetic, compare predicates and back-edges, without dead stack/flag
re-enactment; =4 compares it against the exact native (exact result kept). x86 b30 compare: 466,614 queries,
0 mismatches, 0 declines; 3.6 vs 7.9 us/call. Vita perf330 (perf325 + this, env XV_NATIVE_4B9D0=3), same b30
procedure: aligned windows 0.934 / 0.932 / 0.934 vs perf325; final 10 windows **101.94 ms (9.81 FPS)**, p95 115.6,
p99 183.5, max 196.1, 0 > 200 ms; owner 93.15 ms (-10.0), objects 77.59, AI 13.07; 3,480 semantic queries per
60 frames, 0 declined. perf330 slot 0, perf325 slot 1. Cumulative today: 123.43 -> 101.94 ms (-17.4%).

## Decomp port data check (2026-09-29)

Upstream cybersecurity/halo-ce-universal (0ef2ed7, 2026-09-30) built locally (i686, clang 22, a locally built
32-bit SDL3 3.4.16) in ~/github/halo-ce-vita-spike runs the user's NTSC 01.10.12.2276 maps (a reflink copy):
b30 loads and plays for 420 s headless (SDL offscreen + EGL), no assertions, screenshots show HUD, Pelican,
Covenant, 30 FPS capped. Upstream skips the build-string check in native builds and normalises PAL tags to the
NTSC values (port/linux/game/pal_tags.c), so the data blocker of the cancelled native-source-vita-001 is gone.
