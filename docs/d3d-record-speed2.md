# D3D draw recording speed, round 2 (a30 pod and cutscene)

Sept 24 2026. Branch `work/d3d-record2-20260924` (worktree `d3d-record2-wt`, base `b7b8179`). Not pushed. Round 1:
[d3d-record-speed.md](d3d-record-speed.md).

Scenario: `XV_LEVEL=a30`, new campaign: the lifepod cutscene (host frames ~540-780), then the Chief standing in the pod
(steady state from ~frame 800). Host harness with the Vita play settings (`XV_REC_*=2`, `XV_NATIVE_4B9D0=2`,
`XV_SOUND_OBSTRUCTION=6`, natives, render view, 360p, shadows/reflections/decals off), `XV_LOCKSTEP=2`, scene overlap
mode 2. The host pod records 476 draws per frame: 428 DrawIndexedVertices, 30 DrawVertices, 22 End, the same as the
Vita's pod before `XV_OCCL` (perf195p: 425 / 30 / 22).

## Summary

- **The Vita's DrawVertices cost is reads of uncached memory.** Every DrawVertices in a30 is a QUADLIST. Its rewrite
  to triangles reads the sequential index list and then the written quad pool back for the vertex bound; both live in
  `xv_d3d_scratch`, which is `USER_RW_UNCACHE`. The Vita build (Thumb, `-O2`) re-reads the source after every store:
  6 loads + 6 stores per quad in the rewrite, then 6 more loads in `index_bounds`. **`XV_REC_QUAD`** removes all of
  them (details below). Exact, 0 mismatches.
- `XV_REC_VSC` (SetVertexShaderConstant) and `XV_REC_ENTRY` (the HLE entry hook) are exact instruction savings:
  -0.28 Minstr/frame on the Pi, no measurable cycle change there. Small on the Vita too (estimate 0.2-0.5 ms).
- Measurement tools added to the host harness: uncached scratch memory on the Pi (`XV_HOST_UNCACHED`), raw PMU event
  counting and sampling (`XV_HOST_PERF_EVENTS`, `XV_HOST_PERF_SAMPLE_EVENT`), A/B of selected knobs
  (`XV_REC_AB_KNOBS`), the Vita's on-helper HLE fast path on the host, and a cheap host clock.
- What the Vita's `[hle-time]` numbers mean: **each timed HLE call carries about 0.82 us of timer overhead** on the
  Vita (`SetRenderState_ZBias` has an empty body and times at 0.28 ms / 343 calls). So in perf195p the ~19,000 timed
  calls per frame include about 15 ms of timer overhead. The real recording cost in the pod is about 23 ms/frame
  after the skip (perf202: DrawIndexedVertices 11.8, DrawVertices 7.9, the state setters about 2.5, End 0.6), not 45.

## Where the Vita's DrawVertices time goes

The same 30 DrawVertices per frame cost 7.3-7.9 ms on the Vita in perf195p and perf202 (247 us per call), and
1.7-1.9 ms in perf196p (57 us per call). A DrawIndexedVertices costs about 48-55 us in all three.

The host shows two pod states with the same draws:

| host pod state | quad lists/frame | indices rewritten/frame | uncached loads/frame (original code) |
|---|---|---|---|
| usual | 40-42 | ~2,300 | 4,400-4,700 |
| high (5 of 14 host runs, from the cutscene's end on) | 48 | ~17,000 | 34,000 |
| cutscene windows | 80-110 | 2,000-4,300 | 4,000-8,700 |

The high state has 8 more DrawVertices quad lists per frame, of 300-1,900 vertices each, on top of the usual ones.
They look like a particle effect in the pod. Which state a run lands in depends on thread timing, even under
`XV_LOCKSTEP=2`: in one batch of six concurrent runs, four went high.

Fitting the two Vita pod runs to the two host states gives about **0.17-0.19 us per uncached halfword load** on the
Vita, plus about 37 us of other work per DrawVertices call:

- (7.5 - 1.8 ms) / 29,500 extra loads = 0.19 us. Some of the high state's extra time is also its larger vertex
  capture, so 0.17 us is the lower value.
- The Pi with uncached scratch memory (below) measures 52-56 ns per avoided load. The Cortex-A72 overlaps misses
  (out-of-order); the Vita's in-order Cortex-A9 cannot.

This fit is an inference from logs, not a hardware measurement. The hardware check is below, under
[Enabling on hardware](#enabling-verifying-and-measuring-on-hardware).

## What changed

All knobs follow `runtime/xv_record_opt.h`: 0 (default) = original code, 1 = verify, 2 = new path.

### `XV_REC_QUAD` (`runtime/xv_d3d.c` `rewrite_quads_opt`, `runtime/xv_quad_rewrite.h`)

- **Sequential QUADLIST** (DrawVertices, the immediate HUD and flare quads). The rewrite of `0,1,2,...` is always a
  prefix of one fixed list: quad k -> 4k, 4k+1, 4k+2, 4k, 4k+2, 4k+3.
  - `xv_d3d_init` writes that list once, as `g_seq_quads` (49,152 indices, 96 KiB), after every existing range of
    the scratch block. Every original offset is unchanged.
  - The new path points the command at the matching prefix and writes nothing.
  - It advances the quad pool's fill exactly as the original did. So later rewrites see the same room and the same
    truncation, and a draw that would not fit is truncated or dropped exactly as before.
- **Other sources** (guest-indexed QUADLIST or POLYGON, sequential POLYGON). The same stores, with each source value
  loaded once into a register. A sequential source value is its position, so it is never loaded.
- **The bound.** The rewrite tracks the bound as it goes. `retain_indices` takes it from `g_quad_bound` for this draw
  only, instead of scanning the pool.
- **Verify** runs the new path into a cached shadow pool, then the original. It compares:
  - the result, the count and the pool fill
  - every index value
  - the vertex bound, against `index_bounds` of the original's output
- **Report.** `[rec-quad]` reports the lists taken each way and the uncached loads avoided per 60 frames.

### `XV_REC_VSC` (`recomp/kernel/xd3d.c` SetVertexShaderConstant)

- **Fast path.** When the source rows lie in one guest page:
  - they are checked for non-finite components in place. The check is exponent == 0xFF, which is exactly
    `!isfinite`; the build has no fast-math.
  - they are copied with one translation and one `memcpy`.
- **Fallback.** Any non-finite component, or a source that crosses a page, takes the original row loop, which zeroes
  and logs the non-finite components.
- **Verify** copies the rows to scratch and compares them with the rows the original wrote.

### `XV_REC_ENTRY` (`recomp/kernel/xd3d.c` `xd3d_count`)

- **The original.** Every D3D HLE entry called `xd3d_count` out of line. On the scene helper that call only:
  - stores the name
  - finds, by stack range, that it is on the helper, where the two scene-thread hooks return at once
  - returns, while the histogram is off
- **Mode 2** does exactly that inline and calls the original otherwise.
- **Verify** checks each inline "skip" answer against `xv_scene_thread_on_helper()` and the histogram setting.

### Measurement support

- **`XV_REC_AB_KNOBS=<env>[,...]`.** Limits the `XV_REC_AB` alternation to the listed knobs. The other knobs keep
  their configured mode, so the new knobs are measured on top of the Vita's `XV_REC_*=2`.
- **`XV_HOST_UNCACHED=<block>[,...]`** (1 = `xv_d3d_scratch`, in `recomp/host/vita_runtime_shim.c`). Allocates those
  `USER_RW_UNCACHE` blocks from `/dev/vcsm-cma` on the Pi, which gives a write-combined, uncached user mapping:
  - a u16 load costs 24 ns, against 3 ns cached
  - stores stay buffered

  So the harness pays for uncached loads as the Vita does, only less.
- **`XV_HOST_PERF_EVENTS=0x01,...` and `XV_HOST_PERF_SAMPLE_EVENT=0x01`** (`recomp/host/sampler.c`). Raw PMU counts
  per role (`[host-perf-ev]`), and sampling by a raw event.
- **The Vita's on-helper fast path on the host.** The host helper publishes its pthread stack range
  (`xk_scene_thread.c`), and `host_build.py` passes `-DXV_HOST_HELPER_SP` to `xd3d.c`. The Pi therefore no longer pays
  two scene-thread hook calls per HLE call. That was host-only overhead of about 0.76 Mcycles/frame.
- **Cheaper host clock.** `xk_os_monotonic_us` divides in 32 bits. armhf has no 64-bit divide, and `__udivmoddi4` was
  10-15% of the Pi's scene helper with `XV_HLE_TIMING=1`.
- **Link fix.** `runtime_stubs.c` no longer defines `xd3d_r_visibility_result` in `--runtime` builds. The
  `XV_HOST_VISIBILITY_PIXELS` stub had broken those links.

## Equivalence evidence

All results: 0 mismatches.

| run | machine | frames | recorded draws | QUAD checks | VSC checks | ENTRY checks |
|---|---|---|---|---|---|---|
| `vx86-1` (a30: cutscene + pod) | x86 | 26,880 | 12.5 M | 1.29 M | 30.7 M | 563 M |
| `vx86-a10` (a10 cinematic, flares) | x86 | 26,880 | 9.2 M | 4.27 M | 12.7 M | 349 M |
| `vpi2` (a30, `XV_HOST_UNCACHED=1`) | Pi 4 | 34,860 | 16.3 M | 1.68 M | 40.0 M | 733 M |

All three runs had every knob in mode 1 on top of the Vita play settings (`XV_REC_*=2`). None of the three scenes had
a guest-indexed QUADLIST or POLYGON. The unit test covers those.

Unit tests (`make -C recomp/host test-rec`, x86 and armhf):

- `rec_quad_test`: 2,940 cases against a verbatim copy of the original rewrite and `index_bounds`:
  - QUADLIST and POLYGON
  - sequential and guest sources: random, clustered, top-of-range and zero values
  - counts 0 to 65,536
  - pool fills from empty to full

  The test compares result, count, fill, index values, bound and the untouched pool bytes.
- `rec_hle_test`: the round-1 render-state cases, plus 6,000 randomized SetVertexShaderConstant calls:
  - register windows inside and outside c[-96..95]
  - counts 0 to 200
  - page-crossing and unaligned sources
  - NaN, Inf, denormal and -0 components

  Every call leaves the whole `xd3d_state` equal to the original's.
- `rec_index_test`, `rec_query_test`: pass as before.

## Measured on the Pi

Paired A/B, `XV_REC_AB=120`, frames after 1500, Pi 4 cores 2-3. Another job ran on cores 0-1 throughout, sharing
the L2 cache.

| run | knobs alternated | memory | pairs | helper Mcycles/frame | helper Minstr/frame | DrawVertices ms/frame (HLE timing) |
|---|---|---|---|---|---|---|
| `abq1` | QUAD | uncached scratch | 128 | **-0.241 +- 0.069** | +0.03 +- 0.06 | **1.230 -> 0.970 (-0.258 +- 0.013, -21%)** |
| `abq2` | QUAD, VSC, ENTRY | uncached scratch | 169 | +0.13 +- 0.40 (noisy) | -0.278 +- 0.053 | |
| `ab1` | QUAD, VSC, ENTRY | cached | 157 | +0.008 +- 0.065 | -0.335 +- 0.056 | |

`abq1` also measured the frame at -0.245 +- 0.059 ms.

`abq2` sampled per symbol, recording path, Mcycles/frame:

- `rewrite_quads` + `index_bounds`: 0.236 -> 0.002
- SetVertexShaderConstant + `x_guest_read` + `vsc_fast`: 0.415 -> 0.298
- `xd3d_count` + `xd3d_count_slow`: 0.661 -> 0.559
- everything outside guest code: -0.18

In the pod's usual state (4,630 loads/frame), the Pi's uncached loads cost 52-56 ns each. The Pi pod helper uses
35-38 Mcycles/frame without timers: guest code 60%, recording path 19%, libc 7%.

## Expected saving on the Vita (estimate)

| scene | uncached loads avoided per frame | `XV_REC_QUAD` at 0.17-0.19 us/load |
|---|---|---|
| a30 pod, usual state (perf196p-like) | 4,400-4,700 | **0.75-0.9 ms** |
| a30 pod, high state (perf195p, perf202: DrawVertices 7.3-7.9 ms) | ~34,000 | **5.8-6.5 ms** |
| a30 cutscene | 4,000-8,700 | 0.7-1.6 ms |
| a10 flares (125 kept quads/frame, one draw each) | ~1,500 | 0.25-0.3 ms |

`XV_REC_VSC` + `XV_REC_ENTRY`: the Pi's 0.28-0.34 Minstr/frame, scaled to the Vita's post-skip call counts (506
SetVertexShaderConstant, about 9,500 HLE calls), is about 0.1-0.15 Minstr. That is **0.2-0.5 ms** on an in-order core.

Where the rest of the helper goes (Pi, pod): guest code is 60%, and `f_00070110` (the material/shader setup that
issues 21 SetTextureState_Deferred and 9 SetRenderState calls per material) alone is 8%. The helper takes **1.07 M L1
instruction-cache refills per frame for 27 M instructions** (one refill per 25 instructions, 9% of fetches). The
Vita's 32 KiB I-cache with 32-byte lines can only be worse. `record_draw_body` is 15.7 KiB of Thumb code on the
Vita. On the Pi, `-Os` for the recording units did not reduce the refills: 933k at `-Os` against 921k at `-O2`,
with +1.4 Mcycles. The Pi cannot show the A9's front-end cost.

## Enabling, verifying and measuring on hardware

The knobs are environment variables, read once. Set them in `ux0:data/xita/env.txt` or over the remote before the
level starts:

1. **Verify:** `XV_REC_QUAD=1 XV_REC_VSC=1 XV_REC_ENTRY=1`. Every `[rec-verify] XV_REC_QUAD|VSC|ENTRY mode 1` line
   must report 0 mismatches. Verify is slower: the quad check reads the uncached pool back, and ENTRY adds a call
   per HLE entry.
2. **Measure DrawVertices directly:** `XV_REC_AB=120 XV_REC_AB_KNOBS=XV_REC_QUAD XV_HLE_TIMING=1` on the a30 pod.
   `python3 tools/rec_ab.py <log> --from <frame>` prints `hle D3DDevice_DrawVertices` in both phases. Expected: the
   per-call time falls from ~247 us (high state) or ~57 us (usual state) toward ~37 us; the log's `[rec-quad]` line
   gives the loads avoided. Then run the same without `XV_HLE_TIMING` for the frame time.
3. **Use:** `XV_REC_QUAD=2 XV_REC_VSC=2 XV_REC_ENTRY=2`.

## Integrating into `overlap-candidate/build-x87`

`build-x87` was not modified. Its `runtime/xv_d3d.c` and `runtime/xv_record_opt.h` equal this branch's base.
Its `recomp/kernel/xd3d.c` differs only in the order of the report calls on line 756. The patch applies cleanly to
copies of those files, and the result compiles with the Vita command lines from `build-command.json`: no errors, and
no warnings beyond the existing ones.

```sh
WT=/home/birchwoodgod/xita-backups/2026-09-18-unified-games/d3d-record2-wt
cd $WT && git diff b7b8179 work/d3d-record2-20260924 -- runtime/xv_d3d.c runtime/xv_quad_rewrite.h \
    runtime/xv_record_opt.h recomp/kernel/xd3d.c > /tmp/rec2-knobs.patch
cd /home/birchwoodgod/xita-backups/2026-09-18-unified-games/overlap-candidate/build-x87
git apply --check /tmp/rec2-knobs.patch && git apply /tmp/rec2-knobs.patch
```

No new make variables, and nothing to add to `build-command.json`. Changing the headers triggers a rebuild of every
unit that includes `xv_record_opt.h`: `runtime/xv_d3d.c`, `xv_vertex_capture.c`, `xv_ui_gxm.c` and
`recomp/kernel/xd3d.c`. No generated guest code changes. `xv_d3d_scratch` grows by 96 KiB of uncached memory (the
fixed quad list), allocated whatever the knob setting.

## Files

- **Vita runtime and kernel:**
  - `runtime/xv_quad_rewrite.h` (new)
  - `runtime/xv_d3d.c`
  - `runtime/xv_record_opt.h`
  - `recomp/kernel/xd3d.c`
- **Host only:**
  - `recomp/host/vita_runtime_shim.c`, `sampler.c`, `runtime_stubs.c`
  - `recomp/host/rec_quad_test.c`, `rec_hle_test.c`, `Makefile`
  - `recomp/kernel/xk_os_host.c`
  - `recomp/kernel/xk_scene_thread.c` (host section)
  - `tools/host_build.py`
- **Logs, not committed,** in `d3d-record2-work/`:
  - `runs/vx86-1.log`, `vx86-a10.log`, `quadcount.log`, `qc*.log`
  - `pi-runs/ab1.*`, `abq1.*`, `abq2.*`, `vpi2.log`, `prof2.*`, `icache1.*`, `osA.log`, `o2A.log`

## Looked at and left out

- **Code size of the recording units** (`-Os`). No fewer I-cache refills on the Pi, and 4% more cycles.
- **Always-on per-draw timers.** `xk_os_monotonic_us` is called twice per draw for the `draw-hle` report and the
  scene-bucket detail. That is about 0.7 us per draw on the Vita, but skipping the calls would change report values.
  Left as is.
- **Capture queue depth.** `CAPTURE_JOBS` is 32. On the Vita the queue fills about once per 2 frames (perf202s:
  queue-only pressure 31 per 60 frames), each fill costing a drain. A deeper queue could remove those drains. Its
  exactness needs the arena-reclaim and reuse timing analysed first.
- **Recompiler-level inlining of trivial HLE setters.** SetTextureState_Deferred: 3,394 calls/frame on the Vita,
  about 90% of its cost call overhead. That would change generated code, so it is out of scope here.
