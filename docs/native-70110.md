# Native material setup f_00070110 (`XV_NATIVE_70110`)

Sept 24 2026. Branch `work/native-70110-20260924` (worktree `native-70110-wt`, not pushed). Status: implemented and
verified on the x86 host harness and the Raspberry Pi 4 harness, in a30 with the Vita play settings (scene on the
helper, overlap mode 2, render view). Off by default. The unit, the hooked `code_011.c` and `xd3d.c` compile with the
Vita command lines; not built into a VPK, not run on the Vita.

`f_00070110` sets up one material for the model and structure passes: the render states (z enable, z bias, cull mode,
nine `SetRenderState_Simple` writes and their shadow words at `0x18F464..0x18F4A8`), four texture stages (`f_00080360`
binds each, then 5-6 `SetTextureState_Deferred`), the per-material vertex constants (x87 math on the material and the
camera page `0x2FC000`, `f_00056F20`, `SetVertexShaderConstant`), the fog colours (`f_000118D0`, `f_00011B60`,
`f_00011BD0`), and the draw (`f_0006F340`, `f_0007A960`, a second pass when the material asks, `f_000736F0`).
In a30 it runs 244 times per frame on the scene helper.

The body is now native (`recomp/kernel/xk_native_70110.c`). Every callee is still the translated function, and every
D3D call is still the same HLE function, called directly.

## Status (handover, Sept 24 2026 21:35)

**Ported:** the whole body of `f_00070110`, every path except the prologue's three rare paths, which decline. The 19
translated callees, the 40 D3D HLE setters, the model UV/fog memos and the material-sampler groups are called as the
lift calls them.

**Verified on the final code** (commit `dc31215` plus this doc, where the unit differs only in comments and one report
label):
- **In-game verify, x86 harness, a30 with the Vita play settings:** 20.3 M calls and 1.21 G compared sites, 0
  mismatches.
  - scene helper, pod: 17.15 M calls
  - scene helper, walking: 1.91 M calls
  - scene on the owner, where the sampler, UV and fog memos are active: 1.24 M calls
- **In-game verify, Pi 4 (ARM), scene helper, walking:** 1.49 M calls and 90.2 M sites, 0 mismatches.
- **Earlier builds:** 18.4 M more calls in-game, 0 mismatches except 3 verify-checker false positives on the owner.
  `X_PREEMPT`'s yield sets `eip_hint`; the checker was fixed.
- **Differential tests:** 460,000 cases (7 builds, 4 seeds, `XV_HLE_TIMING` on and off), 0 mismatches.
- **Mutants:** 31/31 caught.
- **Threads test (x86):** 32,000 concurrent runs, 0 mismatches. A deliberately shared env gives 1,733 mismatches.
- **Replay** of 6,000 captured a30 calls on x86: 0 mismatches.

**Speed:**
- **Pi 4 scene helper,** in-game, alternating phases in the same run, final build: **-1.83 +- 0.04 Mcycles/frame
  (-5.2 %)**. An earlier build measured while another job shared the L2: -2.32 Mcycles/frame (-5.4 %).
- **x86 lockstep pair:** the whole call (callees included) 2.744 -> 2.516 us/call.
- **Vita (estimate, not measured):** about 3-4 ms/frame off the scene helper in a30.

**Not done:**
- The ARM differential, threads and replay runs on the Pi. The binaries are built (`~/xita-70110/n70test-arm-os`,
  `n70test-arm-plain`, captures in `~/xita-70110/cap/`); `native-70110-work/scripts/pitests.sh` runs them all on cores
  0-1.
- Not built into a VPK, not run on the Vita or in Vita3K.

**Next steps:**
1. Run `native-70110-work/scripts/pitests.sh` (about 15 minutes) and add its results to the tables below.
2. Install into `overlap-candidate/build-x87` (see "Vita integration"), build, and check the symbols.
3. On hardware: first `XV_NATIVE_70110=1` (verify; expect `mismatched 0`), then `XV_REC_AB=120
   XV_REC_AB_KNOBS=XV_NATIVE_70110` for the helper and frame A/B.
4. If hardware agrees, set `XV_NATIVE_70110_DEFAULT=2`.

## Where the time goes

**Sampling** (`XV_HOST_SAMPLE`, the helper sampled every 200,000 user cycles on the Pi and every 100,000 on x86; a30 from
its start with the play settings; windows after frame 1500). `f_00070110`'s own code, not its callees, is the largest
single guest function on the helper:

| machine | 70110 self | of the helper | per call |
|---|---|---|---|
| Pi 4 (`harness-base`, 227 windows) | 3.60 Mcycles/frame | **8.9 %** | ~14,700 cycles |
| x86 (123 windows) | 0.67 Mcycles/frame | 6.0 % | |

The self time is spread over the whole body. By guest address range (Pi samples of 70110, source lines mapped to the
lift's x86 address comments):

| range | what | share of self |
|---|---|---|
| 70110-70264 | prologue, the rare-path tests | 1.9 % |
| 70265-70423 | alpha/fade math, 11 render-state HLE calls, shadow words | 12.8 % |
| 70424-705CB | four texture stages: 4x `f_00080360`, 21x `SetTextureState_Deferred` | 16.7 % |
| 705CC-707E0 | fade, `B5130`/`173F20`/`111A0`, light colour, stage tests, `658D0` | 12.9 % |
| 707E1-7095F | vertex-constant math, `f_00056F20` | 20.5 % |
| 70960-70A33 | `SetVertexShaderConstant` x2-3 | 5.8 % |
| 70A34-70D11 | fog colours: `118D0`, clamps, `11B60`, `11BD0` x2 | 21.0 % |
| 70D12-70EF2 | fog/draw: `11BD0` x2, `6F340`, cull, `7A960`, second pass, `736F0` | 7.8 % |

By inlined helper, 50 % of the self samples are the body's own statements. 26 % are `x_guest_read` (the float loads of
`x87_load_f32`), and the rest are `x_guest_write`, `x87_push`/`pop`/`compare` and `xv_is_object_job`. On the Pi the body
also took 15.6 % of the helper's L1 instruction-cache refills (`icache1` in `d3d-record2-work`). Its code is 46 KB
(ARM, -O1) on the harness and 22 KB (Thumb, -Os) on the Vita.

**The subtree**, from a probe build with a timer pair around every call site (the counter is rdtsc on x86 and CNTVCT,
54 MHz, on the Pi; per call, a30):

| part | x86 | Pi 4 |
|---|---|---|
| inclusive | 13,206 ticks | 2,397 ticks (44 us) |
| body (inclusive minus the sites and the timer pairs) | 11.4 % | 16.7 % (7.4 us) |
| `f_0007A960` (the draw submission, x1.2) | 39.6 % | 54.4 % |
| `f_00056F20` (x1.2) / `f_00080360` (x4) / `f_0006F340` | 5.3 / 7.0 / 5.8 % | 5.7 / 5.5 / 3.1 % |
| `f_00011BD0` (x4), `11B60`, `111A0`, `118D0`, `658D0`, `B5130`, `173F20` | 6.3 % | 7.4 % |
| 40 D3D HLE calls (21 Deferred, 9 Simple, VSC x2.2, cull x2.2, z enable, z bias) | 11 % | 7.1 % |

**Why this boundary.** The draw `f_0007A960` is the largest callee, but it is a separate subtree (`7A130`, `7A1F0`,
`7A3D0`, `7A460`, the recording path) with its own profile entries. Everything else under 70110 is spread over a dozen
functions, and most of them are shared with other callers. The body is the largest single part, and it is
self-contained: it is straight-line code with calls, and its only loop is one back-edge. So the whole body is native,
and the callees and HLE calls stay as the guest makes them.

## What is native, and how it stays exact

`n70_core` is a transliteration of the lifted body: one statement per lifted statement, with the x86 address and
instruction in a comment. It was drafted by a generator from the lift and then reviewed. The generator is kept in the
work directory, not in the repository. What differs from the lift is only where state is kept:

- **Registers, lazy flags, x87 top and status word are locals.** Each is written exactly where the lift writes it. For
  the flags that is the whole record, both override cells and both stale cf/of cells. The lift's dropped flag writes
  stay dropped: `xor r,r` without flags, `neg` / `and` records as emitted. `shr r8` writes both cells.
- **The x87 slots are locals too.** The depth (pushes since the entry) is static at every statement: the generator checks
  it at every join, and three callees return a float. Slot `j` holds `c->st[(T0 + j) & 7]`, where T0 is the entry top,
  re-derived from `c->fsp` after every call. So `st(i)` at depth `d` is slot `(i - d) & 7`.
  - Float math runs in doubles in the lift's operand order and association.
  - Loads are widened from float, and stores are rounded to float.
  - The unit is built with `-ffp-contract=off`, and the Vita object has no `vfma`.
  - Compares use the lift's condition codes. On Thumb-2 this is `x87_compare`'s own `vcmp`/`vmrs` form.
  - Every compare ORs its TOP into the status word, and the other status bits are kept.
- **Only what changed is stored.** The generator runs a dataflow analysis over the body's control flow and lists, at
  every call site, the fields written since the context was last stored or loaded. There are 30 candidates: 8 registers,
  9 flag fields, fsw and 8 slots, plus fsp at depth `d` (always stored). Before a translated callee or a hook, only those
  fields are stored. After it, everything is loaded back, and the compiler drops the loads nothing reads.
- **HLE calls.** An HLE call stores only the changed ones among ecx/edx/esp, which is what the six setters read. It then
  goes through `XV_HLE_CALL`'s bookkeeping: the profiler's `xv_cur_fn` and the `XV_HLE_TIMING` branch. An object-job
  context is declined at entry. Afterwards esp is `E - off`, and eax is reloaded after `SetVertexShaderConstant`.
- **The fnstsw ax / test ah,MASK / jcc chains.** All 32 are branches on the compare's condition codes. For the masks used
  (0x44, 0x41, 0x05) the flags depend only on C3/C2/C0 of that compare. The statements that set eax, fsw and the flag
  record stay, and the compiler drops the ones nothing reads.
- **Guest memory is addressed as the lift addresses it**, through the calling thread's page table (`X_PT`: TPIDRURW on
  the Vita, the render view on the helper), taken once per call:
  - Integer accesses use one translation, like `X_M32`.
  - x87 float accesses are page-split, like `x87_load_f32` and `x87_store_f32`.
  - The frame window `[E - 0x100, E + 0x20)` is translated once per call. So are the image pages the body addresses by
    constant (0x18F, 0x1F0, 0x232, 0x2E3, 0x2FC), as `X_IMG*` and `x87_load_f32` do.
  - Every read happens at the lift's point, and every write in the lift's order. The four callee-saved pushes, every
    argument and return address, and the dead frame words are all written.
- **The material** is ebp from 70118 on (argument 0, callee-saved). In the fast instance it is read from one page
  pointer when `[ebp, ebp + 0x180)` lies in one page.
- **The back-edge** (70321 -> 7030D, `X_PREEMPT`). `c->preempt` is charged there, and `xv_preempt` is called at that
  point with the state stored.
- **The lift's optional hooks** are called where the lift calls them, with the lift's arguments:
  - `xk_model_uv_begin/end` around the two `f_00056F20` calls
  - `xk_model_fog_begin/end` around the fog region
  - `xv_material_sampler_try` for the four texture groups

  Their `xpt_`/`imgb_` arguments are the thread's table and image base under `XV_THREAD_PAGE_TABLE`, as the shard's
  preamble defines them. The hook passes which of them the shard was compiled with (`cfg`).
- **The phase scope** (`XV_PHASE_SCOPE(c, 24)`) is opened at 70265, the first point no decline can follow, so a declined
  call is counted once.

**Three instances** of the core, one source:
- **FAST (mode 2).** The frame window and the material each lie in one page. esp is always `E - off`, where `off` is the
  lift's stack discipline, which the generator simulates and checks at every join. So every frame slot is `w0 + const`
  and every material field is `mb + const`. After every call it checks that esp and ebp are as the discipline says.
  Otherwise the call continues in GEN after that site, with the state already in the context. In a30 that never
  happened.
- **GEN.** It runs when the window or the material straddles a page. It is the same code with every access translated
  like the lift's. In a30 about 4 % of calls have a material across a page end (`[native-70110] ... GEN restarts`).
- **DRY (verify).** It stops at the next site with its predicted state in a private context. Frame writes go to a
  shadow, and other writes to an overlay. It resumes after a site from the actual state.

**Declines** (the translation runs). Nothing has been written before a decline except the four callee-saved pushes,
which the translation then writes again with the same values.
- The prologue's rare paths: `[2E3520]+0A8h` set and failing its tests (the 6EFC0 path), `[2E3528]` neither 0 nor 1,
  and `[ebp+24h] == 3` (the 6F860 path). In a30 there were none.
- An object-job context.
- esp not 4-aligned, the window near the ends of the address space, or the window over the image pages.

**Assumed, and checked in verify mode on every call:**
- The six HLE setters change only esp (`X_RET(n)`), plus eax for `SetVertexShaderConstant`.
- `xv_preempt` changes only the budget and `eip_hint`. On the owner its yield sets `eip_hint` to `[esp]`.
- The page-table entries of the window, the material and the image pages do not change during one call. A stack page in
  use and the image pages are never remapped, and the render view retargets pages only at scene boundaries.

**Not reproduced:** NaN payload bits. Which operand's payload an operation propagates depends on the host compiler's
operand order. Such a word can reach the frame, a slot, the VSC rows or, through the float stored at `[esp+B0h]` and
reloaded as an integer (7086C), ecx. The checks accept a NaN word against a NaN word and count them (`nan-words`).

**Threads.** The owner and the scene helper (through its render view) can both be in 70110 at once. The unit has no
`__thread` and no shared mutable state: vitasdk's `__thread` is emutls, which did not keep the Vita's threads apart
(docs/native-1721b0.md). A verify session lives in its call's heap block and reaches the tapped copy as an argument.
Shared state is limited to the atomic counters and the mode word, which is set once. On the Vita, `.bss` is 88 bytes.

## Verify mode

The hook tool gives the shard a second copy of the body, `f_00070110_vbody`, with a tap before and after every site: 72
taps in the lift (68 sites, the four sampler groups through `xv_native_70110_sampler`). In verify mode the tapped copy
runs, so the guest's result stands. At the entry and after every site, the native runs DRY from the actual state to its
next site. At that site's pre-tap, its prediction is compared with the actual state:
- all eight registers
- the whole flag record
- st[0..7], fsp, fsw, fcw
- the budget, fs_base, df, scratch, eip_hint, fiber, mm, xmm
- the frame window
- the render-state shadow words `0x18F460..0x18F4B0`, against their snapshot with the native's writes applied
- every other native write

After an HLE call the post-tap checks that only esp (+4+4n) and, for VSC, eax changed. After `X_PREEMPT` it checks the
budget rule and `eip_hint`. A control-flow difference is reported as `site` and resynchronizes at the next post-tap.
Segments with an odd access are skipped and counted: an integer access across a page end, or one partly in the window.

## Knobs and counters

- **Build: `XV_NATIVE_70110=1`.**
  - `Makefile`: the block after `XV_NATIVE_1721B0`'s (flag, `XV_NATIVE_70110_DEFAULT` = 0, `-ffp-contract=off` for the
    unit).
  - `games/halo_ce_3925/runtime.mk`: the unit.
  - `recomp/kernel/xd3d.c`: one weak report call after `xv_native_1721b0_report`.
- **Hook: `python3 tools/patch_native_70110_hooks.py <stage>/recomp`** (idempotent). The shard gets:
  - the wrapper `void (f_00070110)(xctx *restrict c) { if (!xv_native_70110(c, cfg)) f_00070110_body(c); }`. The name
    is in parentheses so that the Makefile's `XV_RENDER_GUEST_SIZE` check still counts one `^void f_00070110`
    definition.
  - the renamed body.
  - the tapped copy. Its marker comments are renamed, so the Makefile's counts of `XV_MODEL_UV_CALLS:`,
    `XV_MODEL_FOG_SCOPE:` and `XV_MATERIAL_SAMPLER_GROUP` are unchanged.

  Every file the tool or the installer changes is written as a new file, so a hard-linked stage copy leaves its origin
  alone. `tools/install_native_70110.py <stage>` does all of it.
- **Env `XV_NATIVE_70110`:** 0 off (default), 1 verify, 2 native. `XV_REC_AB=<frames>` with `XV_NATIVE_70110` in
  `XV_REC_AB_KNOBS` alternates 0 and 2 every `<frames>` recorded frames (`runtime/xv_record_opt.h`, `tools/rec_ab.py`).
- **Env `XV_NATIVE_70110_TIME=1`:** us/call of the whole call, callees included, in modes 0 and 2 (ns clock on the host,
  us clock on the Vita).
- **Report every 60 frames:**
  `[native-70110] 60 frames: native N (frame across a page end N, GEN restarts N) guest N declined N (prologue N layout N
  object-job N); verified N segments N mismatched N skipped N nan-words N (total mismatches N); on the scene helper N;
  us/call native X (N) guest Y (N)`. Up to 16 `MISMATCH <what> native .. guest .. (site, E, material)` lines are
  printed.
- **Host harness only:** `XV_NATIVE_70110_CAPTURE=<file>[:n[:skip]]` writes the page table, then per call the context
  and every block the body reads, for `--replay`. Captures are game memory: they are private and stay out of the
  repository.

## Verification

**Differential tests** (`tools/test_native_70110.py <stage>/recomp [cases] [--seed N]`, driver
`tools/tests/native_70110.c`). The script extracts the lifted body and the shard's preamble and patches them like the
stage (wrapper, renamed body, tapped copy).

- **Callees.** Every callee is a deterministic stand-in whose effects are a hash of everything it can see: all
  registers, the whole flag record, x87 slots/top/status/control word, the budget, 64 bytes above esp, and the call's
  sequence number. It:
  - clobbers eax/ecx/edx and sets a random flag record
  - pushes a float result (B5130, 173F20, 118D0) and scribbles dead x87 slots
  - writes its own frame below esp and its output buffers (111A0, 56F20; 0.0 and 1.0 often)
  - spends budget and pops its arguments like the real one
- **HLE stand-ins** record what the real setters read and do what they change (esp, eax for VSC, the texture-state
  word).
- **Hooks.** The memo stand-ins hit or miss by the hash. A hit rewrites registers, flags, x87 and frame words, like a
  replay would. The builds with the hooks run the real `xv_material_sampler_defaults`.
- **Scenes.** Random materials, render contexts, light tables, fog and camera globals, with NaN, inf, huge, zero, signed
  zero and denormal values. They reach every branch of the body:
  - the decline paths, the z-enable bit
  - the stage and alpha tests (0.0/1.0 chains), the 621E0 path, the third vertex constant
  - fog hits, the second pass, 736F0
- **Entry state.** Random flags and x87 slots, an unmasked top (fsp 8..15), a budget about to run out, esp not
  4-aligned.
- **Layout.** The window within one page or across a page end, and materials across a page end (sometimes unaligned:
  split float loads). Tag pages are shuffled and stack pages reversed.

Each case runs the guest body (mode 0), the native (2) and verify (1) from identical state. It compares:
- the stack, tag and image pages
- every xctx field
- the callee sequence with each call's view hash
- the HLE calls, the memo calls, xv_preempt calls and the phase scope

Verify mode must also leave the guest's result and report no mismatch.

| builds (stage `overlap-candidate/build-x87` bodies) | seeds | cases each | mismatches | verify |
|---|---|---|---|---|
| plain -O2; hooks (UV, fog, sampler) -O2; hooks + thread table + render view -O2; hooks + thread table -O2; hooks + object jobs -O2; hooks -O1; hooks -O0 | 21, 22, 23 | 20,000 | 0 | 0 |
| plain -O2; hooks + thread table + render view -O2, with `XV_HLE_TIMING` on | 24 | 20,000 | 0 | 0 |

That is 460,000 cases and 0 mismatches. Per 20,000 cases: ~15,800 native, ~8,300 declined (the rare paths on purpose),
~1,250 with the window across a page end, ~3,050 GEN restarts, ~1.3 M callee and HLE events, ~740,000 verify segments.

The same suite on ARM (Pi, Thumb, guest -Os) is built but not run (see Status).

**Line coverage** (gcov, 20,000 cases with verify, `XV_HLE_TIMING` on): 91.8 %. Every statement of the transliterated
body ran. The unexecuted lines are:
- the page-split float store (stack floats are 4-aligned)
- the host capture and the timing branches
- the mismatch reporting
- the resume jump table's case lines, which gcov does not attribute

**Mutants** (`--mutants`, 1,500 cases each; a crash counts): **31/31 caught**.

| mutant | cases differing |
|---|---|
| fsw: the TOP of a compare not OR-ed in | 741 |
| x87 top not re-derived after a float-returning callee | 1,191 |
| ecx never stored | 1,191 |
| the stale carry cell never stored | 892 |
| x87 slot 6 never stored | 1,191 |
| fsw never stored | 1,181 |
| back-edge budget not charged | 148 |
| sampler group outcome inverted | 1,191 |
| fog memo hit not taken | 306 |
| uv memo token not passed | 726 |
| FAST frame base off by 4 | 1,323 |
| image constant 0.0 read as 1.0 (prologue compare) | 18 |
| sbb without the carry of neg | 292 |
| shr bl,3 by 2 | 758 |
| shr cl,4 without its flag cells | 1,191 |
| render-state shadow 18F474 not written | 1,191 |
| render-state shadow 18F480 from edx | 616 |
| sete cl inverted | 616 |
| test ah,5 / jp tested as ZF | 156 |
| fxch at 70292 missing | 1,145 |
| fsubr at 70B0E as fsub | 228 |
| light index scaled by 4 instead of 3 (70665) | 687 |
| stack word [esp+10h] = 1.0 missing (70305) | 341 |
| cull mode pushed as 900h | 1,191 |
| material across a page end read from one page | 173 |
| texture of stage 0 from [ebp+E8h] | 1,191 |
| fstp [esp+B0h] missing | 1,191 |
| dirty list of the first texture bind without eax | 1,167 |
| HLE: ecx stored after the call | 1,191 |
| verify: the dry run does not write its frame shadow | 1,191 |
| verify: the dry run stops after an HLE instead of before | 1,191 |

Three mutants were equivalent and were replaced:
- eax not reloaded after `SetVertexShaderConstant`: eax is written again before any read or store after all four.
- movsx at 70662 as movzx: ax is 1..4 there.
- The first material-page mutant, before the scenes put materials across page ends.

**Threads** (`--threads N --iters K`, the per-thread-table build). N host threads run the same scene at once, each
through its own page table whose stack pages are private copies (the render view in the game). Every result must equal
the single-threaded one, and that must equal the guest's.

| mode | threads x runs per scene | scenes | runs | mismatches |
|---|---|---|---|---|
| 2 (x86) | 8 x 10 | 300 | 24,000 | 0 |
| 1 (x86) | 8 x 5 | 200 | 8,000 | 0 |
| 2 (x86), control: the unit with its per-call environment made `static` (shared) | 8 x 10 | 100 | 8,000 | **1,733** |

**Replay of captured calls** (`XV_NATIVE_70110_CAPTURE` in a30; `--replay`). The captured entry states and memory run
with the stand-ins: guest, native and verify from each call's own state.

| capture | calls | x86 | Pi 4 |
|---|---|---|---|
| cap1: calls 150,000-152,999 (the lifepod cutscene) | 3,000 | 0 mismatches, verify 0 | not run |
| cap2: calls 900,000-902,999 (the pod) | 3,000 | 0 mismatches, verify 0 | not run |

**In-game verify** (`XV_NATIVE_70110=1`: every call runs the tapped guest copy, the native predicts every segment, and
the guest's result stands). a30 is loaded from its start (`XV_LEVEL=a30`, new campaign) with the Vita play settings:
- scene thread, overlap mode 2, render view (thread tables, split, early)
- 360p, the play config's other natives in mode 2, `XV_REC_*=2`
- `XV_SOUND_OBSTRUCTION=6`, `XV_OCCL=2`, `XV_LOCKSTEP=2`

| run | build | where | scene / pad | frames | calls verified | sites compared | mismatches |
|---|---|---|---|---|---|---|---|
| fverC | final | x86, scene helper | pod (default pad) | 70,980 | 17,154,281 | 1,043,256,153 | 0 |
| fverA | final | x86, scene helper | walking the whole run | 71,880 | 1,910,373 | 115,946,886 | 0 |
| fverB | final | x86, scene on the owner (sampler, UV, fog memos active) | walking | 44,880 | 1,241,952 | 49,307,288 | 0 |
| pverA | final | Pi 4, cores 0-1, scene helper | walking | 68,400 | 1,486,891 | 90,173,161 | 0 |
| ver1 | first version | x86, helper | pod | 8,880 | 2,001,894 | 121,755,065 | 0 |
| ver2 | x87 slots as locals | x86, helper | pod | 17,880 | 4,197,886 | 255,305,574 | 0 |
| ver3 | fused compares | x86, helper | pod (stopped at 18 min) | 33,720 | 8,062,839 | 490,354,905 | 0 |
| verown1 | fused compares | x86, owner | pod | 17,760 | 4,168,495 | 165,979,792 | 3 (checker, see Findings) |

On the final build that is 21.8 M calls and 1.30 G sites, all of them compared, with 0 mismatches, 0 declines, 0 skipped
segments and 0 NaN words. On the helper, 4 % of the calls ran GEN (a material across a page end) and no call left FAST
through an unexpected esp/ebp. With the walking pad a30 draws only ~25 materials per frame (the pod: 244).

## Speed

**In game, Pi 4** (`XV_REC_AB=120 XV_REC_AB_KNOBS=XV_NATIVE_70110 XV_HOST_PERF=1`). The native alternates with the
translation every 120 recorded frames within one run. Neighbouring 60-frame windows of opposite phase are paired
(`tools/rec_ab.py`), so drift cancels. Pi cores 0-1, a30 pod, the Vita play settings, frames after 1500. The Pi harness
builds the guest and the native at -O1 (ARM mode).

| run | build | pairs | helper Mcycles/frame | helper Minstr/frame | helper CPU ms/frame |
|---|---|---|---|---|---|
| pabA | final | 278 | 34.91 -> 33.09: **-1.826 +- 0.042 (-5.2 %)** | -0.785 +- 0.043 | -1.05 |
| pab3 | before the material page; other job on cores 2-3 | 262 | 43.02 -> 40.70: -2.317 +- 0.064 (-5.4 %) | -0.735 +- 0.044 | -1.33 |
| pab2 | slots as locals, no remap | 251 | -2.319 +- 0.265 | -0.666 | -1.33 |
| pab1 | first version (remap per callee, slots in `c->st`) | 244 | -1.497 +- 0.085 | -0.278 | -0.84 |

The frame in pab3 dropped by 0.895 +- 0.082 ms. pabA's frame is not helper-bound: its helper takes 22 ms of a 33 ms
frame, and the frame moved by -0.013 ms. Per call (244/frame) the native saves 7,500-9,500 cycles. The sampled self cost
of the body was ~14,700 cycles.

**In game, x86** (lockstep pair: `XV_LOCKSTEP=2`, the same calls in both runs, 8,374,080 each; `XV_NATIVE_70110_TIME=1`;
mode 0 on CPUs 0-7, mode 2 on CPUs 8-15, 1,200 s). The whole call, callees included: 2.744 us (translation) -> 2.516 us
(native), -0.228 us per call. The body was 11.4 % of the call (~0.31 us) in the probe.

**Microbenchmark** (`--bench`: 400 game-like scenes, every stand-in as cheap as possible, user instructions and cycles
from perf counters). The lift is built -Os like the Vita's `code_011` and -O2 like the kernel units; the native is
built -O2. The same 19 callee and 40 HLE stand-ins alone cost 2,981 instructions and 1,067 cycles per call (x86). They
are part of both sides.

| where | guest | native | ratio |
|---|---|---|---|
| x86, guest -Os | 10,333 instr, 4,875 cycles | 4,918 instr, 2,578 cycles | 2.10x / 1.89x |
| x86, guest -O2 | 7,899 instr, 3,473 cycles | 4,918 instr, 2,515 cycles | 1.61x / 1.38x |
| Pi 4 (Thumb), guest -Os, native -O2, before the material page | 9,418 instr, 11,807 cycles | 5,297 instr, 6,360 cycles | 1.78x / 1.86x |

**Replay of the captured a30 calls** (x86, 3,000 calls x 30 passes, alternating guest and native):

| capture | guest -Os | native | ratio (instr / cycles) | guest -O2 | ratio |
|---|---|---|---|---|---|
| cap1 (cutscene) | 10,895 instr, 4,310 cycles | 4,986 instr, 1,877 cycles | 2.18x / 2.30x | 7,618 instr, 2,915 cycles | 1.53x / 1.51x |
| cap2 (pod) | 10,891 instr, 4,228 cycles | 4,992 instr, 1,890 cycles | 2.18x / 2.24x | 7,617 instr, 2,910 cycles | 1.53x / 1.57x |

**Code size** (Vita objects, Thumb). The lift's `f_00070110` is 22,068 bytes at -Os. The native's fast instance is
16,748 bytes at -O2. GEN (25.6 KB) and DRY (19.7 KB) run only for rare layouts and in verify mode.

## Expected on the Vita (estimate, to be measured)

The Vita's a30 frame (~74 ms) is bound by the scene helper (~66 ms). This estimate combines:
- The body's share of the helper: 8.9 % on the Pi (~5.9 ms of a 66 ms helper if the Vita's share is the same).
- The saved fraction of the body's cycles in the Pi A/B: 50-65 %, the final build's 1.83 Mcycles out of the body's
  sampled 3.6 Mcycles per frame.

That gives **about 3-4 ms per frame off the helper**, and off the frame while the helper stays the bottleneck.

- **Why it could be larger on the Vita.** The Cortex-A9 is in order and its instruction cache is 32 KB. The lift there
  is `-Os` with 627 helper calls (docs/compiler-probes-20260914.md), and the native's hot code is 16.7 KB against 22.1
  KB.
- **Why it could be smaller.** The HLE setters and the callees the native keeps are unchanged, and they are most of
  70110's inclusive time.

`XV_NATIVE_70110_TIME=1` (modes 0 and 2) or the A/B knob measures it on hardware.

## Vita integration (`overlap-candidate/build-x87`)

Files on branch `work/native-70110-20260924`, on top of `19c3d9e`:

1. **`recomp/kernel/xk_native_70110.c`** (new).
   - The Vita object is 68.9 KB of text: FAST 16.7 KB, GEN 25.6 KB and DRY 19.7 KB, with the verify, report and hook code
     in the rest. `.bss` is 88 bytes (the atomic counters and the mode word).
   - There is no emutls reference and no `vfma`. There are 8 TPIDRURW reads (`mrc p15, 0, rX, c13, c0, 2`: `X_PT` once per
     call path).
2. **`Makefile`**: the `XV_NATIVE_70110` block (flag, `_DEFAULT`, `-ffp-contract=off` for the unit).
3. **`games/halo_ce_3925/runtime.mk`**: the unit under `ifeq ($(XV_NATIVE_70110),1)`.
4. **`recomp/kernel/xd3d.c`**: one weak call, `xv_native_70110_report(60)`, after `xv_native_1721b0_report`.
5. **The shard that defines `f_00070110`** (`code_011.c` in this stage): the wrapper, the renamed body, and the tapped copy
   (`tools/patch_native_70110_hooks.py`).

Steps:

```sh
cd /home/birchwoodgod/xita-backups/2026-09-18-unified-games
python3 native-70110-wt/tools/install_native_70110.py overlap-candidate/build-x87   # items 1-5; idempotent
# add "XV_NATIVE_70110=1" to overlap-candidate/build-command.json (the list build_x87.py runs) and to make-vars.txt
python3 overlap-candidate/build_x87.py
```

Notes on the installer:
- It adds the Makefile block after the stage's `xk_native_1721b0.o` `-ffp-contract` rule, falling back to
  4B9D0's or 92330's. It adds the runtime.mk line after 1721B0's and the report call after 1721B0's.
- It prints `wrapper installed in front of f_00070110, tapped copy with 72 sites`.
- It was run twice on a hard-linked copy of `build-x87` (`native-70110-work/inst-test`). `build-x87` itself was left
  unchanged (link counts and contents checked). `make -n` of that copy lists exactly the units of the stage used for
  the runs above.

`code_011.c` (about 5 minutes at `-Os` on this machine), `xd3d.c` and the new unit rebuild by mtime.

Checks, done here with the exact Vita command lines from `make -n` of the installed copy:
- The unit, `xd3d.c` and `code_011.c` compile.
- `arm-vita-eabi-nm build/recomp/code_011.o | grep -E 'f_00070110|xv_native_70110'` shows:
  - `T f_00070110` (24 bytes)
  - `T f_00070110_body` (22,068 bytes, as before)
  - `T f_00070110_vbody` (24,596 bytes)
  - `U xv_native_70110`, `U xv_native_70110_sampler`, `U xv_native_70110_tap`
- `arm-vita-eabi-nm build/recomp/kernel/xk_native_70110.o | grep -c emutls` is 0.
- `arm-vita-eabi-objdump -d build/recomp/kernel/xk_native_70110.o | grep -c vfma` is 0.
- `arm-vita-eabi-nm build/recomp/kernel/xd3d.o | grep 70110` shows `w xv_native_70110_report`.
- The linked ELF should show `T xv_native_70110`, `T xv_native_70110_report` and `T xv_native_70110_tap`. That was not
  checked here: no ELF was linked.

The ELF grows by about 95 KB: the unit and the tapped copy, neither of which runs unless selected.

Runtime on hardware (`ux0:data/xita/env.txt` or the remote):
1. **Verify: `XV_NATIVE_70110=1`.** Expect `[native-70110] ... mismatched 0 ... (total mismatches 0)`. Verify is slow: it
   runs the tapped copy plus about 60 dry segments per call.
2. **Measure.** Either:
   - `XV_REC_AB=120 XV_REC_AB_KNOBS=XV_NATIVE_70110`, then `tools/rec_ab.py <log>` on the draw/HLE windows and the frame
     time; or
   - `XV_NATIVE_70110=0` against `=2` with `XV_NATIVE_70110_TIME=1`, which prints the whole call's us/call in each mode.
     The difference is the body's saving, since the callees are the same.
3. **Default:** `XV_NATIVE_70110_DEFAULT=2` in the make vars, once hardware agrees.

## Findings along the way

- **The first transliteration kept the x87 slots in `c->st` and re-translated the frame window after every callee.** It
  was only 1.02x faster than the lift on x86. Removing the per-callee remap brought it to 1.95x. The lift, built with
  `restrict c` at -O2, keeps much of its register file in host registers already (the lesson of docs/native-92330.md).
- **The Pi A/B measures more than the instruction count shows.** The first version saved 0.28 Minstr/frame but 1.50
  Mcycles/frame. The body's cost on the A72 is mostly instruction fetch and memory traffic, not instructions.
- **An earlier verify checker flagged `X_PREEMPT` on the owner** (3 in 4.2 M calls). `xv_preempt`'s yield sets
  `eip_hint` to `[esp]` (`xk_yield`). The native already did the same, since it calls `xv_preempt` with the state
  stored. The post-check now allows it.
- **A Makefile check broke.** The lift's marker comments (`XV_MODEL_*`, `XV_MATERIAL_SAMPLER_GROUP`) and
  `^void f_00070110` are counted by the Makefile's checks. The tapped copy renames its markers, and the wrapper's name is
  in parentheses.

## Not done / next

- Not built into a VPK, not run on the Vita or in Vita3K. There is no Vita timing.
- The draw `f_0007A960` (40-54 % of the subtree) and the texture binds `f_00080360` (x4, 5-7 %) are the next guest
  candidates. `f_00011BD0` (x4) and `11B60` are small leaf math functions shared with other callers.
- The HLE setter calls are now the largest part of the native's own cost (~40 calls). Batching them (for example the 21
  `SetTextureState_Deferred` words) would change `xd3d.c`, which another branch is editing, and is left out.
- `-Os` for the unit: not measured. The Vita's `XV_RENDER_GUEST_SIZE` builds the lift -Os. The native's fast instance is
  16.7 KB at -O2 against the lift's 22.1 KB at -Os.
