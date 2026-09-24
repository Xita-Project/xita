# Native aim/look overlay blend (`XV_NATIVE_AIM_BLEND`)

Sept 24 2026. Branch `work/native-tick2-20260924`. Status: implemented and verified: 2.34 M in-game calls compared on
the x86 host harness and the Raspberry Pi ARM harness (0 mismatches, 0 declines), 240,000-case differential test
(4 seeds x 20,000 cases x 3 builds, 0 mismatches), 18/18 mutants caught. ~4.3x faster than the guest on the Pi.
Default off. The units compile for the Vita; not yet linked into a VPK or measured on hardware.

## What was replaced, and why this boundary

The Vita's a10 tick profile (perf177t, `[tick-phases]`) has two ~5 ms/frame guest functions in the game tick:
`f_00090770` (5.4 ms, 2191 calls / 60 frames, under the per-object update `f_0008DDF0`, loop `f_0008FB70`) and
`f_0014B230` (5.2-7.3 ms under the per-actor AI update `f_0014E1A0`). Both are dispatchers whose own bodies are a few
dozen instructions; the cost is in their callees. Host profiles (phase timers wrapped around every direct and
indirect call of the candidates, a10 checkpoint gameplay, frames >= 700):

| function | role | share |
|---|---|---|
| `f_00090770` | object-type postprocess dispatch: for every type in the object's type chain, call the type's node-matrix postprocess (+0x44 of the type definition) | 100 % |
| `f_0003ECC0` | unit postprocess: aiming and looking. Two calls of `f_000A39B0` (aim yaw/pitch, look yaw/pitch), each after `f_0008B970` + `f_000B5CA0` (angles) | 90.5 % of `90770` |
| **`f_000A39B0`** | **2-D overlay blend: bilinear blend of the four keyframes around (yaw, pitch) in the aim animation's frame grid, for every node** | **95.1 % of `3ECC0`** |
| `f_000A2C30` | int16 x4 -> float quaternion (x 1/32767) | 4 per rotated node |
| `f_000B0E70` | quaternion lerp, shortest arc (negate when the dot product is < 0) | 3 per rotated node |
| `f_000B4320` | quaternion normalize ((0,0,0,1) when |q|^2 <= 0) | 3 per rotated node |
| `f_000B0EF0` | quaternion product into the node | 1 per rotated node |
| `f_000A5080` | keyframe pointer (base + stride x frame) | 4 per call |
| `f_0001D150` | MSVC `_ftol`-style truncation (fistp + correction) | 2 per call |
| `f_00180ADA` -> `f_000220FF` -> `f_00180AE4` / `f_0002219C`, `f_00180C8A` | MSVC `_CIfmod` (classify with fxam, dispatch through the CRT table, fprem or the zero handler, error epilogue) | 2 per call |
| `f_001180C0`, `f_00036500` (other type postprocess), `f_000A4950/A4720/A4B40/A43D0` | the rest of `90770`'s subtree | ~10 % together |

`f_000A39B0` is the smallest subtree that holds the cost: ~86 % of `f_00090770`, no HLE, no guest callbacks (the only
indirect jump is the CRT's fmod dispatch, a constant table), writes only the node array and its own stack. It is
called from `f_0003ECC0` (2 sites) and `f_00036500`; one entry hook covers both.

`f_0014B230` (actor state update) was measured too and left alone: its time is concentrated in rare heavy calls
(`f_00160B80` under the state handler `f_00169900`, ~150 us/call once every ~5 frames; `f_00166500` under the actor-type
update `f_00180190`, ~180 us/call) with wide call trees (firing positions, path and collision queries, LCG randomness);
the sibling `f_0015CE90` spends 97 % in `f_0015C890`, a 10-try random direction probe whose cost is the collision ray
test `f_001721B0` (1455 lines, into the collision system). None has a small exact boundary.

## Semantics reproduced (exact)

`recomp/kernel/xk_native_aim_blend.c`:

- **The guest stack.** The native runs on a host shadow of `[esp-0x3D8, esp+0x10)` (read once) and performs every
  stack store the guest performs, in order: `f_000A39B0`'s 0xE8 bytes of locals and its pushes, every callee's return
  address, pushes and locals (the 16-byte aligned `f_0001D150` frame, the 0x2D0-byte `f_00180ADA` frame with its
  control word, fxam status words, dispatch record, saved doubles and the `or byte [ebp-2C8h],3` read-modify-write,
  `f_000B0EF0`'s node copy, ...). The window `[esp-0x3D8, esp)` is written back at the end: every dead stack byte ends
  as the guest leaves it, bytes the guest does not write keep their values. Stack reads (arguments, locals, callee
  reloads such as `fild qword` of the `fistp` result) come from the shadow.
- **x87.** The eight slots are depth-indexed locals (`X[d]` = slot `(top - d) & 7`) updated exactly like the emulator's
  `st[]`: pushes, the CRT's `fxch` swaps, `fstp st(n)`, dead slots included; all eight are written back. `fsw` from every
  compare (C3/C2/C0 and the TOP field the emulator stores), fxam (C3/C2/C1/C0, TOP untouched) and fprem (C2 cleared);
  `fcw` set to 0x133F inside the fmod and restored from the saved word; float math in doubles with the guest's operand
  order and association, loads widened from float, stores rounded to float, `fistp` rounded under `fcw`
  (`x87_round` semantics) with the `x87_store_i64` range rule, `fmod` from libm like the guest. The unit is compiled
  `-ffp-contract=off -fno-math-errno`: no fused multiply-add in the Vita object (the VFPv3 `vmla.f64` GCC still uses is
  the non-fused, separately rounded form, the same the guest shards get), and `sqrt` inlines to `vsqrt.f64` instead of
  the libm wrapper - vitasdk's `__ieee754_sqrt` is that same instruction, the wrapper only adds errno for negative
  input, and the blend only takes the square root of a positive ordered sum. The top of stack is written back masked
  to 0..7, as the guest's pushes and pops leave it.
- **Registers and flags.** eax/ecx/edx: the guest's last assignments (including partial ones: `fnstsw ax`, `mov al,..`,
  A2C30's `pop ecx` of the last component). ebx/ebp/esi popped from the (shadow) stack, esp += 0x10 (`ret 0Ch`), edi
  unchanged. The lazy-flag record of the last flag-setting instruction and the carry/overflow cells of the last
  instruction that wrote them (shr, inc, imul, adc/sbb in the ftol, the CRT's 8-bit shifts/rotates; `sahf` in the
  fprem path): every `xctx` field matches, the stale `f_cf`/`f_of` cells included.
- **Memory.** Guest addresses translate like the shards (the thread's page table, `X_IMG` rules of the build: image
  globals through the page table under `XV_RENDER_VIEW`). Integer loads are single-translation like `X_M16`/`X_M32`,
  float loads/stores page-split like `x87_load_f32`/`x87_store_f32`. Keyframe data and nodes are read and written at the
  same point in the sequence as the guest, so a node array overlapping its own keyframe data behaves the same.
- **Budget.** `c->preempt` drops by exactly the guest's back-edge count (the loop's `jl`, the CRT's `jmp` when
  `[1F2EB0] == 0`, `f_0001D150`'s rare retry), and `xv_preempt` is called the same number of times, at the end of the
  call instead of mid-function (a scheduling point only; the visibility native does the same).

**Declined** (the hook returns 0 before touching anything, the guest body runs): compressed animations (the
`f_000A3750`/`f_000A2EE0` path), the two early exits, `fcw` with the precision exception unmasked, fmod operands the
CRT dispatches elsewhere (NaN/inf), object-job lanes, an unaligned esp, an odd animation header or an animation
entry / node array / keyframe pointer that is not 4-aligned, and any overlap of the node array with the stack window or
the data the loop re-reads (animation entry fields, the 0.0/1.0/(1/32767) constants), or of the keyframe data with the
stack window. The alignment rule exists because a single-translation load that straddles a page reads the
host-adjacent page, which could be a stack page the native holds in its shadow (the differential test found this
with keyframe data 2-aligned against reversed stack pages). In-game every call is accepted (0 declines in 1.5 M
calls).

**Not reproduced:** NaN payload bits of an operation with two NaN operands: which operand's payload propagates is
the compiler's operand order for a commutative add/multiply, not rounding (in the test the native built -O2 picks the
other payload than the same source built -O0 in two such cases; the visibility native saw the guest body itself
disagree between -O0 and -O2). Animation data has no NaNs (0 mismatches in 2.34 M in-game calls); the test compares
both-NaN dead x87 slots, and both-NaN floats in its NaN / garbage-data flavors, as equal.

## Knobs and counters

- Build: `XV_NATIVE_AIM_BLEND=1` (Makefile; adds the source via `games/halo_ce_3925/runtime.mk`,
  `-DXV_NATIVE_AIM_BLEND=1`, `-ffp-contract=off -fno-math-errno` for the unit). `XV_NATIVE_AIM_BLEND_DEFAULT` (0).
- Hook: `python3 tools/patch_native_aim_blend_hooks.py <stage>/recomp` (idempotent; one hook at the entry of
  `f_000A39B0` in `code_015.c`).
- Env `XV_NATIVE_AIM_BLEND`: 0 off (default), 1 verify (native, then restore the stack window and the node array and run
  the guest body on the same state with an unbounded budget, compare everything, keep the guest's result), 2 native.
- Env `XV_NATIVE_AIM_BLEND_TIME=1`: per-call microseconds (`xk_os_monotonic_us`); with mode 0 it times the guest body,
  with 2 the native, with 1 both (native first, then the guest warm).
- 60-frame line from xd3d.c's report block:
  `[native-aim-blend] 60 frames: calls N native N verified N mismatched N (total mismatches N); nodes N rotated N
  translated N; declined compressed N exit N fpu N alias N align N other N; us/call native X guest Y`. Mismatches print
  up to 12 detail lines (`MISMATCH <field> native .. guest ..`).

## Verification

Stage: a copy of `overlap-candidate/build` (Sept 23 evening shards) plus the hook; scene on the helper thread
(`XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1 XV_RENDER_VIEW_THREAD=1 XV_RENDER_VIEW_ALL=4
XV_RENDER_VIEW_SPLIT=1 XV_RENDER_VIEW_EARLY=1`), a copy of the Vita checkpoint save (Continue: mid-a10 gameplay) with
scripted look/move/fire input (the x87-regs pad script).

**Differential test** (`tools/test_native_aim_blend.py <stage>/recomp [cases] [--seed N] [--mutants]`): extracts the
guest bodies of `f_000A39B0` and its 11 callees from the stage and builds `tools/tests/native_aim_blend.c` three ways
(plain page table -O2, per-thread table + render view -O2, -O0). Randomized calls in a synthetic arena: shuffled tag
pages, reversed stack pages, random dead stack, random x87 slots/top/status and lazy flags, five control words
(rounding modes, PM unmasked), both CRT paths (`[1F2EB0]`, `[270678]`, the `_trandisp2` control-word branch), zero /
-0.0 / grid-edge / far-outside / tiny yaw and pitch, zero and negative ranges, 0..130 nodes with sparse to dense masks,
axis-aligned quaternions (exact zero dots), opposite-sign keyframes, zero-norm blends, node arrays inside their own
keyframe data, keyframe data on or just above the stack window, a keyframe dword straddling into a stack-window page,
2^60 node quaternions whose product terms cancel (the association decides the result), NaN/inf data, unaligned
bases, a small back-edge slice. Each case: the native runs first; a decline must leave the arena and the context
untouched; otherwise the guest body runs from the same state and the whole 16 MiB arena, every `xctx` field and the
`xv_preempt` call count are compared.

The native is compiled with its production flags (`-ffp-contract=off -fno-math-errno`), the guest bodies like the
stage.

| run | cases | compared (native ran) | declined | mismatches |
|---|---|---|---|---|
| 4 seeds x 20,000 x 3 builds | 240,000 | 137,430 (45,810 per build; 1.54 M nodes per build) | 102,570 (0 changed state) | 0 |

(An earlier generator counted only four mask words, so past 128 nodes keyframes overlapped and int16 pairs were read
as NaN translations; two -O2 cases then differed in NaN payloads only. The generator now counts the words the loop
reads.)

Mutants (`--mutants`, 3000 cases each; every one must be caught): 18/18 caught - (1-t) rounded to float, negation on
a zero dot, reciprocal rounded to float, quaternion product reassociated, translation rows swapped, fistp rounding
mode ignored, zero fmod operand through fprem (sign of -0.0), two dead-stack stores dropped (`[ebp-2D0h]`,
`[ebp-94h]`), a CRT `fxch` not modelled, a dead x87 slot of the translation, the TOP field of a compare, the carry
cell of `shr`, eax after the translation, the loop back-edge count, a clamp `>=`/`>`, keyframe/node read order,
unaligned keyframe pointers accepted. Two early mutants were equivalent (a push always overwritten, an inc that
re-copies the carry) and were replaced; the dot-product reassociation is invisible for this data (the first two
lerps' dot products are exact sums of int16/32767 products) and is covered by construction.

**In-game verify mode** (`XV_NATIVE_AIM_BLEND=1`, every call compared: registers, flags incl. the stale cells, x87 slots
/ fsp / fsw / fcw, back-edge count, the 1000-byte stack window, the whole node array; the guest result is kept):

| run | build | where | frames | calls compared | nodes | rotated / translated | mismatches | declined |
|---|---|---|---|---|---|---|---|---|
| ab-ver1 | 0086281 | x86 host, Vita checkpoint save, movement input, 15 min | 26,640 | 477,817 | 9.27 M | 4.04 M / 0.28 M | 0 | 0 |
| pi-ab-ver1 | 0086281 | Pi 4 (ARM, cores 2-3), same save and input, 20 min | 35,040 | 1,033,578 | 19.52 M | 8.53 M / 0.75 M | 0 | 0 |
| ab-ver3 | final | x86 host, same, 11.7 min | 20,640 | 367,927 | 7.15 M | 3.10 M / 0.22 M | 0 | 0 |
| pi-ab-ver2 | final | Pi 4, same, 15 min | 26,220 | 459,888 | 8.85 M | 3.86 M / 0.26 M | 0 | 0 |

Total: 2,339,210 in-game calls (845,744 x86, 1,493,466 ARM), 44.8 M nodes, 0 mismatches, 0 declines. The final build
differs from 0086281 by the inline vsqrt, the always-inline accessors, the masked x87 top and one extra decline
condition that never fires in-game.

## Cost, guest vs native (host-side; not Vita numbers)

`XV_NATIVE_AIM_BLEND_TIME=1`, microseconds per call, call-weighted over the 60-frame windows from frame 1500 (the
checkpoint's gameplay; ~19 nodes, ~8.3 rotated, ~0.6 translated per call):

| where | method | guest | native | ratio |
|---|---|---|---|---|
| Pi 4 ARM (-O1, armv7 NEON) | separate runs, mode 0 vs mode 2 (final), same save and pad script, 266/267 windows | 31.34 | 7.29 | 4.3x |
| Pi 4 ARM | verify mode, final build, 412 windows (native cold, guest second and warm) | 35.50 | 8.15 | 4.4x |
| Pi 4 ARM | verify mode, 0086281, 559 windows | 33.38 | 7.83 | 4.3x |
| Pi 4 micro-benchmark (-O2) | one typical call repeated (19 nodes, 8 rotated, 1 translated), hot caches | 15.50 | 3.96 | 3.9x |
| Pi 4 micro-benchmark | same, 512 scenarios over 48 MB (cold) | 17.26 | 5.76 | 3.0x |
| Pi 4 micro-benchmark | per rotated node / fixed per-call setup (0 nodes) | 1.87 / 1.13 | 0.39 / 0.74 | 4.8x / 1.5x |
| x86 host (-O1 build) | separate runs, mode 0 vs mode 2, final build, 319 windows each | 5.56 | 2.02 | 2.8x |
| x86 host | separate runs, 0086281, 269 windows each | 4.48 | 1.82 | 2.5x |
| x86 host | verify mode, final build | 4.04 | 1.97 | 2.1x |
| x86 micro-benchmark (-O2) | hot / cold | 2.27 / 2.52 | 0.52 / 0.57 | 4.4x |

On the out-of-order x86 host both paths are dominated by the same cache misses (keyframe streams, the node array);
the Pi shows the emulation overhead the native removes. The native is latency-bound: per rotated node three
lerp -> sum of squares -> sqrt -> divide chains in exactly the guest's order (~590 cycles on the A72). Tried and
dropped: prefetching the four keyframe streams and the node array (no gain on x86 or the Pi, hot or cold).

Expected on the Vita: the phase timers put `f_00090770` at ~5.4 ms/frame in the a10 cinematic (perf177t), ~86 % of
it `f_000A39B0` (~4.7 ms); at the Pi's ~4.3x the blend would cost ~1.1 ms, i.e. roughly -3.5 ms of tick time per
frame (the in-order Cortex-A9 pays the emulation overhead more than the A72, so the ratio may be higher). To be
measured.

## Vita integration (overlap-candidate stage)

Files (branch `work/native-tick2-20260924`):

1. `recomp/kernel/xk_native_aim_blend.c` (new; Vita object 12.6 KB text, 8 B data, 108 B BSS; the verify-mode node
   buffers are malloc'ed on the first verify call only; the native uses ~1 KB of host stack, ~4 KB in verify mode, on
   512 KB fiber stacks).
2. `Makefile`: the `XV_NATIVE_AIM_BLEND` block after the `XV_NATIVE_VISIBILITY` block (flag + `_DEFAULT` + the
   `-ffp-contract=off -fno-math-errno` rule for `$(RECOMP_BUILD)/kernel/xk_native_aim_blend.o`). The stage Makefile's block order
   differs from the source; paste the block right after its `xk_native_visibility.o: RECOMP_CFLAGS += -ffp-contract=off`
   line.
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_aim_blend.c` under
   `ifeq ($(XV_NATIVE_AIM_BLEND),1)` (after the visibility entry).
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_aim_blend_report(60)` next to `xv_native_visibility_report`.
5. Shards: `python3 tools/patch_native_aim_blend_hooks.py <stage>/build/recomp` (hooks `f_000A39B0` in `code_015.c`;
   idempotent).

   Steps 1-5 in one command: `python3 tools/install_native_aim_blend.py <stage>/build` (idempotent; applied to a fresh
   copy of `overlap-candidate/build` it reproduces the verified stage byte for byte).
6. Build with `XV_NATIVE_AIM_BLEND=1` added to `make-vars.txt`. `code_015.c`, `xd3d.c` and the new unit are rebuilt by
   mtime; no other unit depends on the flag. Check: `arm-vita-eabi-nm build/recomp/code_015.o | grep
   xv_native_aim_blend` (U) and the ELF has `T xv_native_aim_blend`.
7. Runtime: `XV_NATIVE_AIM_BLEND=1 XV_NATIVE_AIM_BLEND_TIME=1` once (verify on hardware: expect `mismatched 0`, all
   `declined` counters 0; the frame pays guest + native + ~2 KB of copies per call), then `XV_NATIVE_AIM_BLEND=2`
   against `0`, both with `XV_NATIVE_AIM_BLEND_TIME=1` (the us/call pair), or with `XV_SCENE_PHASES=1` (the stage's
   `[tick-phases]` `90770` should drop from ~5.4 ms). `XV_NATIVE_AIM_BLEND_DEFAULT=2` in the make vars makes native
   the default once hardware agrees.

Checked here: the new unit, the hooked `code_015.c` and `xd3d.c` compile with the Vita command lines from `make -n`
(arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9 -mfpu=neon), no fused multiply-add in the native.

## Not done / next

- `f_0014B230` / `f_0015CE90` (AI): no small exact boundary (see above); the heavy leaves are the collision ray test
  `f_001721B0` and the firing-position / path searches under `f_00160B80` and `f_00166500`.
- The rest of `f_00090770` (~14 %: `f_0003ECC0`'s own angle code, `f_001180C0` -> `f_000A43D0`) stays guest.
- Possible further native speedups not taken: interleaving two nodes' independent blend chains (the per-node work is
  a serial latency chain; the A9's sqrt/divide are not pipelined, so maybe ~25 %), shrinking the ~2 KB shadow copy to
  the ~370 bytes actually written (~1-2 % on the Pi).
