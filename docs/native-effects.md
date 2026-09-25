# Native effects and material helpers (`XV_NATIVE_EFFECTS`)

Sept 24 2026. Branch `work/native-effects-20260924` (not pushed). Status: implemented and verified on the x86 host
harness and the Pi 4 harness in a30 (the lifepod cutscene, then the pod) with the Vita play settings. Default off. The
unit, the four hooked shards and `xd3d.c` compile with the Vita command lines of `overlap-candidate/build-x87`, and a
scratch copy of that stage links `build/xita.elf` with the flag on. Nothing has run on hardware.

**Summary.** The effects pass `f_0005E270` is about 6 % of the scene helper's time in a30 (x86 6.3 %, Pi 4 6.1 %
of helper samples). So even removing it completely would not reach 20 fps. Its hot leaves are shared with the model
material path (`A26B0` / `6F730`, outside 5E270). Together they hold about 9.7 % of the Pi helper's self time, in
seven small functions. Those seven are native here, hooked at their entries so both passes use them. Exactness
evidence:

- 74.4 M in-game calls on x86 and 12.2 M on the Pi 4 compared in verify mode, 0 mismatches.
- 60,000 differential-test cases on the final unit, 0 mismatches.
- 19 of 19 mutants caught.
- A multi-thread test and ThreadSanitizer run.

Speed in the game:

- Pi 4: the colour packers run 1.8-2.4x faster, 7E530 1.9x and 56F20 1.15x.
- 7E420 (0.8x) and 80360 (0.97-0.99x) do not pay, so they are hooked and verified but left out of the default set.
- The default set saves **0.75 ms/frame of Pi helper time** over the cutscene and the pod, and 0.97 ms/frame in the
  pod.

Vita estimate (not measured): **about 2-4 ms/frame of scene-helper time**. This alone does not reach 50 ms.

## Where the time goes (measured first)

Host sampler on the scene helper thread, a30 new campaign, Vita play settings. The probe build wraps 5E270's direct
callees and the hooked helpers in phase scopes (`XV_SCENE_PHASES=2`), and every sample carries the innermost timed
callee. Pi 4: 89 windows, 2.46 M helper samples (`pi-runs/pprof1`). x86: 53 windows, 2.77 M samples (`runs/prof4`).

Inside `f_0005E270`, by innermost timed callee (share of 5E270's samples):

| callee | Pi 4 | x86 | what it is |
|---|---|---|---|
| `7A130` (+ `7A3D0`, `7A540`) | 18.6 % (+6.9, +5.4) | 19.5 % (+6.4, +9.0) | draw submission: the runtime's recording (`record_draw_body`, capture, memcpy) |
| `66510` | 18.4 % | 15.8 % | per-effect render: 22 callees, recursive; self time is dispatch between HLE state calls |
| `56F20` | 8.6 % | 4.5 % | texture-coordinate animation |
| `7E530` | 7.8 % | 6.3 % | vertex-shader constant matrices |
| `7E5D0` | 7.2 % | 11.5 % | combiner / render state (HLE-bound: `SetRenderState*`) |
| `191E0` (5DFF0's sort) | 4.5 % | 8.3 % | qsort of the effects and its comparator |
| `7E420` | 3.2 % | 3.7 % | vertex-shader constants for two lights |
| `11C80` | 2.6 % | 2.5 % | render-state wrapper (HLE-bound) |
| `80250` | 2.5 % | 1.4 % | texture bind (325C0 texture cache + `SetTexture`) |

Whole helper, self time of the leaf functions (Pi 4, flat profile; "in 5E270" is the part under 5E270):

| function | Pi 4 helper self | in 5E270 | main other caller |
|---|---|---|---|
| `f_00056F20` | 3.36 % | 9 % | `A26B0` (model material setup) |
| `f_0007E530` | 1.93 % | 19 % | `6F730` |
| `f_00080360` (+ `1290E0`, `80250` 0.33 %) | 1.46 % | 6 % | `A26B0` |
| `f_00011BD0` | 0.92 % | 1 % | `A26B0` |
| `f_00011B60` | 0.82 % | 2 % | `A26B0`, `59550` |
| `f_0007E420` (+ `7DBE0` 0.17 %) | 0.64 % | 16 % | `6F730` |
| `f_00011610` | 0.04 % | | `74D10`, `17FCC0` |
| **sum replaced** | **~9.7 %** | | |
| for comparison: `f_00066510` | 2.18 % | 42 % | |
| `f_0007E5D0` | 0.96 % | 23 % | |
| `f_00070110` (another agent's port) | 11.56 % | 0 % | |

The x86 flat profile's symbolization is unreliable in this build (many samples land on `??` or on unrelated
neighbouring symbols such as `xk_RtlNtStatusToDosError`). The x86 numbers above use the phase tags, which do not
depend on symbols.

Calls per frame in a30 (x86 lockstep, frames after 3000 = the pod): `80360` 1,197, `11BD0` 1,005, `11B60` 516,
`56F20` 372, `7E530` 199, `7E420` 196, `11610` 8.

## What was replaced, and why this boundary

| function | role | disposition |
|---|---|---|
| **`f_0007E530`** | for each of n 52-byte transform records at `[esi]` (scale, 3x3, translation) the scaled rows into `0x278258`, then `SetVertexShaderConstant(-36, 0x278258, 3n)`; the `XV_NATIVE_CONSTANT_PACK` prefix is called where the lifted body calls it | **native** |
| **`f_0007E420`** + `f_0007DBE0` | the two lights of a model: 7DBE0 per light (inline), 11 constant rows on the frame (`rep stos` through the runtime), `SetVertexShaderConstant(-79, rows, 11)` | **native** |
| **`f_00056F20`** | texture-coordinate animation: per axis the period and the owner's scale, three calls of the periodic function `173F20`, the rotation (`fcos`/`fsin`), the two output rows | **native** (`173F20` stays the guest's body) |
| **`f_00080360`** + `f_001290E0` + `f_00080250` | a shader map's bitmap for a stage: the animated sequence (1290E0 inline, frame modulo count), 80250 inline (`325C0` texture cache, `SetTexture`), the width/height words at `0x278A9C` | **native** (`325C0` stays the guest's body) |
| **`f_00011B60`**, **`f_00011610`**, **`f_00011BD0`** | float colour to D3DCOLOR (x 255, `fistp` under the guest's control word, masks and shifts) | **native** |
| `f_0007E5D0` | combiner and render state: mostly HLE `SetRenderState*` calls | guest: a native was written and verified, but its speed was 0.87-1.02x, so it is not hooked |
| `f_00080250` as its own hook | | guest: 0.87-0.94x; its body is inside the 80360 native |
| `f_00066510` | per-effect render, 22 callees | guest: its self time is dispatch around ~20 guest/HLE calls. A native would still call all of them and would save only the lifted glue |
| `5DFF0` / `191E0` / `19170` | the effect sort | guest: 0.25 % of the Pi helper in the pod, 0.8 % in the cutscene |

A first attempt generated the natives from the lifted text, keeping every instruction and moving the context into a
local struct the compiler could keep in registers. It gave ~1.0x: the lifted code already keeps `restrict c` fields in
registers, and the cost is in the per-access translation, the lazy-flag records and the x87 stack traffic. The shipped
natives are hand-written from the lifted bodies. They translate a record that lies in one page once (`hview`), keep
the x87 slots and the flag record in locals, and write back only what the lifted code leaves behind.

## Semantics reproduced (exact)

- **Guest memory**:
  - Every store has the lifted code's address, width and value, in the order its reads can see.
  - Integer accesses are one translation (`X_M32`: an access across a page end touches the next host bytes, as the
    lifted code does). Float accesses are page-split (`x87_load_f32` / `x87_store_f32` / `x87_store_i32`).
  - A record view (`hview`) is taken only when the whole record lies in one page. Then every access through it
    reaches the same host bytes as the lifted access. Views are re-taken after every guest call.
  - `X_IMG*` constants go through the table under `XV_RENDER_VIEW`.
  - The page table is the calling thread's own (`X_PT`, TPIDRURW on the Vita), taken once per call like the shard
    preamble's `xpt_`.
- **Registers**: all eight at return, including dead ones: scratch values, `esp` after `ret N`.
- **Flags**: the lazy-flag record (kind, operands, result, width, CF/OF overrides) exactly as the lifted body's last
  flag-writing instruction leaves it. `inc` keeps the carry. `imul`, `shl` and `sbb` set their CF/OF cells. Where the
  recompiler eliminated dead flag writes, the native eliminates the same ones.
- **x87**:
  - The slots the function pushes keep their final values at `st[(TOP - k) & 7]`.
  - TOP is unchanged at return.
  - `fcomp`/`fnstsw` write the C0/C2/C3 bits and the TOP field into `fsw` as the runtime does.
  - Arithmetic is in doubles with the lifted order of operations (`-ffp-contract=off`, no `vfma`).
  - `fistp` rounds under the context's control word: exact round-to-nearest-even by the 2^52 trick, and
    floor/ceil/trunc for the other modes. `fcos`/`fsin` are the runtime's.
  - 56F20 around `173F20`: the x87-regs protocol. Slots are written back before the call and reloaded after it. On a
    depth miss the native calls `xv_x87reg_miss` and continues with the memory lowering's addressing.
- **Back-edge budget**: `X_PREEMPT` at the same back-edges (7E530's record loop, 7E420's two loops), with the full
  context current when `xv_preempt` runs.
- **Calls**:
  - D3D HLE entries (`SetVertexShaderConstant`, `SetTexture`) through `XV_HLE_CALL`, with the same registers, stack
    words and return-address words. The phase/HLE timing, the object-job proxy and the tap all behave as for the
    lifted call.
  - Guest callees (`325C0`, `173F20`, the constant-pack prefix) get the full context, as the lifted body passes it.
- **NaN**: the payload a product or sum propagates depends on the host compiler's operand order. The lifted bodies
  built -O0 and -O2 already differ, so a NaN float word against a NaN float word counts as equal (and is counted).
  The runtime turns NaN constants into 0 anyway.
- **Declines** (the lifted body runs, counted `declined`): the colour packers with an unaligned `esp` (their
  `[esp+4]` constant slot); an object-job context (`XV_EXPERIMENTAL_OBJECT_JOBS`: the job proxy owns the HLE path).
  Neither occurs in a30.

## Knobs and counters

- Build: `XV_NATIVE_EFFECTS=1` (make var; adds `-DXV_NATIVE_EFFECTS=1` to `CFLAGS`/`RECOMP_CFLAGS` and the unit to
  `XITA_GAME_SRCS`), `XV_NATIVE_EFFECTS_DEFAULT=0|1|2` (the mode without the env key).
- Env `XV_NATIVE_EFFECTS`:
  - 0 off: the hook returns 0 and the lifted body runs.
  - 1 verify: journaled native, undo, lifted body, compare; the guest's result is kept.
  - 2 native.
- `XV_NATIVE_EFFECTS_TIME=1`: ns clock per hooked call. Mode 0 times the lifted body, mode 2 the native, mode 1 both.
- Acting set: by default `7E530`, `56F20`, `11B60`, `11610`, `11BD0` (the `on` column of `NX_HOOKS`). 7E420 and
  80360 are hooked and verified, but their natives are not faster in the game, so by default they run the lifted
  body. `XV_NATIVE_EFFECTS_FUNCS=all` makes all seven act; `XV_NATIVE_EFFECTS_FUNCS=7E530,80360,...` (hex) makes only
  the listed ones act. The log line at start names the acting set.
- Report every 60 frames (xd3d.c's weak chain):
  `[native-effects] 60 frames: calls N native N verified N mismatched N (total mismatches N) skipped N declined N
  journal-overflow N callee-diverged N nan-words N; 7E530 <calls> (us/call native X guest Y; ms/frame a/b) ...`
- Verify prints up to 16 `MISMATCH <fn> (esp .., stores, HLE calls): N differences` blocks, with up to 8 lines each.
- Verify details:
  - Only one verify reference run happens at a time (an atomic claim). A second thread meanwhile runs the lifted
    body, counted `skipped`.
  - `callee-diverged` (80360 only): the guest's texture cache `325C0` loaded a texture in one run and not in the
    other. The native's run left it resident, so the two runs did not start from the same state; the guest result
    stands. One such call in 39 M on x86.

## Verification

**Differential test** (`tools/test_native_effects.py <stage>/recomp [cases] --flags-from make-n.txt`, driver
`tools/tests/native_effects.c`):

- The inputs are the stage's lifted bodies of the seven functions and of their lifted callees (7DBE0, 1290E0, 80250),
  behind the same wrapper the stage gets, plus the unit.
- Each case runs in a synthetic arena of shuffled physical pages, so records straddle page ends and float accesses
  split. The stack is on reversed pages and the image window is `0x180000..0x3A0000`.
- `173F20` and `325C0` are replaced by deterministic stand-ins. They change registers, flags, memory and the x87
  stack, and sometimes return at an unexpected depth, which sends the x87-regs body down its memory lowering.
- The D3D HLE entries are replaced by stand-ins with the real register and stack effect. They log the name, the
  registers, the arguments and a hash of the constant or program data they read.
- Per case, mode 0, mode 2 and mode 1 run from identical state. The whole arena, every `xctx` field, the xv_preempt
  call count and the HLE log must match mode 0's, and verify must print no MISMATCH.
- The undo test runs the journaled native, undoes it without restoring the window, and requires the arena to equal
  its starting state, apart from what callees and stand-ins wrote.
- The scenes cover:
  - ties and NaN/infinite/huge floats, and all four rounding controls
  - 7E530 records aliasing its constant buffer
  - the light index -1 and the -1.0 attenuation test in 7DBE0
  - bitmap tags -1, sequence mismatches and frame counts of 0 in 80360
  - an unaligned `esp`
  - a small back-edge slice, so xv_preempt runs inside the loops

| unit | seed | variant | cases | mismatches | verify failures | undo failures | NaN words (counted) |
|---|---|---|---|---|---|---|---|
| final | 11 | plain page table -O2 | 20,000 | 0 | 0 | 0 | 4,119 |
| final | 11 | per-thread table + render view -O2 (the stage's configuration) | 20,000 | 0 | 0 | 0 | 4,119 |
| final | 11 | plain -O0 | 20,000 | 0 | 0 | 0 | 61 |
| before the tap fix | 1 | plain -O2 / -O0 | 20,000 each | 0 / 0 | 0 / 0 | 0 / 0 | 2,870 / 53 |
| before the undo test | 1 | the three variants | 20,000 each | 0 | 0 | | 4,807 / 4,807 / 47 |

Each seed's 20,000 cases are ~2,800-2,970 per function.

**Mutants** (`--mutants 4000`, plain -O2): 19 of 19 deliberately broken natives caught. Cases that differ, out of
4,000:

| mutant | cases differing |
|---|---|
| round half away from zero | 234 |
| rounding drops the sign | 332 |
| 7E530 scale slot not multiplied last | 414 |
| 7E530 loop condition signed as unsigned | 8 |
| 7E530 back-edge budget not counted | 390 |
| 11B60 without the green byte | 434 |
| 11B60 255 slot not left in st(4) | 547 |
| 11610 alpha masked to a byte | 50 |
| 11BD0 blue shifted by 8 | 531 |
| 7DBE0 -1.0 test inverted | 71 |
| 7E420 second record copy skipped | 69 |
| 56F20 rotation row difference reversed | 260 |
| 56F20 sin of the unrounded angle | 402 |
| 56F20 x87 base not moved on a depth miss | 136 |
| 80360 bitmap test on the wrong halfword | 18 |
| 80360 `sbb` without the carry of `neg` | 149 |
| journal skips word stores (verify undo) | 1,285 |
| undo in forward order | 2 |
| a view across a page end taken as one | 587 |

Two mutants were left out as equivalent:

- The carry an `inc edx` keeps in 7E530: the `shl` before it shifts i * 3 < 2^17 by 4, so its carry is always 0.
- The flag record of 7E420's `or eax,-1`: 7DBE0's first instruction rewrites it before anything reads it.

**Threads** (`--threads 8 --iters 200`, 64 scenes): 8 threads call the hooks at the same time on private arenas
through their own page tables, 102,400 calls per mode.

- x86, mode 2 and mode 1: 0 mismatches each.
- The same test cross-built for ARM (-O2 -mthumb, static) on the Pi 4, 2 threads on cores 2-3: mode 2 12,800 calls
  and mode 1 5,120 calls, 0 mismatches.
- Under ThreadSanitizer (4 threads x 20 x 64, both modes) the first run reported the verify claim word read without
  an atomic (`nx_tap`) and the tap installation. Both were fixed (atomic load, release publication). A second run has
  no report in the unit.
- The only remaining report is `xv_cur_fn`, the runtime's sampling-profiler word, which `XV_HLE_CALL` writes the same
  way in the lifted bodies.
- The Vita object has no `__thread` and no emutls reference. The unit's only mutable statics are the counters
  (relaxed atomics), the mode words (set once) and the claim.

**In-game verify** (`XV_NATIVE_EFFECTS=1`, every hooked call compared, a30 new campaign: the cutscene, then the pod;
scene on the helper with overlap mode 2, render view, the Vita play settings of `run30.sh`):

| run | where | frames | calls compared | mismatches | callee-diverged | skipped |
|---|---|---|---|---|---|---|
| ver2 | x86 host | 11,880 | 39,281,503 | 0 | 1 | 0 |
| ver3 (final natives) | x86 host | 10,680 | 35,089,641 | 0 | 2 | 0 |
| pver (final unit, `XV_NATIVE_EFFECTS_FUNCS=all`) | Pi 4, cores 2-3 | 4,140 | 12,229,252 | 0 | 1 | 0 |

Per function in ver2 + ver3:

- `80360` 25.4 M
- `11BD0` 21.3 M
- `11B60` 11.1 M
- `56F20` 7.9 M
- `7E530` 4.3 M
- `7E420` 4.2 M
- `11610` 0.18 M

0 journal overflows, 0 declines, 0 NaN words. Together on x86: **74.4 M calls compared, 0 mismatches.** ver2 and ver3
ran with all seven acting. ver3 used the final natives; after it only the acting-set default and comments changed.
The Pi run used the final unit and adds 12.2 M ARM calls (`80360` 4.2 M, `11BD0` 3.5 M, `11B60` 1.9 M, `56F20` 1.3 M,
`7E530` 0.71 M, `7E420` 0.68 M, `11610` 0.03 M), also 0 mismatches.

An earlier run (ver1) hooked 80250 and 7E5D0 as well. It had one mismatch in 13.9 M: the 80360 call in which `325C0`
loaded a texture during the native's run and found it resident in the lifted run. That led to the `callee-diverged`
classification (the budget each run consumed tells the two apart).

## Cost, guest vs native (host-side; not Vita numbers)

**In game, x86** (`XV_NATIVE_EFFECTS_TIME=1`, lockstep, runs t0b / t2b side by side, call-weighted over 8,820 frames;
all seven acting; the same per-call clock in both modes):

| fn | calls/frame | lifted us | native us | ratio | saved ms/frame |
|---|---|---|---|---|---|
| 80360 | 1,116 | 0.082 | 0.091 | 0.91 | -0.009 |
| 56F20 | 346 | 0.158 | 0.141 | 1.12 | 0.006 |
| 11BD0 | 936 | 0.039 | 0.029 | 1.31 | 0.009 |
| 7E530 | 187 | 0.162 | 0.105 | 1.55 | 0.011 |
| 7E420 | 183 | 0.124 | 0.158 | 0.78 | -0.006 |
| 11B60 | 490 | 0.038 | 0.033 | 1.15 | 0.002 |
| 11610 | 8 | 0.104 | 0.089 | 1.17 | 0.000 |

On x86 all seven together cost ~0.1 ms/frame of helper time, and the native saves ~0.01 ms. The x86 host is not the
target: its lifted code is cheap here.

The per-thread helper cycle counters (`XV_HOST_PERF=1`) of the same runs showed the mode-2 runs *higher*
(9.7 -> 11.0 and 9.4 -> 12.0 Mcycles/frame) at equal instruction counts. This came from placement, not from the
natives: those runs shared physical cores (SMT siblings 16-19 and 24-27) with other harnesses and with the verify
run, while the mode-0 runs had quiet siblings. The windows show bursts of 13-15 Mcycles in the contended runs only.
A placement-controlled rerun was not possible: another agent had pinned harnesses to all 32 CPUs.

**Microbenchmark** (`--bench 3200`: game-like scenes, lifted vs native alternating, ns/call and user-space
instructions from the perf counters; -O2):

| fn | x86 ns lifted / native | x86 instr | ratio (time / instr) | ARM (Pi 4, -O2 -mthumb) ns | ARM instr | ratio (time / instr) |
|---|---|---|---|---|---|---|
| 7E530 (bench n larger than the game's) | 520 / 111 | 10,675 / 3,082 | 4.67 / 3.46 | | | 2.88 / 3.89 (older, larger scene) |
| 7E420 | 17.5 / 19.0 | 406 / 443 | 0.92 / 0.92 | 259 / 228 | 492 / 531 | 1.14 / 0.93 |
| 56F20 | 69.7 / 58.2 | 1,567 / 1,235 | 1.20 / 1.27 | 703 / 522 | 1,653 / 1,295 | 1.35 / 1.28 |
| 80360 | 19.9 / 15.7 | 404 / 365 | 1.27 / 1.11 | 204 / 189 | 418 / 367 | 1.08 / 1.14 |
| 11B60 | 19.2 / 11.9 | 444 / 284 | 1.61 / 1.56 | 356 / 154 | 568 / 282 | 2.31 / 2.01 |
| 11610 | 23.5 / 10.6 | 480 / 255 | 2.22 / 1.88 | 321 / 154 | 609 / 266 | 2.08 / 2.29 |
| 11BD0 | 19.0 / 9.0 | 383 / 220 | 2.11 / 1.74 | | | |

(The ARM bench ran on the Pi while other agents' harnesses shared it, so its ns are high. The ratios are the
comparable part.)

**In game, Pi 4** (`XV_NATIVE_EFFECTS_TIME=1`, `XV_HOST_PERF=1`, lockstep, 240 s each, mode 0 (pt0) then mode 2
(pt2) with all seven acting, sequentially on cores 2-3 while another agent's harness ran on cores 0-1; 96 windows each;
the pod is frames after 3000):

| fn | calls/frame (pod) | lifted us (median, all / pod) | native us (median, all / pod) | ratio (pod) | saved ms/frame (all / pod, call-weighted) |
|---|---|---|---|---|---|
| 11BD0 | 1,005 | 0.546 / 0.552 | 0.225 / 0.226 | 2.44 | 0.281 / 0.327 |
| 7E530 | 199 | 3.085 / 3.204 | 1.659 / 1.686 | 1.90 | 0.218 / 0.297 |
| 11B60 | 514 | 0.706 / 0.734 | 0.346 / 0.350 | 2.10 | 0.166 / 0.193 |
| 56F20 | 372 | 2.944 / 3.005 | 2.564 / 2.574 | 1.17 | 0.078 / 0.144 |
| 11610 | 8 | 1.703 / 1.746 | 0.934 / 0.931 | 1.88 | 0.005 / 0.006 |
| 80360 | 1,197 | 1.151 / 1.181 | 1.184 / 1.194 | 0.99 | -0.046 / -0.022 |
| 7E420 | 196 | 2.057 / 2.101 | 2.540 / 2.583 | 0.81 | -0.110 / -0.100 |
| **default set** (the first five) | | | | | **0.748 / 0.967** |
| all seven | | | | | 0.592 / 0.846 |

The same per-call clock runs in both modes, so its overhead cancels in the difference.

The helper's perf counters of the pair (all seven acting): pod 26.47 -> 25.65 Minstr/frame (-3.1 %) and 46.2 -> 44.3
Mcycles/frame. The cycles are noisy (the other agent's harness shared the L2 and DRAM); the instruction count is not.

On ARM the lifted code of these leaves is 7-20x slower per call than on x86, against 3-11x for the natives. The x87
stack traffic and the lazy-flag records are relatively dearer there, and the natives gain more.

7E420 and 80360 are slower in the game than in the microbenchmark (0.8x against 0.92-1.14x). Both ports keep every
store and translation one for one, and their lifted bodies are already mostly loads and stores. The difference was
not pinned down. They stay hooked and verified, so the Vita can measure them (`XV_NATIVE_EFFECTS_FUNCS=all`), but they
do not act by default.

## Expected on the Vita (estimate, to be measured)

The Pi saving of the default set is 0.75 ms/frame over the cutscene and the pod, and 0.97 ms/frame in the pod, at
1.8 GHz: 1.35-1.75 Mcycles of helper time per frame. The estimate assumes that, like the other Pi-based estimates
(`docs/d3d-record-speed.md`), a Cortex-A9 needs about as many cycles as the A72 for this code. It also assumes the
same calls per frame as on the host (they are per rendered frame: effects, shader maps, lights). On those
assumptions it is **3.0-3.9 ms/frame at 444 MHz**. If the Vita's frame makes fewer of these calls (the D3D recording
work found about 2/3 of the Pi's draw count in a10), it is **2.0-2.6 ms**. So **about 2-4 ms/frame** off the scene
helper.

The A9 is narrower and its memory slower. The natives remove mostly memory traffic (context fields, flag records, x87
slots, translations), so the Vita's ratio may be nearer the instruction ratios (1.6-2.3x for the packers and 7E530).

This is not a frame-time prediction. The a30 frame is set by the slower of the owner and the helper, and by the GPU.
It is far from the 20 fps target on its own: the whole effects pass is ~6 % of the helper. The larger remaining
masses on the same thread are the material setup `f_00070110` (11.6 % of the Pi helper; another agent's port),
`54010`, `A2380`, `A26B0` and the runtime's draw recording.

Measure on hardware:

1. `XV_NATIVE_EFFECTS=1` (verify, `mismatched 0`).
2. `XV_NATIVE_EFFECTS=0` and `=2`, each with `XV_NATIVE_EFFECTS_TIME=1`. The per-window `ms/frame` pairs sum to the
   saving. The Vita clock is microseconds, so the per-call values are coarse.
3. Without the timer: the scene phase timers or the frame time.

## Vita integration (overlap-candidate stage)

Files on branch `work/native-effects-20260924`:

1. `recomp/kernel/xk_native_effects.c` (new). The Vita object has 55.2 KB of text, 680 B of data and 20 B of BSS.
   There is no `__thread`, no emutls reference and no `vfma`.
2. `Makefile`: the `XV_NATIVE_EFFECTS` block (flag, `_DEFAULT`, the `-ffp-contract=off` rule for the unit).
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_effects.c` under
   `ifeq ($(XV_NATIVE_EFFECTS),1)`.
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_effects_report(60)` after `xv_native_1721b0_report`.
5. The shards that define the seven functions (build-x87: `code_000.c` 11B60/11610/11BD0, `code_010.c` 56F20,
   `code_012.c` 7E530/7E420, `code_013.c` 80360). Each gets a wrapper in front of its body, which is renamed
   `f_XXXXXXXX_body` under the flag (`tools/patch_native_effects_hooks.py`). The wrapper line starts with the
   extern declaration, so the Makefile's `XV_RENDER_GUEST_SIZE` check (`rg '^void f_0007E530\b'` over the shards)
   still finds the count it expects.

Steps for `overlap-candidate/build-x87` (the same for `overlap-candidate/build`):

1. Run `python3 tools/install_native_effects.py overlap-candidate/build-x87` from this checkout.
   - It copies the unit.
   - It adds the Makefile block after the stage's `xk_native_1721b0.o` -ffp-contract rule (falling back to the
     4B9D0, 92330 or visibility one).
   - It adds the runtime.mk lines after the 1721B0 lines, and the xd3d.c call after the 1721B0 report call.
   - It runs the hook patcher, which prints one line per wrapper.
   - It is idempotent. On a stage patched by an earlier form of the patcher, it replaces the old wrappers and
     removes those of functions no longer hooked.
   - It was run here on a copy of build-x87, and again on the same copy (all "already present").
2. Add `XV_NATIVE_EFFECTS=1` to `overlap-candidate/make-vars.txt`, and to the make variables in `build-command.json`
   if the build driver takes them from there. The shards, `xd3d.c` and the unit rebuild by mtime and flags. On the
   scratch copy, `make -j16 build/xita.elf <build-command args> XV_NATIVE_EFFECTS=1` recompiled 15 units and linked.
3. Check:
   - `arm-vita-eabi-nm build/recomp/code_000.o` shows `T f_00011B60`, `T f_00011B60_body` (same for 11610, 11BD0)
     and `U xv_native_effects_00011B60`, `_00011610`, `_00011BD0`.
   - `code_010.o` shows `T f_00056F20`, `T f_00056F20_body`, `U xv_native_effects_00056F20`.
   - `code_012.o` shows the same for 7E530 and 7E420.
   - `code_013.o` shows the same for 80360.
   - `arm-vita-eabi-nm build/recomp/kernel/xd3d.o | grep effects` shows `w xv_native_effects_report`.
   - `arm-vita-eabi-nm build/recomp/kernel/xk_native_effects.o | grep -ci emutls` is 0.
   - `arm-vita-eabi-objdump -d build/recomp/kernel/xk_native_effects.o | grep -cE 'vfma|vfms|vfnm'` is 0.
   - The ELF has `T xv_native_effects_0007E530` ... `T xv_native_effects_report`. Its 12 emutls symbols are the same
     12 the stage's ELF has without the flag.

   All of these were checked here on the scratch copy with the final unit. The ELF grows by 66 KB.
4. Runtime:
   - First run `XV_NATIVE_EFFECTS=1` and expect `mismatched 0`. Verify pays for the native, the journal and the
     guest, and the scene helper is slower meanwhile.
   - Then compare `XV_NATIVE_EFFECTS=2` against `0`, either with `XV_NATIVE_EFFECTS_TIME=1` (the us/call pairs; the
     Vita clock is microseconds, so per-call values are coarse and only the sums per window mean much) or with the
     scene phase timers.
   - For verify, add `XV_NATIVE_EFFECTS_FUNCS=all` so that 7E420 and 80360 are compared too. The same setting in
     mode 2 measures whether they pay on the A9.
   - Once hardware agrees, `XV_NATIVE_EFFECTS_DEFAULT=2` in the make vars makes it the default.

## Findings along the way

- The whole effects pass is ~6 % of the helper. The 20 fps target in a30 cannot come from this subtree. Its shared
  leaves, plus the model material path's own leaves (`70110`, `54010`, `A2380`, `A26B0`), are the larger mass.
- The lifted glue of HLE-bound functions (7E5D0, 80250, 11C80, 11E30) cannot be sped up by a native: the HLE entries
  and the runtime's recording hold their time.
- On x86 these leaves cost tens of ns per call and the natives gain little. On ARM the lifted code of the same leaves
  is 5-20x slower per call than on x86 (Pi bench and in-game us/call). The x87 stack traffic and the lazy flags cost
  relatively more there, and the natives gain more.
- Host helper cycle counters are placement-sensitive (SMT siblings, CCDs). Instruction counts and per-call clocks
  are the robust measures on the shared machine.

## Not done / next

- Hardware: nothing has run on the Vita. The next step is a verify run, then the 0/2 pair (see Runtime above).
- Replay of captured calls: not implemented. In-game verify compares every call on its real state, on x86 and the Pi.
  A capture/replay harness would add a noise-free benchmark on real inputs, not more exactness. The spec's
  replay item is open.
- A placement-controlled x86 A/B of the helper cycle counters (both modes on quiet, symmetric cores).
- The rest of the effects pass (`66510`, the sort) and the HLE-bound wrappers: low payoff per the measurements above.
- The acting set: adjust the `on` column once the Vita numbers say which hooks pay there.
- Why 7E420 and 80360 lose in the game (0.8x / 0.97x) while the microbenchmark has them even: not found.
