# Native lens-flare visibility test (`XV_NATIVE_63C00`)

Sept 24 2026. Branch `work/native-63c00-20260924`. Status: implemented and verified on the x86 host harness (25.4 M
in-game calls compared with the final code, 51 M across builds), the Raspberry Pi 4 ARM harness (0.8 M) and a
555,000-case differential test: 0 mismatches.
Default off. The unit, the hooked
`code_011.c` and `xd3d.c` compile with the Vita command lines; not yet linked into a VPK or measured on hardware.

## What `f_00063C00` is and where its time goes

`f_00060560` (the scene's flare pass) calls `f_00062240`, which pushes eax/ecx/edx and calls
`f_00063C00(point *, size, query id)`: one lens-flare **occlusion query**, ~182 calls per frame in the a10 cinematic
(10,920-10,980 per 60 frames on the Vita).

| part | what it does | cost |
|---|---|---|
| `f_000637A0` | size > `[1F0A68]`; `f_000B5EA0` transforms the point by the view matrix `0x2FC72C` (native `xv_math_point_transform`); four projection dot products (`0x2FC860..0x2FC89C`), 1/w, the viewport `0x2FC6F4`, depth clamp to `[1F0A78]`; writes screen x/y/z and the half extents | guest code |
| `f_00063C00` body | extents raised to 1.0; four edges clamped to `[[1F0D84], [1F0AE4]]` and floored by `f_00019E7B` (the CRT floor, under the control word at `0x1F2840`); fistp, int16 corners, area = (x1-x0)(y1-y0) | guest code (x87-heavy) |
| HLE block (area > 0) | `D3DDevice_BeginVisibilityTest`, `Begin(QUADLIST)`, 4x `SetVertexData4f(0, x, y, z, 1)`, `End`, `EndVisibilityTest(id)` | runtime: `End` records the quad through `draw_immediate_flare` |

Vita measurements (`overlap-candidate` logs, `[scene-phases]` inclusive, a10 cinematic):

| run | build | `63C00` ms/frame | calls/frame | us/call | flare-quad recording (`[flare-work]`) |
|---|---|---|---|---|---|
| perf172 | `XV_NATIVE_CRT_FLOAT` on | 6.17 | 182 | 33.9 | 1.89 ms/frame (124 kept + 64 culled quads) |
| perf177t | build-x87 (register splice) | 6.60 | 183 | 36.1 | 1.93 ms/frame |

So about 1.9 ms of the ~6.4 ms is the renderer recording the quads (`draw_immediate_flare`, inside `D3DDevice_End`).
The rest is the guest code plus the other HLE calls (`SetVertexData4f` copies 256 bytes per vertex; the
visibility begin/end bookkeeping). The Vita split between the two is **not measured yet**:
`XV_NATIVE_63C00_TIME=1` in mode 0 measures it (compute until the first HLE call, then the HLE block). On the x86
host (softgfx renderer, raster off) the guest code is 0.17-0.22 us of a 0.43-0.52 us call; on the Pi 4 1.3 us of
compute against 3.9 us of HLE block per drawn quad. Estimated for the Vita (below): roughly 2.3-3.6 ms/frame of guest
code, the other 3-4 ms HLE and renderer.

Note: the build-x87 VPKs (perf175 onwards) run the **lifted** CRT floor: their `build-command.json` lacks
`XV_NATIVE_CRT_FLOAT=1` (`make-vars.txt` has it; perf177t/178/180 logs have no `[crt-float]` line). The native handles
both configurations; its gain is larger with the lifted floor.

### Why this boundary

- The guest code is one function plus its private callees: `f_000637A0` is called only from `f_00063C00`, and
  `f_000B5EA0` and `f_00019E7B` are leaf math. No guest callbacks, no loops except the lifted floor's back-edge.
- The HLE calls stay: the native makes the same seven calls with the same guest stack, registers and x87 state at
  each, through the same `XV_HLE_CALL` bookkeeping (`xv_cur_fn`, `XV_HLE_TIMING`, object-job dispatch). The renderer
  sees an identical call stream, so nothing on the GPU side changes and nothing needs hardware to validate.
- The renderer part (`draw_immediate_flare`, ~10 us per kept quad on the Vita) is the larger lever but is runtime
  code, not guest code: see "Not done / next".

## Semantics reproduced (exact)

`recomp/kernel/xk_native_63c00.c`:

- Guest memory addressed as the shards address it: the thread's page table and arena base cached per function (the
  shard preamble's `xram_`/`xpt_`/`imgb_`), every access translated through the table when it happens; split-aware
  reads where the lift uses `x87_load_f32`; `X_IMG*` rules of the build (render view: through the table) for the
  viewport words and the lifted floor's control word; `X_M32` for the CRT native's.
- Each input read once (the lift re-reads the image constants `[1F0A68]`, `[1F0A78]`, `[1F0D84]`, `[1F0AE4]`,
  `[1F2840]`, which no one writes). Values the guest stores to its own frame and reloads (fstp/fld dword, fistp/movsx,
  the pushed size) are carried in registers with the same rounding.
- Float math: the guest's operand order on doubles (dot products `st*mem`, `faddp` earlier+later, `fdivr`, `fsubr`),
  float rounding at every dword round trip, compares with `x87_compare`'s condition codes; the unit is built
  `-ffp-contract=off` (on the Vita GCC still emits VFPv3 `vmla`/`vmls`/`vnmls`, which are chained, not fused: the
  product is rounded before the add; there is no `vfma` in the object).
- `fsw`: every compare ORs its TOP into the status word, so the result is the entry value with C3..C0 cleared, TOP
  bits for st(0) (and st(3) for 637A0's depth clamp), and the condition codes of the **last** compare the guest
  executes (the lifted floor's `fcomp` when that runs).
- The four floors as the stage runs `f_00019E7B`: `XV_NATIVE_CRT_FLOAT` built and on -> `xv_native_crt_float(c, 2)`
  (rounding under `[1F2840]`, no frame); otherwise the lifted CRT floor, whose final frame (`_controlfp` twice,
  `_frnd`: 10 dwords at its entry esp) is written. The `fistp` after each floor converts the float of an integral
  value, so its rounding is the identity.
- The stack: the final value of every dword the guest writes (its frame, 637A0's frame and saved registers, B5EA0's
  output, the lifted floor's frame, the HLE arguments), and before each HLE call exactly the stack above esp, the
  registers (eax..edi) and st(0)/fsp/fsw/fcw the guest has there. Intermediate values nothing reads are skipped.
- Exit state: eax/ecx/edx per path (e.g. depth-fail leaves ecx = `0x2FC72C`, edx = the matrix scale word; the drawn
  quad leaves ecx = y1 as float, edx = the query id), esp += 16 (`ret 0Ch`), the lazy flags of the last `test`
  (`test al,al` on the projection-fail paths, `test esi,esi` after the imul) including the imul's carry/overflow
  cells, the dead x87 slots of 637A0 and B5EA0 (st(0)..st(4) of the x87-regs body), fsp unchanged, fcw unchanged.
- The back-edge budget: 63C00/637A0/B5EA0 have none; the lifted floor's inexact path jumps back (`19F26 jne 19F15`,
  one `X_PREEMPT`). The native charges `c->preempt` exactly and declines a call whose back-edges would reach
  `xv_preempt()` (slice 20000), so every scheduling point stays where the guest has it.
- Kept from the stage: `XV_OBJECT_MATH_GUARD` around the matrix/point reads (as `xv_math_point_transform` takes
  it), the phase scope of 637A0 (id 18). Not kept: the `[native-point]` / `[crt-float]` counters for these calls.

Declined (the guest body runs; nothing was written; counted by reason in the report): esp not 4-aligned (a slot
straddling a page is written with one translation and read back split by the lift - Halo's esp is always aligned),
a point overlapping the stack window, a non-finite matrix/point/projection/size input (which NaN payload an operation
propagates is the host compiler's operand order), a non-finite floor argument or an inexact floor with the
precision exception unmasked (the CRT's matherr/`_except` paths), the lifted floor reaching `xv_preempt()`.
In-game: 0 declines in every run.

Assumed, and checked by verify mode: the five D3D HLE functions read only their stack arguments and change only
eax and esp (true: they write no guest memory).

## Knobs and counters

- Build: `XV_NATIVE_63C00=1` (Makefile block; `games/halo_ce_3925/runtime.mk` adds the unit; `-ffp-contract=off` for
  the unit). `XV_NATIVE_63C00_DEFAULT` (0).
- Install into a stage: `python3 tools/install_native_63c00.py <stage>` (copies the unit, adds the Makefile and
  runtime.mk blocks, the `xd3d.c` tap and report call, and the entry hook through
  `tools/patch_native_63c00_hooks.py`; idempotent).
- Env `XV_NATIVE_63C00`: 0 off (default), 1 verify, 2 native.
  Verify: the native runs **dry** (the HLE calls are recorded with their full view, not made), then the state is
  restored and the guest body runs with every D3D HLE entry observed (`xv_hle_tap` in `XD3D_COUNT`); registers,
  lazy flags (including the carry/overflow cells), x87 slots/fsp/fsw/fcw, the preempt budget, the stack window
  `[E-0x80, E+0x10)` and each HLE call (kind, eax..edi, arguments, x87 state) are compared. The guest's result and
  its HLE calls stand. Inputs are re-read after the guest run; a change (another thread) counts as "raced", not a
  verdict.
- Env `XV_NATIVE_63C00_TIME=1`: us/call and the split: compute until the first HLE call, and the HLE block per
  drawn call; the guest body in mode 0 (where the path counters stay 0: the guest runs unclassified, and the tap
  that timestamps its first HLE entry is installed for every D3D call), the native in 2, both in 1 (the native's
  dry run has no HLE block). "skipped": a verify/timing reference run was already in progress on another context.
- 60-frame line from xd3d.c's report block:
  `[native-63c00] 60 frames: calls N (projection-fail N, empty N, drawn N) declined N (layout N, inputs N, floor N,
  preempt N); verified N mismatched N raced N skipped N (total mismatches N); native us/call X (compute X, hle X/draw);
  guest us/call Y (compute Y, hle Y/draw)`. Mismatches print up to 16 detail lines (`MISMATCH <field> native .. guest ..`).

## Verification

Stage: a copy of `overlap-candidate/build-x87` with `tools/install_native_63c00.py` applied (the installer's result is
byte-identical to the hand integration). Scene on the helper thread under the overlap (`XV_SCENE_THREAD=1
XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1 XV_RENDER_VIEW_THREAD=1 XV_RENDER_VIEW_ALL=4 XV_RENDER_VIEW_SPLIT=1
XV_RENDER_VIEW_EARLY=1`) unless noted.

**Differential test** (`tools/test_native_63c00.py <stage>/recomp [cases] [--seed N]`): every case runs the guest body,
the native and verify mode from identical state and compares the whole arena, every xctx field (no exceptions:
the carry/overflow cells and all eight x87 slots bit for bit), the xv_preempt calls, and every HLE call (kind,
eax..edi, arguments, x87 state and flags at the call). Scenes: camera-like (view matrix, perspective rows, 640x480),
random, special values (NaN, inf, huge, signed zero anywhere), exact ties (edges equal to the clamp bounds, extents
and depth equal to `[1F0A78]`, size or depth equal to `[1F0A68]`); stack pages mapped in reverse, windows across a
page boundary, misaligned esp; points unaligned, across pages, in the stack window, inside the matrix.

| stage bodies | builds | cases | mismatches |
|---|---|---|---|
| build-x87 (x87 register form) | CRT floor native (env on / env off), lifted, per-thread table + render view, object-job guard + light census, -O0 lifted; 3 seeds | 360,000 | 0 |
| overlap-candidate/build (memory lowering) | the same six; 2 seeds | 180,000 | 0 |
| build-x87, Raspberry Pi 4 (armhf -O2) | native CRT, lifted, thread table | 15,000 | 0 |

Branches reached per 20,000 native runs (one build): size fail 7,602, depth fail 7,564, depth clamp 8,360, extent raised
6,970 / 6,828, edge below / above the clamp 21,844 / 13,488, inexact floors 47,184, fistp out of range 40, int16 wrap
1,142, imul overflow 20, negative area 130, empty 9,280, drawn 8,888, matrix scale != 1 7,752; declines 6,536.

Mutants (`--mutants`, 5,000 cases each): 20/20 caught - summation order of the depth row (12 cases), no float rounding
of the stored depth (1,354), extent raise `>=` (6), size test `>=` (281), depth test `>=` (85), clamp `<=` (19),
TOP of the depth-clamp compare (1,076), condition codes of an earlier compare (996), floor under the caller's control
word (1,230), a lifted-floor frame word missing (1,995), B5EA0's return address missing (2,227), a dead x87 slot missing
(2,227), second vertex x (1,078), an HLE-view word skipped (1,078), imul carry/overflow cells (1,687), exit edx on the
depth-fail path (1,030), st(0) at the first SetVertexData4f (912), no decline for a point in the stack window (35),
for a misaligned esp (10), lifted-floor back-edge budget (1,567). Equivalent, not listed: `one >= Z` in the depth clamp
and `v >= hi` in the clamp (equality stores the same value; the condition codes are overwritten).

**In-game verify mode** (`XV_NATIVE_63C00=1 XV_NATIVE_63C00_TIME=1`; the guest result is kept):

| run | where | frames | calls compared | drawn quads | mismatches | declined | raced |
|---|---|---|---|---|---|---|---|
| ver4 | x86 host, copy of the Vita checkpoint save (a10), scripted look/move/fire, overlap | 26,640 | 920,362 | 352,783 | 0 | 0 | 0 |
| ver-ng4 | x86 host, new game -> a10 cinematic -> play, overlap | 26,760 | 9,610,790 | 6,360,729 | 0 | 0 | 0 |
| ver3 | x86 host, checkpoint save, overlap (*) | 35,640 | 1,310,217 | 581,002 | 0 | 0 | 0 |
| ver-ng3 | x86 host, new game, overlap (*) | 35,760 | 12,705,779 | 8,493,746 | 0 | 0 | 0 |
| ver-owner | x86 host, checkpoint save, scene on the owner (`XV_SCENE_THREAD=0`) (*) | 26,640 | 849,816 | 463,186 | 0 | 0 | 0 |
| ver-arm | Pi 4 ARM (cores shared, nice 15), checkpoint save with movement, overlap, 30 min (*) | 52,320 | 790,514 | 173,147 | 0 | 0 | 0 |

(*) the same native with verify mode still inline in the hook function (moved into its own function afterwards so
modes 0/2 do not carry its 4 KB frame; no logic change). Total: 26.2 M in-game calls, 16.4 M drawn quads, 0
mismatches, 0 declines, 0 raced. Earlier builds added 25.6 M more (13.4 M with the final algorithm and the
x87r_compare-based condition codes, 12.2 M with a first, literal transcription), also 0.

## Cost, guest vs native

Microbenchmark (`--bench`: drawn camera quads the native handles, both sides including the eight HLE entries into
cheap stand-ins, 2,000 repetitions per scene; instructions from the CPU's retired-instruction counter):

| where | build | guest | native | ratio (instructions) |
|---|---|---|---|---|
| x86 host, -O2 | CRT floor native | 286 ns, 3,626 insns | 102 ns, 1,916 insns | 2.8x (1.9x) |
| x86 host, -O2 | lifted CRT floor (what the build-x87 VPKs run) | 443 ns, 5,808 | 100 ns, 1,894 | 4.4x (3.1x) |
| x86 host, -O2 | per-thread table + render view | 304 ns, 3,644 | 102 ns, 1,906 | 3.0x (1.9x) |
| Pi 4 A72, armhf -O2 | CRT floor native | 1,767 ns, 4,056 | 781 ns, 2,512 | 2.3x (1.6x) |
| Pi 4 A72, armhf -O2 | lifted CRT floor | 2,607 ns, 6,182 | 791 ns, 2,530 | 3.3x (2.4x) |
| Pi 4 A72, armhf -O2 | per-thread table | 1,962 ns, 4,166 | 789 ns, 2,507 | 2.5x (1.7x) |

In the game (`XV_NATIVE_63C00_TIME=1`, us per call, the host harness is built -O1):

| where | method | guest: total (compute / HLE share) | native: total (compute / HLE share) | compute ratio |
|---|---|---|---|---|
| x86, checkpoint save | separate runs, mode 0 vs 2, same pad script | 0.518 (0.221 / 0.297) | 0.361 (0.114 / 0.247) | 1.9x |
| x86, a10 cinematic | same | 0.428 (0.170 / 0.258) | 0.301 (0.087 / 0.214) | 2.0x |
| Pi 4, checkpoint save | verify mode (native dry, then the guest) | compute 1.32, HLE block 3.94 per drawn call | 0.86 incl. recording the eight HLE views | - |

The guest's HLE share includes the tap's cost in timing mode 0. What the native removes is the guest code: the lazy
flags, the x87 bookkeeping, the register file in memory, three function calls and the point/floor natives' checks,
~110 re-reads and intermediate stack writes. What it keeps is the minimum a bit-exact port needs on this runtime: ~40
input reads, the final stack words and the HLE views (each access translated through the thread's table), the
rounding calls, and the HLE calls themselves.

Expected on the Vita (to be measured): the Cortex-A9 is in order and the recompiled x87 code is dependency-bound
memory traffic, so the ratio should be at least the A72's (2.3x, and 3.3x with the lifted CRT floor the build-x87
VPKs run). With the guest code at roughly 2.3-3.6 ms of the 6.6 ms/frame (A72 guest cost scaled 5-8x to the A9; the
rest is HLE, 1.9 ms of it the renderer's flare recording), the native saves roughly 1.5-2.5 ms per frame on the scene
helper. `XV_NATIVE_63C00=0 XV_NATIVE_63C00_TIME=1` on hardware gives the real split.

## Vita integration (overlap-candidate/build-x87)

Files (branch `work/native-63c00-20260924`, on top of bfb1871):

1. `recomp/kernel/xk_native_63c00.c` (new; Vita object 13.3 KB text, 148 B BSS; the hook's frame is 280 bytes,
   verify mode's buffers (~4 KB) are on its own function's frame).
2. `Makefile`: the `XV_NATIVE_63C00` block after the `XV_NATIVE_VISIBILITY` block's `-ffp-contract=off` line.
3. `games/halo_ce_3925/runtime.mk`: the unit under `ifeq ($(XV_NATIVE_63C00),1)`.
4. `recomp/kernel/xd3d.c`: `XD3D_TAP` in `XD3D_COUNT` (with the flag only: one pointer test per D3D HLE call) and
   the weak `xv_native_63c00_report(60)` call after `xv_native_visibility_report`.
5. The entry hook in `f_00063C00` (`code_011.c` of build-x87).

Steps:

```
cd /home/birchwoodgod/xita-backups/2026-09-18-unified-games
python3 native-63c00-wt/tools/install_native_63c00.py overlap-candidate/build-x87     # items 1-5, idempotent
# add "XV_NATIVE_63C00=1" to overlap-candidate/build-command.json (the list build_x87.py runs) and make-vars.txt
python3 overlap-candidate/build_x87.py
~/vitasdk/bin/arm-vita-eabi-nm overlap-candidate/build-x87/build/recomp/code_011.o | grep xv_native_63c00   # U
~/vitasdk/bin/arm-vita-eabi-nm overlap-candidate/build-x87/build/xita.elf | grep -E 'xv_native_63c00$|xv_hle_tap'   # T, B
```

`code_011.c`, `xd3d.c` and the new unit are rebuilt by mtime; no other unit depends on the flag. Checked here with the
exact Vita command lines from `make -n` (arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon): the unit (no
`vfma`), the hooked `code_011.c` (`U xv_native_63c00`) and `xd3d.c` (`B xv_hle_tap`) compile.

Runtime on hardware:

1. `XV_NATIVE_63C00=1 XV_NATIVE_63C00_TIME=1`: verify; expect `mismatched 0`, `declined 0`. The frame pays the dry
   native, the guest, the tap and three clock reads per call.
2. `XV_NATIVE_63C00=0 XV_NATIVE_63C00_TIME=1` and `=2 XV_NATIVE_63C00_TIME=1`: the guest and native us/call with the
   compute / HLE-block split (the clock is `sceKernelGetProcessTimeWide`, microseconds: read the averages). Or without
   TIME and with `XV_SCENE_PHASES=1`: `63C00` inclusive should drop by the compute part.
3. `XV_NATIVE_63C00_DEFAULT=2` in the make variables makes the native the default once hardware agrees.

Separately (not part of this change): the build-x87 VPKs run without `XV_NATIVE_CRT_FLOAT` (missing from
`build-command.json`), so every CRT floor in the game is the lifted one again (perf172 had the native: `63C00` 6.17
vs 6.60 ms). Adding `XV_NATIVE_CRT_FLOAT=1` is independent of this native; it handles both.

## Not done / next

- The HLE block is now most of the cost: `D3DDevice_End` -> `draw_immediate_flare` records a full draw per quad
  (vertex shader bind, tracked constants, `sync_draw_state`, a 4-vertex immediate draw: ~10 us per kept quad, 1.9
  ms/frame), and `SetVertexData4f` snapshots all 16 attribute registers (256 bytes) per vertex. A flare fast path
  that skips the shader/constant/state sync when nothing changed since the previous flare quad, and copies only the
  attributes the flare declaration reads, is the next lever for this call site. It is renderer code: it needs
  hardware checks of the image and the query results, not a guest-exactness argument.
- Not taken: caching a stack page's translation within a burst of writes (the render view can rewrite a thread's
  table entries from another thread, `xv_render_view_mirror`), merging the compute and commit passes (a few percent).
