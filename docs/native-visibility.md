# Native BSP subcluster visibility pass (`XV_NATIVE_VISIBILITY`)

Sept 23 2026. Branch `work/native-visibility-20260923`. Status: implemented and verified (x86 host harness, Raspberry
Pi ARM harness, 120,000-case differential test: 0 mismatches). Default off. Units compile for the Vita; not yet
linked into a VPK or measured on hardware.

## What was replaced

`f_000539C0` is the scene's cluster-visibility pass (once per frame from `f_0005D410`). Its call tree:

| function | role | disposition |
|---|---|---|
| `f_000539C0` | clears the cluster bitmap (0x2FEDCC) and the surface bitmap (0x30BE10), runs the portal traversal, optional PVS-debug list, then the subcluster pass | guest (own work is two short `rep stos` and a flag test) |
| `f_00053540` | portal traversal: `f_000532E0` (recursive), clip/polygon code `B7F10`, `B71C0`, `B7B40`, `5C5E0`, ... | guest, phase-timed separately (~1.5 ms on the Vita with `5B9A0`/`537E0`) |
| **`f_00052E10`** | **subcluster pass: for each visible cluster, each subcluster AABB against the entry's frustum; visible subclusters OR their surface indices into the surface bitmap** | **native** |
| **`f_0005C300`** | **leaf: enclosing-box reject, 8 corners x 4 planes (x87), classification 0/1/2** | **native (inlined)** |
| `f_000537E0` | alternative when cluster 0 has no subclusters | guest |

The "539C0 self 4.5 ms" of the Vita phase timers (handoff §54) is `f_00052E10` + `f_0005C300`: the phase timers
wrap the calls to `53540`, `5B9A0` and `537E0` but not `52E10`, and 539C0's own instructions (two short `rep stos`
bitmap clears) are microseconds. On the host the subtree is `f_0005C300` 3.2 %, `f_00052E10` 2.3 % and most of
`x87_load_f32` 3.5 % / `x87_compare` 0.9 % of the helper+worker samples (a10, frames >= 1500).
The hot `f_00052520`, `f_00052F50`, `f_00052D50`, `f_00053E90` of the Pi sampler are NOT under 539C0
(`53E90` is called from the ordered-pass code; `52520`/`52D50`/`52F50` from other BSP/render functions).

### Why this boundary

- `f_00052E10` + `f_0005C300` is the smallest subtree that holds the measured 4.5 ms and it makes no calls at
  all besides 5C300 -> no HLE, no draw code, no guest callbacks, nothing to keep as a guest call.
- One call per frame, one call site (`539C0:53AF2`, `push ebp; call 52E10`), so the hook is a single entry hook.
- The portal traversal (`53540`) is ~1.5 ms incl. and x87-heavy recursive clipping with existing partial
  natives; the next candidate if more is needed, but a different order of work.

## Semantics reproduced (exact)

`recomp/kernel/xk_native_visibility.c`:

- guest memory through the same translation the shards use (thread page table; `X_IMG*` through the page table
  under `XV_RENDER_VIEW`, image base otherwise); integer accesses single-translation like `X_M32`, float
  loads/stores page-split like `x87_load_f32`/`x87_store_f32`;
- float math: loads widened to double, the per-plane operand order of the guest (`((x*n0 + y*n1) + n2*z) - d`,
  `((n0*x + n2*z) + y*n1) - d`, `((n2*z + y*n1) + n0*x) - d` twice), compared to the float at 0x1F0A68 with the
  x87 compare semantics (outside = `>` ordered); the unit is compiled `-ffp-contract=off`;
- every guest read that can observe a write is done at the same point as the guest (surface lists, bitmap
  words, subcluster counts, cluster fields, view count); the count at 0x38BE10 is written through;
- exit state: eax/ecx/edx (the guest's last assignments), esp += 8 (`ret 4`), the canonical lazy-flag record
  of the last compare, x87 sp/control/status and the two scratch slots `f_0005C300` leaves, and the dead
  stack below esp (52E10's locals and pushes, the last `f_0005C300` frame: corner floats, pushes, counter);
- the back-edge budget `c->preempt` drops by exactly the guest's back-edge count (7 per full classification
  plus the three loops) and `xv_preempt` is called the same number of times, at the end of the pass rather than
  mid-loop (on the scene helper under the overlap it never yields; on the owner it is a scheduling point only);
- a surface index outside [0, 0x400000) (a write outside the bitmap) writes the same word and reloads every
  cached value (count, eps, frustum, stack locals); counted as `oob` (0 in all runs).

Not reproduced (unobservable): the stale `f_cf`/`f_of` cells (both overrides are 0 after the final compare, so
no flag read consults them) and NaN payload bits in the dead x87 scratch slots (which NaN an addition propagates is
the host compiler's operand order; the guest body built -O0 and -O2 already disagrees). Map data has no NaNs.

## Knobs and counters

- Build: `XV_NATIVE_VISIBILITY=1` (Makefile; adds the source via `games/halo_ce_3925/runtime.mk`,
  `-DXV_NATIVE_VISIBILITY=1`, `-ffp-contract=off` for the unit). `XV_NATIVE_VISIBILITY_DEFAULT` (0).
- Hook: `python3 tools/patch_native_visibility_hooks.py <stage>/build/recomp` (idempotent; one hook at the entry
  of `f_00052E10` in `code_009.c`).
- Env `XV_NATIVE_VISIBILITY`: 0 off (default), 1 verify (native, then restore and run the guest body on the same
  state with an unbounded budget, compare everything, keep the guest's result), 2 native.
- Env `XV_NATIVE_VISIBILITY_TIME=1`: per-pass microseconds (`xk_os_monotonic_us`); with mode 0 it times the
  guest body, with 2 the native, with 1 both.
- 60-frame line from xd3d.c's report block:
  `[native-visibility] 60 frames: passes N verified N mismatched N (total mismatches N); subclusters N full N
  visible N new-bits N oob N; us/pass native X guest Y`. Mismatches also print up to 12 detail lines
  (`MISMATCH <field> native .. guest ..`).

## Verification

All with the stage copy of `overlap-candidate` (Sept 23 evening shards) plus the hook; scene on the helper thread
(`XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1 XV_RENDER_VIEW_THREAD=1 XV_RENDER_VIEW_ALL=4`) unless noted.

**Differential test** (`tools/test_native_visibility.py <stage>/build/recomp [cases] [--seed N]`): 120,000 cases
(2 seeds x 3 builds x 20,000; builds: plain page table -O2, per-thread table + render view -O2, -O0), 0 mismatches.
Compared per case: the whole 24 MiB arena, every xctx field (f_cf/f_of excepted), xv_preempt call count. ~20 % of the
cases reach the 0x4000 cap. Mutants caught: summation order of a plane (33/3000 cases), cap test `>`
(529), back-edge count (2527), no reload after a stray write (256), enclosing-box `>=` (128), a dead corner
store (2186). One mutant is equivalent for every terminating input (`edx` exit without sign extension: differs
only with >= 0x8000 subclusters in a cluster).

**In-game verify mode** (`XV_NATIVE_VISIBILITY=1`, every pass compared: registers, flags, x87, stack window, the
whole 512 KiB bitmap + count; the guest result is kept):

| run | where | frames | passes | subclusters classified | new bits | mismatches | stray writes |
|---|---|---|---|---|---|---|---|
| ver1 | x86 host, menu -> a10 cinematic -> start of play | 17,760 | 17,080 | 4.83 M | 100.9 M | 0 | 0 |
| ver2 | x86 host, same + 25 min of turning/look input | 44,760 | 44,071 | 12.13 M | 213.4 M | 0 | 0 |
| ver-owner | x86 host, scene on the owner (`XV_SCENE_THREAD=0`) | 20,760 | 20,068 | 5.34 M | 95.9 M | 0 | 0 |
| nv-ver1 | Pi 4 (ARM, cores 0-1), a10 from a new game, 45 min | 43,560 | 42,055 | 11.68 M | 205.4 M | 0 | 0 |
| nv-vver | Pi 4, a copy of the Vita save (Continue at its a10 checkpoint), player turning/walking, 25 min | 28,320 | 27,712 | 0.86 M | 14.8 M | 0 | 0 |

Total: 150,986 in-game passes (81,219 x86, 69,767 ARM), 34.8 M subcluster classifications, 630 M new bitmap
bits, 0 mismatches, 0 stray writes. (One Pi run, `nv-nat`, stayed in the main menu - a missed pad press, the
same binary entered the campaign on the rerun; not counted.)

## Cost, guest vs native (host-side; not Vita numbers)

`XV_NATIVE_VISIBILITY_TIME=1`, microseconds per pass (one pass per frame), mean over 60-frame windows:

| where | method | guest | native | ratio |
|---|---|---|---|---|
| x86 host (-O1 build) | separate runs, mode 0 vs mode 2, same pad script, cinematic windows | 167.0 | 23.9 | 7.0x |
| x86 host | same, gameplay windows | 172.7 | 29.4 | 5.9x |
| x86 host | verify mode (both on the same state; guest runs second, warm) | 167.8 | 30.9 | 5.4x |
| Pi 4 ARM (-O1, armv7 NEON) | verify mode, 701 windows | 817.5 | 119.3 | 6.9x |
| Pi 4 ARM | separate runs, mode 0 vs mode 2, frames 1500-8700 (a10 cinematic), 120 windows each | 938.6 | 123.7 | 7.6x |
| Pi 4 ARM | verify mode, Vita-save gameplay (smaller views: ~31 subclusters, ~530 new bits per pass) | 196.8 | 25.2 | 7.8x |

Sampler (`XV_HOST_SAMPLE`, helper+worker samples, frames >= 1500): mode 0 has `f_0005C300` 2.7 % + `f_00052E10`
2.1 % + ~2.8 % of `x87_load_f32`/`x87_compare`/`x87_store_f32` (those drop from 4.0 % to 1.2 % in mode 2) = ~7.6 %;
mode 2 has `nv_run` 1.4 %.

Expected on the Vita: the phase timers put `539C0` self at ~4.5 ms in the a10 cinematic; with the 6-8x ratio measured on x86 and ARM
the pass would cost ~0.6-0.8 ms, i.e. roughly -3.5 to -4 ms of scene-helper time per frame. To be measured.

## Vita integration (main session)

Files (branch `work/native-visibility-20260923`, 3 code commits on top of 52647f6):

1. `recomp/kernel/xk_native_visibility.c` (new; Vita object 6.9 KB text, 76 B BSS; the two 512 KiB verify
   buffers are malloc'ed on the first verify pass only).
2. `Makefile`: the `XV_NATIVE_VISIBILITY` block after the `XV_NATIVE_CRT_FLOAT` block (flag + `_DEFAULT` + the
   `-ffp-contract=off` rule for `$(RECOMP_BUILD)/kernel/xk_native_visibility.o`). The stage Makefile's block
   order differs from the source; paste the block after its `XV_NATIVE_CRT_FLOAT` block.
3. `games/halo_ce_3925/runtime.mk`: `XITA_GAME_SRCS += recomp/kernel/xk_native_visibility.c` under
   `ifeq ($(XV_NATIVE_VISIBILITY),1)`.
4. `recomp/kernel/xd3d.c`: one weak call `xv_native_visibility_report(60)` next to `xv_crt_float_report`.
5. Shards: `python3 tools/patch_native_visibility_hooks.py <stage>/build/recomp` (hooks `f_00052E10` in
   `code_009.c`; idempotent).
6. Build with `XV_NATIVE_VISIBILITY=1` added to `make-vars.txt`. `code_009.c`, `xd3d.c` and the new unit are
   rebuilt by mtime; no other unit depends on the flag. Check:
   `arm-vita-eabi-nm build/recomp/code_009.o | grep xv_native_visibility` (U) and the ELF has
   `T xv_native_visibility`.
7. Runtime: `XV_NATIVE_VISIBILITY=1 XV_NATIVE_VISIBILITY_TIME=1` once (verify on hardware: expect
   `mismatched 0`; the frame pays guest + native + ~1.5 MB of bitmap copies), then
   `XV_NATIVE_VISIBILITY=2` (native) against `0`, both with `XV_NATIVE_VISIBILITY_TIME=1` (the us/pass pair), or
   with `XV_SCENE_PHASES=1` (539C0 self should drop from ~4.5 ms). `XV_NATIVE_VISIBILITY_DEFAULT=2` in the make
   vars makes native the default once hardware agrees.

Checked here: the new unit, the hooked `code_009.c` and `xd3d.c` compile with the Vita command lines from
`make -n` (arm-vita-eabi-gcc -O2 -mthumb -mcpu=cortex-a9), no fused multiply-add in the native.

## Not done / next

- `f_00053540` (portal traversal, ~1.5 ms with `5B9A0`/`537E0` on the Vita) stays guest; it is recursive,
  x87-heavy (`B71C0`, `B7B40`, `5C5E0`) and already has partial natives - the next candidate under 539C0.
- 539C0's own body (two bitmap clears, PVS-debug path) is microseconds; not worth a native.
- Possible further native speedups (count kept in a register, per-page bitmap pointers) were not taken: the pass
  is already ~7x faster and every extra shortcut widens the exactness argument.
