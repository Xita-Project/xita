# x87 register-stack codegen (`xita_recomp.py --x87-regs`)

Status (2026-09-23 night): opt-in recompiler mode, default off, branch `work/x87-regs-20260923`.
Host (x86) verified against a new state-hash oracle in 72 run pairs (4,380 to 12,480 lockstep frames each:
menu, a10 load, cinematic, cryo bay with look input, checkpoint gameplay). Pi (ARM) runs are stable but the Pi harness is not
reproducible run to run even in baseline, so ARM is not bit-verified (see Verification). Not yet built or
measured on the Vita.

## Why

The default lowering keeps the x87 stack in the guest context:

    X_ST(i)      = c->st[(c->fsp + i) & 7]
    x87_push(v)  : c->fsp = (c->fsp - 1) & 7; c->st[c->fsp] = v
    x87_pop()    : c->fsp = (c->fsp + 1) & 7

so every x87 operand is a dynamically indexed load or store through `c`, and GCC cannot keep an x87 value
in a VFP register. The tick's hottest leaves are x87-heavy (handoff §62: `f_0004B9D0` 186 x87 loads,
~11.8 ms/frame; `f_00092330` ~6.9 ms/frame self), and so is much of the scene.

## What the mode does (`recompiler/x87_regs.py`)

**Static depth.** Inside a function, the depth `d` is the number of net pushes since entry. At depth `d`,
`st(i)` is the logical slot `L = d - 1 - i` (the first push is slot 0; the caller's `st(0)` is slot -1).
Every slot the function touches becomes a `double` local (`xr0`, `xr1`, ..., `xrn1` for slot -1) that
mirrors `c->st[(fsp0 - 1 - L) & 7]`, where `fsp0` is `c->fsp` at entry. The status word becomes a
`uint16_t` local `xfsw`; the control word stays in memory (`x87_round` reads `c->fcw`).

**Same arithmetic.** `lower_x87_regs` is `Emitter.lower_x87` with `X_ST(i)` replaced by slot names: the same
C expressions in the same order. `x87r_compare` / `x87r_fxam` (`recomp/xv_x87reg.h`) are `x87_compare` /
`x87_fxam` on the local status word, including the ARM `vcmp` path, the `fcomi` EFLAGS write and the TOP
field (`(fsw & ~0x4700) | cc | top << 11`, OR-ed in exactly like the memory lowering; TOP = `(fsp0 - d) & 7`
is static). `xv_x86rt.h` and `xv_recomp_protos.h` are untouched (both are pinned by
`tools/query_memory_capture.py`).

**Sync points.** Memory is made current wherever anything else can look at it:

* before every call (direct, `XV_HLE_CALL`, kernel thunk, indirect `xv_call`), return, tail call (`jmp`,
  `jcc`, `loop` to another function, indirect tail jump) and the emitter's trailing `return;`: store every
  slot written since the last sync (popped ones too, so `c->st[]` holds exactly what the memory lowering would
  have left in it), `c->fsp = (fsp0 - d) & 7` when it changed, `c->fsw = xfsw` when written;
* after every call: reload every slot and `xfsw` (the callee may have written any register).

Slots are filled from `c->st[]` at entry (after the entry hooks); GCC drops the dead loads. `X_PREEMPT()` is
not a sync point: `xv_preempt`/`xk_yield` switch to other guest fibers, each with its own `xctx`; nothing reads
a suspended fiber's x87 state (object-job contexts are copied at `f_0008FB70` entry, a sync point; a job
that faults aborts).

**Call effects.** The depth after a call needs the callee's net x87 effect. Two interprocedural fixed points
over all 8021 functions (optimistic TOP -> value -> BOTTOM lattice, callers re-queued on change):

* *proven*: direct callees' summaries plus `HLE_X87_DELTA` (only `xv_hle_crt_fmod` moves the stack, -1;
  every other HLE and kernel export leaves it alone, checked by grepping recomp/kernel and runtime). Any
  indirect call below makes a summary BOTTOM. CE: 4152 functions prove 0, 93 prove +1 (float returns),
  115 -1, ...; 3553 are BOTTOM, almost all through an indirect call somewhere below (object/type callbacks,
  the CRT's matherr/exception dispatch).
* *assumed*: the same, but an unknown effect takes what the caller's own code expects (`guess()`: +1 when the
  next x87 instruction after the call reads below the pre-call depth, i.e. consumes a float return, else 0).
  CE: 7348 assumed 0, 125 +1, 180 still BOTTOM.

A call whose effect is proven is used as is. A call whose effect is only assumed is **guarded**: right after
it, `if (c->fsp != ((fsp0 + K) & 7)) goto M_<block>_<index>;` jumps into a copy of the whole function body in
the *memory* lowering (labels `M_...`, emitted after the register body, reached only from failed guards),
just after the same call. That is exact by construction: memory is authoritative right after a call and the
memory lowering needs nothing else. `xv_x87reg_miss()` (xv_x86rt.c) counts misses and logs the first 32.
Measured: the only missing site is `0x180BDB` (the CRT `_CIfmod` dispatcher calling the argument classifier
`0x220FF`, whose effect differs per path), ~0.19 misses/frame on host and Pi, every one an exact fallback.

**Fallbacks** (whole function stays in the memory lowering; `<outdir>/x87_regs_report.json` lists every
function with its reason, summaries and per-function slot/sync/guard counts):

| reason | CE count | meaning |
|---|---|---|
| `join-depth@X` | 64 | block X is reached with two different depths |
| `hook-before-instruction` | 51 | a game hook inserts native code mid-body (it may read `c->st`), e.g. `f_0008DDF0` |
| `hook-transform` / `hook-transform-refused` | 8 | `transform_body` does more than insert observer lines, or its body pin refuses the new text (a pinned needle appears twice once the memory copy exists, e.g. `f_0005B4A0`) |
| `insn:fnsave`, `insn:fnstenv`, `insn:fstpnce` | 6 | instructions that save, inspect or reset the whole stack (also `fninit`, `frstor`, `fldenv`, `fincstp`, `fdecstp`, unknown mnemonics) |
| `slot-range` | 3 | slots span more than the 8 physical registers |
| `hook-entry-goto` | 2 | an entry hook jumps into the body (would skip the slot fill) |
| `density` | 0 (718 at R=3) | `--x87-regs-min-density R`: fewer than R x87 instructions per sync point |

`unreached` and `noreturn-call` exist but did not occur with guards on. Entry hooks without `goto` run before
the fill. Observer-only transform hooks (light census, hold profile, phase timers: `OBSERVER_LINE`) are
allowed when both lowerings pass the insertion check (e.g. `f_00092330`'s light census).

Result for CE 3925: **2107 of 2241 x87 functions converted**, 3818 guarded calls in 917 functions (the rest
have proven effects). Converted: `f_0004B9D0`, `f_00092330`, `f_00063C00`, `f_000B8980`, `f_00061270`,
`f_00019E7B`, `f_0015CE90`. Not converted: `f_0008DDF0` (hooks), `f_0004C980` (stage body differs, below).

Options: `--x87-regs-only 4B9D0,92330` / `--x87-regs-exclude ...` (hex entries, for bisecting),
`--x87-regs-no-guards` (only functions whose calls are all proven), `--x87-regs-min-density R`.

## Verification

**Default output unchanged**: without `--x87-regs` every generated file is byte-identical to the regeneration
before this work (re-checked with the final code).

**Oracles.** `XV_TICK_TRACE` and `XV_DRAW_HASH` as asked, plus a host-only `XV_STATE_HASH=<file>`
(`recomp/host/host_reports.c`): at every present, FNV over every Halo data array in the game-state region
(`d@t@` header + all elements) and over the physics block (+0x5C..0xB8: position, velocities, orientation,
bounds) of every live object; every 60 frames the per-array hashes. `XV_STATE_HASH_SKIP=name,...` drops
arrays, `XV_STATE_DUMP=<array>,<frame>` dumps one array every 60 frames. `tools/state_hash_compare.py A B`
compares two runs at equal ticks. All runs `XV_LOCKSTEP=2 XV_SOFTGFX_RASTER=0`, the tools/host_run.sh knobs
(object jobs with 2 workers, owner phase, vertex worker).

What two *baseline* runs agree on (measured, not assumed):

* The level start can land later (streaming I/O timing during the load, handoff §41). Same-phase runs are
  comparable frame by frame; runs one tick apart at equal ticks, where they differ only at the spawn tick
  0x369; runs several ticks apart (2 of 26 host runs, one of them a baseline) are not comparable at all.
* `XV_DRAW_HASH` is **not** a cross-run oracle: two baseline runs share 751 of 4380 frames (handoff §41 too).
* Arrays that follow host real time differ between baseline runs: `sounds`, `looping sounds`,
  `object looping sounds`, `xbox sound`, `xbox sound cache`, `xbox texture`, `xbox texture cache`,
  `cached object render states`, and downstream `effect`, `effect location`, `particle*`, `lights`,
  `decals`, `decal vertex cache`, and one actor's perception fields in `actor`/`prop` (a 3x3 look matrix at
  +0x18C..0x1AC and a few words at +0x124, +0x5A4..0x5C4, +0x6FC..0x71C of actor 2). Timing control: the same
  baseline build at two host speeds (taskset) differs in `actor` in 113 of 206 reports; base vs regs at equal
  speed in 38. Making DirectSound follow the virtual clock did not remove it (the streaming caches remain).
* Identical in every same-phase baseline pair: the tick trace, the object physics hash at every equal tick,
  and every other array (objects, players, units, weapons, AI encounters, ...).

Host results, `--x87-regs` (all converted functions, final splice) against the memory lowering:

| scenario | comparable runs | frames | tick trace | object physics (equal ticks) | arrays beyond the noise set |
|---|---|---|---|---|---|
| menu -> a10 load -> cinematic -> cryo bay | 3 base, 3 regs | 4,380 each | identical (same phase) | 4,282/4,282 | none |
| same + look/move input, two host speeds | 5 base, 4 regs | 12,480 each | 12,480/12,480 | 12,381/12,381 | none |
| checkpoint Continue + move/fire input | 3 base, 4 regs | 10,680 each | 10,680/10,680 | 10,679/10,679 | none |

(All 72 base-regs, base-base and regs-regs pairs among those are clean: `tools/state_hash_compare.py` finds no
physics difference except the spawn tick of one-tick-apart runs, no array beyond the noise set, and an equal
tick trace at every equal tick; same-phase pairs match frame by frame. Two of the regs runs in the last row
predate the final splice change, which only moved the CRT native hook ahead of the slot fill.)

Actor field dumps (`XV_STATE_DUMP=actor`) show base-vs-regs differences only in the fields base-vs-base
pairs also differ in. No mismatch attributable to the codegen was found. Caveat: the gameplay reached with
scripted input is the cryo bay and the checkpoint area; the Vita's heavy a10 corridors (many bipeds, where
`f_0004B9D0` costs 11.8 ms/frame) were not reached on the host.

**Pi (ARM).** Static armhf harnesses (`-marm -mfpu=neon`, `-O1`) ran 29,000 frames (1 core each) and 14,100
frames (2 cores each) with no fault; guard misses as on the host. But two *baseline* Pi runs differ from each
other: level phase offsets of 60-120 ticks, object physics different at 86 % of equal ticks, with object
jobs on or off (`XV_EXPERIMENTAL_OBJECT_JOBS=0`), one or two cores. Base vs regs differs at the same rate
(65-94 %) and in no array outside the noise set, so no ARM-specific divergence is *detectable*, but the Pi
cannot give a bit-exact verdict until its runs are reproducible (the slow SD-card streaming during the load
is the likely cause: the load phase decides when scripted sequences start). An in-process check
(register body vs memory body on the same inputs) would be the ARM oracle.

## Speed

**Pi 4 (ARM A72, the closest bench to the A9)**, `XV_HOST_SAMPLE` over the same frame range of the intro
(220-232 60-frame windows, owner thread):

| | base samples | regs samples | change |
|---|---|---|---|
| all guest code (`f_*`), run 1 (1 core each) | 66,890 | 61,188 | -8.5 % |
| all guest code (`f_*`), run 2 (2 cores each, final splice) | 59,007 | 53,890 | -8.7 % |
| owner thread total (incl. runtime) | 147,358 | 140,573 | -4.6 % |
| `f_00061270` (pack float3) | 2,763 | 1,855 | -33 % |
| `f_00052520` | 2,631 | 1,736 | -34 % |
| `f_000637A0` | 898 | 593 | -34 % (run 1) |
| `f_00011120` | 1,090 | 823 | -25 % |
| `f_00063C00` | 1,319 | 1,190 | -10 % |
| `f_0004B9D0` / `f_00092330` | 170 / 118 | 178 / 122 | noise: rarely run in the cryo bay |
| `f_00019E7B` (floor helper, 18 x87 / 8 calls) | 162 | 341 | +110 %: call-heavy, x87-light |

The losers are functions with few x87 instructions between many calls (reload after every call).
`--x87-regs-min-density 3` keeps those in the memory lowering (1390 functions convert; every converted
body is byte-identical to the verified full set) and is the suggested first Vita configuration.

**Host x86** (`-O1`, lockstep idles most of each frame): guest code -2.4 % (intro) / -2.8 % (checkpoint);
per-function counts are too small to resolve single functions; out-of-order x86 hides most of the memory
traffic anyway. Not indicative of the Vita.

**Vita code (static, arm-vita-eabi-gcc -O2 -mthumb)**: `f_00092330` 1954 -> 1808 instructions, vldr 60 -> 47,
vstr 70 -> 36 (no guards, one body). Functions with guards carry the memory copy: `code_008.o` 1.54 -> 1.88 MB,
shard compile time +20..60 % (code_008 181 -> 245 s). The real Vita gain needs a Vita run (a10 steady,
`[tick-phases]` for 4C980 -> 4B9D0 and 8D760 -> 92330 are already in the stage).

Side observation: `XF_P` (`__builtin_parity`) compiles to a libgcc `__paritysi2` call on ARM (11 call sites in
`f_0004B9D0` alone, every `fnstsw; test ah; jp` idiom); an inline 8-bit parity would remove them (xv_x86rt.h
is pinned, so that is a separate change).

## Integrating into a Vita stage

The stage shards are hand-maintained (handoff §27), so converted bodies are spliced, not regenerated:

1. In a checkout of this branch, regenerate twice from the stage's own XBE and symbols with the stage's
   flags (the overlap stage uses `--phase-timing`):

       PY=/home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/venv/bin/python
       ST=<stage>/build
       $PY recompiler/xita_recomp.py $ST/haloce/default.xbe --profile halo_ce_3925 \
           --symbols $ST/local/halo_ce_3925/halo_symbols.json --phase-timing -o /tmp/regen-base
       $PY recompiler/xita_recomp.py $ST/haloce/default.xbe --profile halo_ce_3925 \
           --symbols $ST/local/halo_ce_3925/halo_symbols.json --phase-timing --x87-regs \
           --x87-regs-min-density 3 -o /tmp/regen-regs

   (Regeneration needed two fixes first: 60f31c5, 75c4270.)
2. Copy `recomp/xv_x87reg.h` into `$ST/recomp/` and add `xv_x87reg_miss()` from `recomp/xv_x86rt.c` to the
   stage's copy (optional: shards reference it weakly; without it misses are silent).
3. `python3 tools/splice_x87_regs.py --stage $ST/recomp --base /tmp/regen-base --regs /tmp/regen-regs
   [--dry-run] [--exclude ...]`. A function is replaced only when the stage body equals the baseline
   regeneration, or equals it after removing the known observer patches (older entry-line order,
   `patch_scene_phase_timers.py` timers, `patch_crt_float_hooks.py` hooks), which are re-installed (the CRT
   hook ahead of the slot fill). overlap-candidate at R=0: 2068 spliced, 33 with observers, 6 kept
   (`4C980`, `96430`, `B77C0`, `170C10`, `172BF0`, `172F40`: comment text/hook placement differ from today's
   generator). Re-running the stage's patch tools afterwards is safe (idempotent, same anchors).
4. Delete `build/recomp/code_*.o` and `build/recomp/xv_x86rt.o` (the shard rule does not depend on
   `xv_x87reg.h`), then build as usual (`PATH=build/bin:$PATH make -j3 xita.vpk $(cat ../make-vars.txt)`).
   `make -n` on the spliced stage emits the same 120 compile commands; no Makefile gate trips. For the host or
   Pi harness: `tools/host_build.py` as usual (touch the shards if objects are newer than the splice).
5. Checking a build: `XV_STATE_HASH=<file>` host/Pi runs and `tools/state_hash_compare.py`; the log's
   `x87-regs guard miss` lines count the fallbacks (~0.2/frame expected, all at 0x180BDB).
6. To take functions out: `--x87-regs-exclude` in step 1 or `--exclude` in step 3.

## Files

* `recompiler/x87_regs.py` - analysis, summaries, plans, register lowering
* `recompiler/xita_recomp.py` - flags, sync points, prologue, guarded calls, memory copy (`M_` labels)
* `recomp/xv_x87reg.h` - `x87r_compare`, `x87r_fxam`, `xv_x87reg_miss` declaration
* `recomp/xv_x86rt.c` - `xv_x87reg_miss`
* `tools/splice_x87_regs.py` - stage integration
* `recomp/host/host_reports.c` - `XV_STATE_HASH`, `XV_STATE_HASH_SKIP`, `XV_STATE_DUMP`
* `tools/state_hash_compare.py`, `tools/sampler_compare.py` - verification and profile comparison
* Work area (not committed): `xita-backups/2026-09-18-unified-games/x87-regs-work/` (stage copies
  `stage-base`/`stage-regs`, harness objects `objs-{base,regs}-{x86,arm}`, runs, scripts)
