# D3D draw recording speed (`XV_REC_*`)

Sept 24 2026. Branch `work/d3d-record-20260924` (worktree `d3d-record-wt`, base `c122b5c`). Not pushed.
Round 2 (a30 pod and cutscene: `XV_REC_QUAD`, `XV_REC_VSC`, `XV_REC_ENTRY`): [d3d-record-speed2.md](d3d-record-speed2.md).

Status: five exact fast paths for the scene thread's D3D recording, each behind its own environment knob (default
0 = original code). Every path has a verify mode that runs the original and the new code in the same process and
compares every output. Verified with 0 mismatches:

- 25,920 frames (10.75 M recorded draws) on the Raspberry Pi 4 ARM harness
- 49,800 frames (12.8 M draws) on the x86 harness
- earlier builds of the same paths: 76,000 more frames
- host unit tests

Paired A/B measurement on the Pi, all knobs against the original: the scene helper does **6.0% fewer cycles**
(-2.40 +- 0.21 Mcycles/frame). The recording path itself does 20% less work, and the helper-bound frame is 1.59 +- 0.21 ms
shorter. Expected on the Vita: **about 2.3-3.6 ms/frame** off the a10 steady scene helper. That is not enough on its
own to reach 50 ms from 56.6 ms. Nothing here has run on hardware yet. Every changed unit compiles with the Vita
command lines.

| knob | where | new path | Pi, sampled Mcycles/frame saved | Vita estimate, ms/frame |
|---|---|---|---|---|
| `XV_REC_QUERY` | `runtime/xv_d3d.c` | visibility-query slots through a hash index instead of three 512-entry scans | 0.39 | 0.3-0.6 |
| `XV_REC_INDEX` | `runtime/xv_d3d.c`, `xv_index_copy.h` | one read of each guest index: copy, bounds, mirror and coverage bitmap in one pass | 0.42 (with memcpy) | 0.5-0.75 |
| `XV_REC_DRAW` | `runtime/xv_d3d.c` | memos of per-draw lookups over append-only tables; the trace flags read once per draw | 0.27 | 0.3-0.4 |
| `XV_REC_HLE` | `recomp/kernel/xd3d.c` | render-state table, draw-hash and histogram gates, one-page texture-state read, cached `XV_WATCH` | 0.60 | 0.6-0.9 |
| `XV_REC_CAPTURE` | `runtime/xv_vertex_capture.c` | capture-reuse candidates through a 1024-bucket table hashed on all address bits | 0.13 | up to 0.2 |
| **all** | | | **1.80** (knob symbols); **2.40 +- 0.21** (helper counters) | **2.3-3.6** |

"Vita estimate" is explained in [Expected saving on the Vita](#expected-saving-on-the-vita).
The per-knob figures are rough (they scale by call counts), and the total is the defensible number.

## How it was measured

- **Harness.** `tools/host_build.py --runtime` links the Vita recording path (`runtime/xv_d3d.c`, `xv_ui_gxm.c`, the
  capture/prepare/upload workers, the shader loader) into the whole-game host harness. It replaces the GXM, kernel
  and IO calls with `recomp/host/vita_runtime_shim.c`. GXM draws are no-ops, so recording runs in full and nothing is
  replayed. The runtime units and `xd3d.c` build at -O2 like the Vita's. The guest code builds at -O1.
- **Pi 4.** Cortex-A72 at 1.8 GHz, static armhf build (GCC 13.3), `taskset -c 0,1`, own directory `~/xita-d3d`.
  Another agent used cores 2-3 throughout. The Pi stayed at 1.8 GHz with no throttle flags.
- **Scene.** New a10 campaign into the intro cinematic, which gives the Vita's "a10 steady" frames. Scene overlap
  mode 2 (`XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2`, render view, native pack), lockstep virtual clock
  (`XV_LOCKSTEP=2`), about 490 recorded draws per frame. On the Vita, the a10 in-game frame records 290-340 draws
  (perf186d/h and perf187).
- **Counters and samples** (`recomp/host/sampler.c`). `XV_HOST_PERF=1` counts the scene helper's user cycles,
  instructions and CPU time per 60 frames. `XV_HOST_PERF_SAMPLE=200000` samples the helper every 200,000 user cycles
  (perf_event_open, works at `perf_event_paranoid` 2). ITIMER_PROF is limited to HZ=250 on the Pi and was too coarse.
- **Paired A/B** (`XV_REC_AB=<frames>`). Every knob alternates between the original path (phase 0) and the new
  path (phase 1) every 120 recorded frames within one run. The flip happens at a recording-frame start (xd3d.c
  `xv_rec_ab_frame`). The Pi shares its L2 and DRAM with the other agent's work, so separate runs drifted by several
  percent. `tools/rec_ab.py` pairs neighbouring 60-frame windows of opposite phase and reports the mean difference
  with its standard error. `--samples` gives per-symbol cycles per phase. The runtime prints
  `[rec-ab] draw|hle frames a b` markers, so a Vita log can be split the same way.
- **Tools.** `tools/record_profile.py` (groups, outermost and innermost symbol, source line, `--focus`, `--phase`,
  `--ab`) and `tools/rec_ab.py`.

## Profile: where the scene helper's cycles go

Pi, final A/B run `final-ab-pi`, frames 3000-18720, phase 0 (original code, 130 windows), 39.84 Mcycles/frame
sampled:

| group | Mcycles/frame | share |
|---|---|---|
| guest code (`f_*`) | 22.49 | 56.4% |
| D3D recording (`runtime/`, `xd3d.c`) | 8.79 | 22.1% |
| other (x87 helpers, natives, merge, scene-thread plumbing) | 5.73 | 14.4% |
| libc (`mem*`/`str*`, `getenv`) | 2.59 | 6.5% |
| kernel/HLE | 0.25 | 0.6% |

The largest recording symbols in phase 0, in Mcycles/frame:

| symbol | Mcycles/frame | note |
|---|---|---|
| `memcpy` | 0.99 | mostly capture-arena copies. The Pi's capture worker is starved on 2 cores (see below) |
| `record_draw_body` | 0.93 | |
| `cap_reuse_find` | 0.67 | |
| `xv_index_copy_reference_bounds_neon` | 0.55 | |
| `xv_vertex_capture_submit` | 0.48 | |
| `retain_old` | 0.48 | |
| `rs_method_old` | 0.32 | |
| `ui_texture_for_pal` | 0.31 | |
| `xd3d_r_visibility_end` | 0.28 | |
| `xd3d_ps_sync` | 0.24 | |
| `xd3d_hash_call` | 0.20 | |

Nothing dominates: the recording path is a long flat list of small costs. On the Vita the same work shows as
`[draw-prep]` (perf186d, in-game a10, 14.4 ms/frame with the profiler on). Its larger stages are:

- indices 4.0 ms (index-cache 1.3, index-scan 1.4)
- streams 4.1 ms (capture submit 2.5)
- textures 2.5 ms
- state 1.4 ms
- program 1.1 ms

The same work also shows as `[hle-time]` (perf186h, 23 ms/frame of D3D HLE over about 9,000 timed calls). Its larger
entries are:

| call | ms/frame | calls/frame | per call |
|---|---|---|---|
| DrawIndexedVertices | 10.7 | 199 | |
| End | 2.8 | 205 | |
| SetTextureState_Deferred | 2.3 | 2,473 | |
| SetRenderState_Simple / NotInline | 1.9 / 1.3 | 1,718 / 1,117 | |
| SetVertexData4f | 1.1 | 755 | |
| SetVertexData2f | 0.59 | 139 | 4.2 us |
| EndVisibilityTest | 0.52 | 174 | 3.0 us |

Pi-only effects, which the Vita does not share:

- **Capture submit.** It waits on the capture worker, which gets little time on 2 cores. `[draw-prep] streams` is
  18.9 ms/frame on the Pi against 4.1 ms on the Vita.
- **Host HLE dispatch.** Every HLE call on the host runs `xv_scene_thread_join_owner` / `d3d_call`. The Vita skips
  these with a stack-range check.
- **Uncached memory.** The Vita's uncached GPU memory is ordinary cached memory on the host.

The measurements below compare phases within the same run, so these effects cancel out of the differences. They
still dilute the Pi's relative numbers for DrawIndexedVertices.

## What changed

All five knobs follow `runtime/xv_record_opt.h`:

- `XV_<NAME>=0` (default): the original path only.
- `1`: verify. The original path runs and its result is used. The new path runs on the same inputs into scratch
  outputs, every output is compared, mismatches are counted and the first 8 are logged. Every 60 frames it prints
  `[rec-verify] <knob> mode 1: <checks> checks <mismatches> mismatches (session ...)`. `XV_REC_VERIFY_ABORT=1`
  aborts at the first mismatch.
- `2`: the new path only.

A knob is read once, on first use. None of them changes a recorded command, draw, state or value. The memos cache
only functions of data that never changes after it is written: append-only tables, and guest code hashes.

### `XV_REC_INDEX`: index retain in one pass (`retain_indices` -> `retain_old` / `retain_new`)

The original path reads each guest index up to three times:

- compare it with the cache mirror
- copy it
- scan it for bounds and the per-draw vertex coverage bitmap (a read-modify-write per index)

The new path reads each index once:

- **Cache hit that needs this frame's GPU copy.** `xv_index_copy_if_equal` copies the list while checking it
  against the mirror.
- **Miss.** `xv_index_copy2_reference_bounds` / `xv_index_copy2_bounds` write the GPU slot and the mirror from the
  same loads and track min/max.
- **Coverage.** Each index does one independent store into a byte map. `xv_index_pack_groups` then packs the map
  into bitmap words with NEON (`vtst`/`vand`/`vpadd`) and clears only the used words.

Verify runs the new path into a shadow pool and a shadow cache entry, then runs the original. It compares:

- the result and the pool fill
- the GPU index bytes
- the returned pointer, by pool offset
- the vertex count
- the whole coverage struct
- the cache entry and its mirror

Measured on the Pi, index-scan fell 35% and the indices stage 19%.

### `XV_REC_DRAW`: per-draw lookups memoized

- **Vertex program handle by microcode hash** (`xv_d3d_handle_for_hash`, 32-entry memo over
  `handle_for_hash_scan`). The handle slots are append-only.
- **Combiner table entry** per (vertex program, combiner key, 2D mask) (`ps_entry_memo`, 128 slots over
  `ps_entry_for`). The table only grows. The table-full fallback is never memoized. The combiner key is recomputed from the
  live combiner registers at every sync. When Halo patches a register, the key changes, so a memo entry is never stale.
- **Reference layout** per declaration, stream and stride (`vertex_reference_layout`, 64 slots).
- **Texture-coordinate scale** per stage size word.
- **Trace flags.** The two flags (`trace_frame_live`, `vertex_trace_frame_live`) are read once per draw instead of at
  every use.

Verify recomputes each lookup and compares it with the memo.

### `XV_REC_QUERY`: visibility-query slots through an index

The flare and the other occlusion queries go through `xv_visibility_issue`, `read`, `find`, `generation`, `stale` and
`wait`. Each of these scanned the 512-entry result table.

- **Why an index is exact.** Slots are allocated at the first empty entry and never freed. The used slots are
  therefore a prefix, and each ID owns exactly one slot.
- **Index layout.** A 2048-cell open-addressing index (`g_vis_map`, slot + 1 per cell) finds it.
- **Concurrency.** The single issuer publishes each cell with a release store after the slot's ID. A concurrent
  reader either finds the slot or stops at an empty cell, exactly as the scan would.
- **A/B.** In A/B runs the index is maintained in both phases.

Verify runs the scans and compares:

- every slot, return code and serial
- the prefix invariant, on every allocation

On the Pi, EndVisibilityTest fell 50%, and visibility result reads fell about as much.

### `XV_REC_CAPTURE`: capture-reuse candidate table

`cap_reuse_find` walked a 256-bucket chain hashed on address bits 4-11, so aligned sources shared chains. The new
path looks candidates up in a second, 1024-bucket table hashed on all address bits (`cap_bucket2`). Entries of one
identity sit in the same bucket, in insertion order, in both tables, so the first full-key match is the same entry.

Verify walks both tables and compares the candidate. On the Pi the chain steps fell from 115,000 to 56,000 per
60 frames.

### `XV_REC_HLE`: the hottest state setters (`recomp/kernel/xd3d.c`)

- **Render-state methods** (SetRenderState_Simple, SetRenderStateNotInline, the pixel-shader and stencil helpers).
  One lookup in a table built once from the original switch and `ps_method_to_def`. It replaces the case dispatch and
  the pixel-shader register range chain. Verify runs the original on the live state and the table path on a copy of
  the whole `xd3d_state`, and compares the two byte for byte. Halo patches the combiner registers in guest memory
  through these calls; the table writes the same `ps_shadow` words and sets `ps_dirty` exactly as before.
- **Draw-hash gate.** Every D3D HLE entry called `xd3d_hash_call`, which returns at once while the draw-stream hash
  is off (`g_draw_hash_on == 0`). Mode 2 skips the call inline. It still calls it while the setting is unread (-1) or
  on.
- **Histogram gate.** `xd3d_hist_active()` is skipped while no trace is configured (`g_hist_frame == -1`). Verify
  compares with the call.
- **Texture states.** `xd3d_texture_states` reads the 20 words of the texture-state table
  (`0x18F180..0x18F37F`, one guest page) through one translation instead of 20. Verify compares all 20 words.
- **`XV_WATCH` in SetVertexData2f.** SetVertexData2f called `getenv("XV_WATCH")` on every register-0 call after
  frame 1: 385,872 calls in 4,200 host frames, and 139 calls/frame on the Vita, where it costs 4.2 us/call against
  1.5 us for SetVertexData4f. Mode 2 caches the answer. Only two places set the environment:
  - the startup config loader, which runs before any frame
  - the remote `env` command, which now bumps `xv_env_generation` (`runtime/xv_remote.c`) and so invalidates the
    cache

  Verify compares the cached answer with a live getenv.

The HLE path is chosen once per frame (`hle_mode_now`), so a gate costs one load and a compare.

## Looked at and left out

- **Skipping redundant per-draw sync** (`sync_draw_state`: `xd3d_ps_sync`, texture states, `xv_d3d_SyncDrawState`).
  Halo patches combiner registers and render states between almost every pair of draws. The state generation changes
  before every draw, and the sync inputs were identical by value in under 1% of draws. The pixel-shader identity and
  constant-colour caches that already exist (`XV_PREP_STATE_CACHE`) cover the repeated part. The only exact part
  left is the one-page texture-state read above.
- **`XV_REC_TEXTURE`** (per-frame texture-source memo). About half the resolver calls hit, but checking the key
  words cost as much as the hits saved (`ui_texture_for_pal` 0.32 -> 0.21 Mcycles/frame, memo 0.12). Measured
  neutral and removed.
- **Blend-variant and constant-stream memos** (DRAW). The scans they replace are as short as the memo check.
  Neutral, removed.
- **Single-page SetVertexShaderConstant copy** (HLE). 0.32 -> 0.30 Mcycles/frame. Within noise, removed.
- **Flare quad path.** Each kept flare quad costs:
  - an occlusion query: Begin/EndVisibilityTest plus a result read, now through the `XV_REC_QUERY` index
  - a recorded draw through `draw_immediate_flare` -> `record_draw`, which gets the `XV_REC_DRAW` memos

  `[flare-work] recording` fell 5-7% on the Pi (0.37 -> 0.34 ms/frame for 103 kept quads/frame). The rest is the
  same per-draw recording as any draw: program/constant sync, state sync and command write. No flare-specific
  shortcut was exact short of batching quads, which would change the recorded draws.
- **Capture-arena `memcpy`.** Mostly a Pi artifact: the capture worker is starved on 2 cores, which forces arena
  copies. Not a Vita cost.
- **Host HLE dispatch** (`xv_scene_thread_join_owner`, `d3d_call`, `xv_scene_thread_on_helper`). Host only. The
  Vita skips these with a stack-range check.
- **NEON beyond the index pass.** The index coverage pack is the one place where the loads were the cost. The
  remaining recording hot spots are pointer chasing and branches (`record_draw_body`, `cap_reuse_find`,
  `ui_texture_for_pal`), not data-parallel loops.

## Equivalence evidence

All results: 0 mismatches. "Final code" means the branch's knob code in its last form (commits up to `e73d7f8`;
later commits only add log markers and tooling). Frame counts are recorded frames.

Unit tests (`make -C recomp/host test-rec`, x86 and Pi):

| test | cases | result |
|---|---|---|
| `rec_index_test` | 464 index lists, scalar and NEON (Pi): bytes, bounds, coverage, fused hit copy | pass |
| `rec_hle_test` | 262,144 render-state cases (every method word x 16 values x both `ps_synced` states, random start states), whole `xd3d_state` compared; texture-state page read against 20 reads | pass |
| `rec_query_test` | 24 issue/read/generation/stale sequences, 208,000 verified lookups, IDs below and above the table size, table-full path | pass |

`make -C recomp/host test-draw-prep` also runs the existing index-copy and draw-state tests, which pass. Two older
tests fail with and without this branch, on files it does not touch:

- `draw_profile_test`, an assertion on the `xv_draw_profile.c` report text
- `ps_cache_test`, the fragment assertion at line 353

Its `retain_indices` checks pass in all three modes.

In-game verify runs, all five knobs in mode 1:

| run | code | frames | draws | HLE checks | INDEX | DRAW | QUERY | CAPTURE |
|---|---|---|---|---|---|---|---|---|
| Pi `final-verify-pi` (a10 cinematic) | final | 25,920 | 10.75 M | 211.5 M | 7.13 M | 76.2 M | 15.1 M | 10.9 M |
| x86 `final-cine-x86` (a10 cinematic) | final | 24,960 | 10.74 M | 209.6 M | 7.13 M | 75.9 M | 15.6 M | 10.7 M |
| x86 `final-walk-x86` (a10 checkpoint, scripted look/move/fire) | final | 24,840 | 2.02 M | 57.0 M | 1.36 M | 14.1 M | 2.35 M | 1.85 M |
| x86 `vlong-cine`, `vlong-walk` | before the XV_WATCH cache | 23,760 + 23,640 | | 237 M | 7.7 M | 82 M | 15.9 M | 11.4 M |
| Pi `vall-pi1/2/3` | earlier builds, before the XV_WATCH cache (pi1: INDEX, DRAW, HLE; pi2 adds QUERY; pi3 adds CAPTURE) | 28,560 | | 160 M | 5.9 M | 67 M | 9.8 M | 3.6 M |

## Measured savings

### Paired A/B, all knobs (`XV_REC_AB=120`)

| metric | Pi phase 0 | Pi phase 1 | Pi paired change | x86 phase 0 | x86 paired change |
|---|---|---|---|---|---|
| scene helper Mcycles/frame | 39.96 | 37.74 | **-2.40 +- 0.21** (-6.0%) | 16.26 | -0.68 +- 0.08 (-4.2%) |
| scene helper Minstr/frame | 33.78 | 31.67 | -2.25 +- 0.20 (-6.6%) | 37.14 | -1.83 +- 0.17 (-4.9%) |
| scene helper CPU ms/frame | 32.94 | 31.36 | -1.71 +- 0.17 | 3.65 | -0.14 +- 0.02 |
| frame (game) ms | 46.84 | 45.41 | **-1.59 +- 0.21** (-3.4%) | 33.20 | 0 (fixed 30 fps clock, not helper-bound) |
| frame draw-hle ms | 22.74 | 22.06 | -0.73 +- 0.17 | 0.95 | -0.06 +- 0.01 |

The runs used:

- **Pi, `final-ab-pi`:** 130 pairs, frames 3000-18720.
- **x86, `final-ab-x86`:** 153 pairs, frames 3000-21480, with the stage timers on.

The sampled recording group fell from 8.79 to 7.05 Mcycles/frame (-20%). Everything outside guest code fell from
17.35 to 15.43.

### Per knob (Pi, sampled Mcycles/frame, same run)

| knob | symbols | phase 0 | phase 1 | change |
|---|---|---|---|---|
| QUERY | `xv_visibility_*`, `vis_*`, `xd3d_r_visibility_*` | 0.537 | 0.151 | -0.386 |
| INDEX | `retain_*`, `xv_index_copy*`, `xv_vertex_refs_add`, `index_bounds` | 0.729 | 0.361 | -0.367 |
| DRAW | `handle_for_hash_scan`, `ps_entry_*`, `vertex_reference_layout*`, trace flags | 0.412 | 0.146 | -0.266 |
| HLE draw-hash gate | `xd3d_hash_call`, `xd3d_hash_gate` | 0.299 | 0.064 | -0.235 |
| HLE render-state table | `rs_method*`, `rs_state_fast`, `rs_diag`, `ps_method_to_def` | 0.511 | 0.357 | -0.154 |
| HLE XV_WATCH cache | `getenv`, `strncmp*` | 0.139 | 0.003 | -0.136 |
| CAPTURE | `cap_reuse_find`, `cap_bucket2` | 0.428 | 0.302 | -0.126 |
| INDEX (memcpy share) | `memcpy` | 0.989 | 0.934 | -0.054 |
| HLE texture-state page | `xd3d_texture_states*` | 0.118 | 0.070 | -0.048 |
| HLE histogram gate | `xd3d_hist_*` | 0.140 | 0.110 | -0.030 |
| all knob symbols | | 4.302 | 2.499 | **-1.803** |
| guest code (`f_*`) | | 22.49 | 22.21 | -0.28 (cache effects or noise) |
| everything else | | 13.05 | 12.93 | -0.12 |

### Stage timers (Pi, `final-stage-pi`)

Settings: `XV_DRAW_PROFILE=1`, `XV_HLE_TIMING=1`, 119 pairs. Values are ms/frame. The timers add their own
overhead: with them on, the helper change is -2.47 +- 0.28 Mcycles/frame and the frame change is -1.31 +- 0.24 ms.

| stage | phase 0 | paired change | relative |
|---|---|---|---|
| `[draw-prep]` setup | 0.328 | -0.071 +- 0.002 | -22% |
| `[draw-prep]` state | 0.523 | -0.021 +- 0.003 | -4% |
| `[draw-prep]` indices | 1.568 | -0.294 +- 0.007 | -19% |
| (`[draw-prep-sub]` index-scan) | 0.606 | -0.211 +- 0.004 | -35% |
| (`[draw-prep-sub]` index-cache) | 0.496 | -0.077 +- 0.002 | -16% |
| `[draw-prep]` program | 0.376 | -0.064 +- 0.002 | -17% |
| `[draw-prep]` streams (capture submit; Pi-starved) | 18.92 | -0.16 +- 0.16 | -1% |
| `[draw-prep]` textures | 0.822 | -0.014 +- 0.009 | -2% |
| `[flare-work]` recording | 0.560 | -0.027 +- 0.006 | -5% |
| `[hle-time]` DrawIndexedVertices | 19.19 | -0.48 +- 0.14 | -2.5% (diluted by the Pi's streams wait) |
| `[hle-time]` SetRenderState_Simple | 1.262 | -0.123 +- 0.011 | -10% |
| `[hle-time]` SetRenderStateNotInline | 0.828 | -0.061 +- 0.005 | -7% |
| `[hle-time]` EndVisibilityTest | 0.218 | -0.108 +- 0.009 | -50% |
| `[hle-time]` SetVertexData2f | 0.178 | -0.099 +- 0.001 | -56% |
| `[hle-time]` End | 0.810 | -0.036 +- 0.007 | -4.5% |
| `[hle-time]` SetTextureState_Deferred | 1.205 | -0.027 +- 0.007 | -2% |
| `[hle-time]` SetVertexShaderConstant / SetTexture / SetStreamSource | 0.33 / 0.33 / 0.21 | -0.02 each | -6 to -9% (hash gate) |

## Expected saving on the Vita

Three estimates, all from the Pi run and the Vita's own a10 logs:

1. **Stage by stage.** For each stage, take the Vita's ms (perf186d `[draw-prep]`, perf186h `[hle-time]`, in-game
   a10 windows) and multiply by the Pi's paired relative change for the same stage.
   - `[draw-prep]` stages: -1.19 ms (indices -0.76, program -0.19, setup -0.11, state -0.06)
   - HLE handlers outside the draw calls: -1.10 ms (SetVertexData2f -0.33, EndVisibilityTest -0.26, render
     states -0.29, the others -0.22)

   Total **about -2.3 ms**. The flare quads are counted inside `[draw-prep]`. The small handlers carry about 0.4 us
   of timer overhead per call in the Vita log, so this estimate leans high for them and low for DrawIndexedVertices.
2. **Per knob, by call counts.** Each knob's sampled Pi cycles are scaled by the ratio of Vita to Pi event counts,
   at 444 MHz. The Pi's frame makes about twice the Vita's D3D calls:
   - draws 322/490
   - DrawIndexedVertices 199/310
   - render-state calls 2,835/6,603
   - EndVisibilityTest 174/243
   - SetVertexData2f 139/266
   - HLE calls 9,000/18,000

   Result: QUERY 0.63, INDEX 0.53-0.61, DRAW 0.40, draw-hash gate 0.26, XV_WATCH 0.16, render-state table 0.15,
   CAPTURE 0.18, texture states 0.07, histogram 0.03. Total **about -2.4 ms**.
3. **Whole helper, by draw count.** -2.40 Mcycles x 322/490 = -1.58 Mcycles, which is **-3.6 ms** at 444 MHz. This
   includes the small guest-code and cache side effects.

So: **about 2.3-3.6 ms/frame** off the scene helper, assuming a Cortex-A9 needs about as many cycles as the A72 for
this code. Per-call checks agree to within about 50%. The removed work is mostly scans and chased loads, and
those cost relatively more on the Vita's slower memory. The a10 steady frame is helper-bound, and on the Pi the frame
gained at least as much as the helper did, so the frame should drop from 56.6 ms to about 53-54 ms. **These changes
alone do not reach the 50 ms target.** The next recording costs are the capture submit, `record_draw_body` and the
texture resolvers. Beyond those, the rest of the gap is guest code (56% of the helper).

## Enabling and verifying on hardware

The knobs are runtime environment variables, read once on first use. Set them in `ux0:data/xita/env.txt`, which
the startup loader reads before the first frame. Setting them later over the remote only works before first use.
No make variables are needed.

1. **Verify.** Set `XV_REC_INDEX=1 XV_REC_DRAW=1 XV_REC_HLE=1 XV_REC_QUERY=1 XV_REC_CAPTURE=1`. Play a10 (the
   cinematic plus some gameplay) for as long as practical; the host runs used 25,000 frames. Check the log:
   - every `[rec-verify] XV_REC_* mode 1` line reports `0 mismatches (session ... 0 mismatches)`
   - no `[rec-verify] ... mismatch` detail lines appear

   Verify mode is slower than both paths put together. The render-state check copies and compares the 3.7 KB
   `xd3d_state` on every render-state call, about 2,800 per frame. Expect lower fps while verifying. To keep the
   frame closer to normal, verify `XV_REC_HLE` in a separate session from the other four.
2. **Measure.** Set `XV_REC_AB=120` (no per-knob settings needed: every knob alternates 0/2). Add
   `XV_DRAW_PROFILE=1` and/or `XV_HLE_TIMING=1` for the stage split. Then run
   `python3 tools/rec_ab.py <log> --from 3000`, which prints:
   - `frame game ms` (the helper-bound frame)
   - `frame draw-hle ms`
   - every `[draw-prep]`, `[draw-prep-sub]`, `[flare-work]` and `[hle-time]` figure

   Each figure comes with phase 0 and phase 1 means and the paired difference. The windows are classified by the
   `[rec-ab] draw` / `[rec-ab] hle` markers.
3. **Use.** Set all five to `2`.

## Integrating into `overlap-candidate/build-x87`

I did not modify `overlap-candidate/` or `build-x87`. These steps are for whoever integrates. In build-x87, the six
affected runtime and kernel files equal this branch's base (`c122b5c`) except for one line of `xd3d.c` (line 707,
report-call order). `runtime/xv_record_opt.h` is new. The patch below applies cleanly to copies of them, without fuzz, and the result compiles with the
Vita command line.

```sh
WT=/home/birchwoodgod/xita-backups/2026-09-18-unified-games/d3d-record-wt
cd $WT && git diff c122b5c work/d3d-record-20260924 -- runtime/xv_d3d.c runtime/xv_index_copy.h runtime/xv_record_opt.h \
    runtime/xv_vertex_capture.c runtime/xv_ui_gxm.c runtime/xv_remote.c recomp/kernel/xd3d.c > /tmp/rec-knobs.patch
cd /home/birchwoodgod/xita-backups/2026-09-18-unified-games/overlap-candidate/build-x87
git apply --check /tmp/rec-knobs.patch && git apply /tmp/rec-knobs.patch     # works outside a git repository too
```

Then rebuild as usual. Nothing new goes into `build-command.json` or `make-vars.txt`. Only these units recompile:

- `runtime/xv_d3d.c`, `xv_vertex_capture.c`, `xv_ui_gxm.c`, `xv_remote.c`
- `recomp/kernel/xd3d.c`

No guest code (`code_*.c`) changes. The knobs default to 0, so a build with the patch behaves like one without it until
`env.txt` sets them. The host-only files (`recomp/host/*`, `tools/host_build.py`, `recomp/kernel/xk_scene_thread.c`'s
weak role hook) are not needed on the Vita.

## Files

Runtime and kernel (Vita):

- `runtime/xv_record_opt.h`: the knob type, modes, A/B phase, report
- `runtime/xv_index_copy.h`: fused copy, bounds and coverage
- `runtime/xv_d3d.c`: INDEX, DRAW, QUERY
- `runtime/xv_vertex_capture.c`: CAPTURE
- `recomp/kernel/xd3d.c`: HLE, A/B phase, `[rec-ab] hle` marker
- `runtime/xv_ui_gxm.c`: `[rec-ab] draw` marker
- `runtime/xv_remote.c`: `xv_env_generation`

Host only:

- `recomp/host/vita_runtime_shim.c`, `neon_x4_compat.h`, `harness.c` and `host_reports.c` hooks
- `recomp/host/sampler.c`: helper kind, LR, perf counters and sampling, A/B windows
- `recomp/host/rec_index_test.c`, `rec_hle_test.c`, `rec_query_test.c`, plus the Makefile `test-rec` target
- `tools/host_build.py --runtime`, `tools/record_profile.py`, `tools/rec_ab.py`, `tools/host_profile.py`

Logs, not committed, in `d3d-record-work/`:

- `pi-runs/final-verify-pi.log`, `final-ab-pi.{log,samples}`, `final-stage-pi.log`, `final-ab-pi.phase{0,1}.txt`
- `runs/final-cine-x86.log`, `final-walk-x86.log`, `final-ab-x86.log`
