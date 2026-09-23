# Claude Code overnight handoff — 2026-09-20

Continuation of [claude-handoff-20260919.md](claude-handoff-20260919.md). Same
workspace, branch `work/2026-09-18-packet-followup`, private origin. Goal as
raised by the user on 2026-09-19: 30 FPS in heavy Halo CE gameplay plus
rendering fixes, "at any cost"; Halo 2 parked. Rules unchanged: cumulative
optimizations, no automated off/on/off benchmarks, no Vita3K performance
validation, full restart after updates, ordinary gameplay and logs.

Honest estimate given to the user: weeks of sustained work, not days. The
reasons are measured below.

## 1. Helper uptake question (perf48): closed

The 2–3.5 % helper uptake is a polling mismatch (waiters sleep 50 µs between
lock attempts; an offer lasts about 1 µs), but it does not matter: all
hierarchy local transforms together are under 0.05 ms/frame (17,785 nodes per
60 frames). No further work on `XV_HIERARCHY_ASSIST`; it stays harmless.

## 2. Perf49 / 062f0e1: first lock-holder attribution in ordinary gameplay

Added `XV_OBJECT_HOLDS=1` / `XV_OBJECT_HOLDS_DEFAULT` so the compiled hold
sampler runs from process start (previously reachable only through the
benchmark compare path). Tools: `tools/symbolize_vita_hold_sites.py` (reuses
the lock-site relocation recovery) and `tools/summarize_object_holds.py`.

Result (three 60-frame windows, sampled 1/64, inclusive, lane sums overlap):

| Holder | Share of sampled hold |
| --- | ---: |
| `4C980` object movement callback | 64.7 % (mean 746 µs) |
| `56670` guarded query entry | 19.4 % |
| everything else | < 5 % each |

Inside `4C980`: `4B9D0` 95 % → solver `49600` 93 % → world query `172BF0`
97 % → `171F10` 84 % → `88110` BSP sphere query 76 %, world route `171f94`
63 % of `171F10`. Extrapolated, the math mutex is held about 20 ms/frame
summed over both lanes inside a 28 ms object batch, i.e. the lanes are
effectively serialized; each lane waited 11.5–13.5 ms/frame.

## 3. Perf50 / d5a8ab2: `XV_QUERY_UNLOCK` (installed, slot 1)

Runtime SHA-256 `d230bce7bae3f43e6dbba58a252a7749ad5b6edf85795f266634090e686e6f1c`,
only `game-a.self` and `boot-game.txt` changed from perf49. Receipts in
`../query-unlock-hardware/`.

Design: the `88110` closure (`88110/87EA0/87E10/86F50/B0CB0`) makes no
absolute guest reads or stores (checked over the generated code); all access
goes through the caller's stack packet into static structure-BSP geometry and
its result lists live on the caller's private stack. `171F10` writes the shared
cluster/object visitation stamps only after the call returns. So the world
route query runs outside the actor guard: `xv_object_world_query_release` /
`xv_object_world_query_reacquire` in `xk_object_jobs.c`, wrapped in
`xv_query_reuse_run`. Admission mirrors the typed cluster-query suspension
(exact worker lane/context, depth exactly one, no other private-overlap state,
no hold profiling, no pending park, private stack mapping around ESP).
What the window exposes is the enclosing actor's pre-solver field writes
(`+18/+1C`, `+24..+2C`, `+464`) to the other lane: an ordering relaxation, not
memory unsafety. `XV_QUERY_UNLOCK=0` in env disables it.

Validation: host worker fixture 40 configurations × 600 callbacks passes plain,
ASan/UBSan and TSan with hierarchy assistance enabled; both lanes rendezvous
outside the guard. `xk_object_jobs.h` is byte-identical (the query-fusion
generator pins its hash).

Hardware (same checkpoint, four windows each):

| | perf49 | perf50 |
| --- | ---: | ---: |
| game ms (median) | 77.6 | 77.0 |
| object batch ms | 28.5–29.6 | 26.0–26.6 |
| lane wait ms/frame | 11.5–13.9 | 9.2–10.6 |
| tick `FA920` ms | 38.0–38.9 | 35.5–35.8 |
| scene `BCB30` ms | 37.2–38.9 | 39.6–40.4 |
| unlock admission | — | 100 % (≈714/60 frames), 3.3 ms/lane/frame unlocked |

The CPU saving is real but the frame did not move: the scene dispatcher's
flare-query GPU waits grew by the same amount and GPU completion latency rose
(117–125 ms vs 101–118). The performance overlay shows the worker cores at
31–35 % and the owner core at 63 %. **In this view at native resolution the
frame is GPU-bound.** No crash markers; screenshot confirms world, Marine,
pistol and HUD.

## 4. GPU composition evidence

One draw-trace frame on perf50 (frame 8407, 141 draws): 65 alpha-blended
(23 ONE/INVSRCALPHA, 13 DESTCOLOR/ZERO, 16 DESTALPHA-based, 4 additive), only
50 depth-writing, 3 render passes. Blended passes cannot use PowerVR
hidden-surface removal, so fill scales with overdraw; that is consistent with
Codex's same-view 360p result (completion 125 → 46 ms). Alpha-test discard is
already specialized away (`_na` variants selected when the test is off,
`XV_ALPHA_SPECIALIZE` default on; the opaque-material proof covers `154066FD`).
329 of 906 base `.gxp` in the stage still lack an `_na` sibling; a draw with
alpha test off that hits one of those falls back to the discard variant.

Textures are native BC with mip chains; not a texture-cache explanation.

## 5. Live render height

In gameplay `SELECT+CIRCLE` opens Xita's graphics panel; `DOWN` ×3 from row 0
selects Render resolution; `LEFT`/`RIGHT` step 360/400/480/544; `CIRCLE`
closes. Saves to `xita.cfg`, applies next frame, log line
`[settings] XV_RENDER_HEIGHT=NNN applied`. Script:
`../query-unlock-hardware/set-resolution.py --current A --target B --out DIR`
(panel row persists across opens; pass `--row 3` after a first use).

Results are appended in section 7.

## 6. Tooling notes

- Build stages need `bin/rg` on PATH: the Makefile uses `$(shell rg ...)` and
  the interactive shell's `rg` is an alias, not a binary. `build.py` in the
  new stages prepends `bin/`.
- The device `/log` GET returns at most 64 KiB per request; use
  `Client.log(path)` for the whole log. Draw trace: `vita_remote.py trace-draw`,
  then look for `[draw-state] frame` rows in the full log.
- Keep-awake: the lease API caps at 3600 s; `keepawake.sh` renews every 5 min
  (running detached in `../lock-holders-hardware/`).
- `tools/compare_gameplay_windows.py A.log B.log -n N` prints frame ms,
  batch ms, lane waits, draw-HLE and `[query-unlock]` rows.

## 7. Live 480p on perf50 (installed setting)

Switched through the panel at 00:34; `[settings] XV_RENDER_HEIGHT=480 applied`;
screenshot confirms world, Marine, pistol and HUD. Five ordinary windows each,
same checkpoint area:

| | native 544 | 480p |
| --- | ---: | ---: |
| game ms (median) | 77.5 | 71.4 (14.0 FPS) |
| GPU completion latency ms/frame | 117–125 | 74.7–75.6 |
| tick `FA920` ms | 35.5–35.8 | 37.2–37.7 |
| scene `BCB30` ms | 39.6–40.4 | 34.4–34.7 |
| object batch ms | 26.0–26.6 | 27.4–28.1 |
| draw-HLE ms | 11.3–12.1 | 10.2–11.0 |

At 480p the frame is CPU-bound again (tick + scene ≈ frame), so CPU savings
now show directly. 360p is not expected to add much beyond Codex's 69 ms
observation; 480p is retained as the installed setting (revert: panel,
Render resolution row, `RIGHT` once).

## 8. Perf51 / 058e12c: `XV_OBJECT_JOB_SPLIT` plus draw-prep profile (installed, slot 0)

Heavy/light lane split: jobs ordered by a per-object running average of
measured job time, lane 0 pops the heavy end and lane 1 the light end of one
packed CAS counter (`xk_object_jobs.c`, report `[job-split]`). Host fixture
40 × 600 passes plain and TSan with unlock and hierarchy assist; two-worker
run took 221 heavy-end / 379 light-end.

Hardware at 480p, four windows: lane 0 took ≈3,070 heavy-end and lane 1
≈1,650 light-end jobs per window; object batch 27.4–28.1 → 26.6–27.4 ms; lane
0 wait 9.5–9.9 → 8.1–8.9 ms; lane 1 wait unchanged (light jobs still take the
guard through 56670/hierarchy/matrix helpers). Small but real; kept.

The build also enabled `XV_DRAW_PROFILE_DEFAULT=1` for the `[draw-prep]`
stage breakdown. That profile costs about 2 ms/frame (draw-HLE 10.5 → 12.6),
so the frame went 72.3 → 74.3 ms; it is off again in perf52. Stage breakdown
(ms/frame, four windows): streams 5.8–6.3, textures 2.2–2.6, indices 1.6–1.8,
state 1.1–1.3, program 0.8–0.9, constants 0.4, setup 0.4.

**The streams stage is the vertex-capture reuse path.** `[vertex-capture]`
reports "capture ≈310,000 µs / 60 frames" = 5.2 ms/frame of guest-thread time
on perf50 and perf51 alike (Codex's corridor number was 16–18 ms). All 3,779
reuse checks per window were exact hits, so every compare runs its full
length (≈447 KiB/frame through `xv_bytes_equal_blocks` /
`xv_packed_equal`), about 82 µs per check. All buffers are cached
`USER_RW`; clocks are 444/222 MHz. Perf52 turns on
`XV_VERTEX_CAPTURE_DETAIL` and `XV_VERTEX_PROFILE` to split compare, copy and
bookkeeping before choosing between a cheaper change check and a different
reuse policy.

## 9. Perf52 / perf53: capture profiles and `XV_CAPTURE_TRUST_TAGS` (installed, slot 1)

Perf52 (capture detail + vertex-work profiles, draw profile off) attributed
the streams stage: sampled compares ≈65 µs per 11 KB (≈170 MB/s, far below
cached NEON throughput; timings include preemption), extrapolating to ≈4.4
ms/frame, copies ≈2 ms, publish < 1 ms.

Perf53 adds `XV_CAPTURE_TRUST_TAGS` (`runtime/xv_vertex_capture.c`): the
file layer bumps a generation whenever a read lands in the 22 MB tag cache
(physical `0x3A6000`, the static BSP/model vertex buffers; hook in
`xk_file.c` next to the texture-cache invalidation). A reuse source inside
that region under the same generation skips the byte compare; the existing
1/64 sampled submissions still compare and disable trust on the first
mismatch. Heap (dynamic) vertex data keeps the full compare. Host
`tools/test_vertex_capture.py` passes.

Hardware at 480p, three windows: trusted 3,707–3,742 hits per window skipping
24 MiB of compares, 65–68 sampled verifications, **0 mismatches**; guest-side
capture 307 → 181 ms per 60 frames (5.1 → 3.0 ms/frame); draw-HLE 11.7 → 9.1
ms; frame 74.4 → **70.7 ms median (14.1 FPS)**; tick 35.5, scene 33.

Cumulative tonight at this checkpoint: 77.6 → 70.7 ms (12.9 → 14.1 FPS),
from 480p (−6), query unlock (−0.6 at native, visible at 480p), job split
(−0.8 batch) and trusted tag reuse (−3.7). Runtime SHA-256
`92eeeb67…` (see `../capture-trust-hardware/package-check.json`), source is
`aede57b` plus the uncommitted trust change at build time (committed next).

## 10. Perf54 / 1a9c141: `XV_OBJECT_OWNER_LANE` (owner participates in the batch)

At 480p the frame is CPU-bound and the owner core idles for the whole object
batch (it only joins and services requests; the overlay showed it at ~60 %
while the worker cores sat at ~35 %). The owner now also executes jobs on
lane 2 (`xk_object_jobs.c`):

- `service_owner` is split into a reusable `service_scan(completed,
  allow_quiescent)`; the blocking loop is unchanged in behaviour.
- `owner_participate()` runs before `service_owner()` in the join: scan, and
  if no quiescent park is pending, pop a light-end job (the split's heavy-first
  order; lane 2 pops the same end as lane 1) and run it; repeat until the
  queue is empty or a park is pending (`pause-breaks`).
- The owner's guard acquisition inside a lane-2 job never blocks: it tries
  the recursive mutex, and while it fails it scans worker requests and sleeps
  50 µs (`lock-spins`). A parked worker holding the guard while it waits for
  an owner service therefore cannot deadlock the owner.
- The owner's own kernel services inside a job run inline as in owner-only
  mode; quiescent ones first park every worker (servicing non-quiescent
  requests meanwhile), then run, then a normal scan runs pending worker
  quiescent services and resumes all (`quiesce`).
- Owner-lane jobs use the plain owner guard path, so the worker-only private
  overlaps (query unlock, typed cluster query, hierarchy) decline on lane 2;
  the light-end choice keeps its guard holds short.

Validation: fixture owner-lane mode (owner-thread-aware admissions,
cross-lane rendezvous skipped, quiescent audio-commit services raised from
every lane including the owner): 40 × 600 pass plain and TSan; the standard
mode passes with the option compiled but off (the refactor touches the
default service loop). Report `[owner-lane]`. `XV_OBJECT_OWNER_LANE=0`
disables. Hardware result follows in section 11.

## 11. Perf54 hardware: HUNG at the load→gameplay transition (rolled back pending)

Deployed 01:26 (verified, boot-confirmed slot 0, runtime SHA-256 `05ac4860…`).
The campaign sequence ran; the observer saw the level still loading at frame
≈5,350 (`draws/frame 2`, `loaded 0 active 0`), and the next poll timed out.
The remote service (a separate thread) stopped answering entirely, i.e. the
device hung or livelocked around the point where the first object batches
start (perf53 reached `loaded 1 active 1` at ≈ frame 5,590). The rollback
request also timed out. **Slot 0 still boots perf54.**

Mitigation left running: `../owner-lane-hardware/rollback-watchdog.sh`
(detached; log `rollback-watchdog.log`) polls status every 20 s and issues
`rollback` (previous confirmed executable = perf53, slot 1) as soon as the
device answers. If the Vita has to be power-cycled by hand, run
`python3 tools/vita_remote.py --config PRIVATE rollback` from the dashboard
before launching Halo, or set `XV_OBJECT_OWNER_LANE=0` in env.txt. Do not
launch the campaign on perf54.

No device log could be fetched; the cause is not established. Candidates
the code review raised, none verified: a STOP on the owner lane (job-stop
parking on the owner thread), the owner's quiescent inline services at the
first batches (cache yields during streaming), or an owner-thread identity
check in a native helper (`xk_clip_region_control.c`,
`xk_polygon_edge_control.c`, pose/solver owner checks) taking an owner-only
path inside a lane-2 job. The option stays compiled out of the next build
until the device log of the hang is available. perf53 remains the last good
build; all its receipts are unaffected.

## 12. State at 01:45 and what to do first

**Installed / boot slot:** slot 0 = perf54 (owner lane, hung at load). Slot 1
= perf53 (`30ff83e`, last good: unlock + split + trusted tag reuse, 480p
setting persisted in `xita.cfg`). The detached rollback watchdog will issue
`rollback` the moment the device answers; check
`../owner-lane-hardware/rollback-watchdog.log`. If it did not fire, from the
dashboard run `python3 tools/vita_remote.py --config PRIVATE rollback`, then
confirm `status` reports `0.2.0-perf.53`. Then pull `ux0:data/xita/xita.log`
(the log file is truncated on each start, so copy it before launching again)
to see how perf54 died: a `[object-jobs] STOP` line means an abort on the
owner lane; no such line and a frozen frame counter means a hang.

**Source:** branch `work/2026-09-18-packet-followup`, HEAD after this file's
commit. Owner lane is opt-in (`XV_OBJECT_OWNER_LANE=1` build flag, default
off) and must stay out of cumulative builds until the perf54 log is read.

**Kill switches (env.txt / xita.cfg):** `XV_OBJECT_OWNER_LANE=0`,
`XV_CAPTURE_TRUST_TAGS=0`, `XV_OBJECT_JOB_SPLIT=0`, `XV_QUERY_UNLOCK=0`,
`XV_RENDER_HEIGHT=544`.

**Measured tonight (same checkpoint, ordinary play, 60-frame windows):**

| Build | Setting | game ms | FPS |
| --- | --- | ---: | ---: |
| perf48 (Codex) | native | 77.6 | 12.9 |
| perf50 unlock | native | 77.0 | 13.0 |
| perf50 | 480p | 71.4 | 14.0 |
| perf51 split (+2 ms draw profile) | 480p | 74.3 | 13.5 |
| perf53 trust | 480p | 70.7 | 14.1 |

Not 20 FPS. The frame is now CPU-bound: tick ≈ 35.5 ms (object batch 26,
of which the two lanes still wait 8.5/10 ms on the actor guard) plus scene
≈ 33 ms (draw-HLE 9; capture 3; the 5B760 per-model loop ≈ 12), sequential
on the owner thread.

**Roadmap to 20 and 30 FPS (honest):**

1. Owner lane, once the hang is understood: −6 to −10 ms (batch 26 → ~18).
2. Remaining guard holds in `4C980` (walk 1716F0 ≈ 2 ms, packet 172BF0 ≈ 1.8,
   solver 170C10 ≈ 1.8, object-route queries ≈ 1.2): each needs its own
   ownership proof like the world query; maybe −4 ms total.
3. Capture path residual 3 ms (copies of non-tag sources) and draw-HLE
   textures/indices/state stages (≈ 5 ms): −3 ms plausible.
4. That lands near 55–58 ms (≈ 17–18 FPS). **20 FPS needs the 5B760 per-model
   loop or the tick/scene split; 30 FPS needs simulation and rendering on
   different cores** (frame snapshot exchange exists as a prototype; render
   must read a consistent copy of object state). That is the multi-week item.
5. GPU: at 480p it is no longer the wall in this view (completion ≈ 40
   ms/frame with two in flight); heavy-combat views with 460 draws were not
   re-measured tonight and remain far worse.

## 13. Owner-lane hardening (f6987d4) and perf55 staged, not deployed

Review after the hang found one real hole: the owner's non-blocking guard
path was selected by a flag that any non-worker native thread (render pump,
capture and vertex workers) could also reach, which would run owner service
scans on the wrong thread. It is now restricted to the owner thread, and the
first participation is logged (`[owner-lane] first participation`) so the
device log shows whether the lane engaged before a failure. TSan owner-lane
driver 40/40. Whether this was the cause is unknown; the perf54 log decides.

`../owner-lane2-hardware/` holds perf55 (perf54 plus this fix) built and
package-checked but **not deployed**: do not deploy it blind while nobody can
power-cycle the device. Sequence for the next attempt: rollback confirmed on
perf53 → copy `xita.log` → read it → then deploy perf55 with the observer and
auto-rollback (`deploy.sh` + `wait-gameplay.py`, as in section 11).

## 14. Next guard-release candidates (static purity over the generated code)

Same check as for `88110` (absolute `X_IMG*/X_M*` reads and stores over the
call closure, perf54 stage):

| Closure | Absolute reads | Absolute stores | Verdict |
| --- | --- | --- | --- |
| `868F0` world packet build (13 fns) | none | none | candidate, ≈1.5 ms/frame held; sits between the `88110` return and the stamp stores in `171F10`, so the unlocked window could extend over it (needs the `nq_collection_172c95` hook, not the reuse adapter) |
| `170C10` solver traversal (11 fns) | `0x206F9C` only | none | candidate, ≈1.8 ms/frame held; the existing `xv_object_solver_begin/end` release exists but is Makefile-exclusive with `XV_NATIVE_SOLVER_FUSION`, so the release must be applied at the native solver call site instead |
| `1716F0` object walk (29 fns) | `0x2FC6AC` object table, `0x39CE24`; drives object-route queries | none | stays under the guard (reads other objects' data) |

These are held-time reductions of ≈3 ms in total, i.e. a couple of ms of
batch wall time; they do not change the 20 FPS picture in section 12.

## 15. 10:37 — rollback done, perf54 log lost

The Vita came back at 10:36 booted into perf54 (dashboard, fresh boot); the
watchdog issued the rollback at 10:36:20 and the device answered at 10:37:01
as **perf53, boot slot 1, runtime 92eeeb67…**. Receipts:
`../owner-lane-hardware/rollback-watchdog.log`, `postrollback-xita.log`
(79-line perf53 boot), `postrollback-launcher.log` (slot selections only).

The game log is truncated on every start, so the fresh boot destroyed the
perf54 crash/hang lines before anything could read them; the launcher log
records nothing about the session. Before any further owner-lane trial, make
the log survive a restart (rename `ux0:data/xita/xita.log` to a `.prev` copy
in `runtime/xv_log.c` before the truncating open, and serve it through the
remote log endpoint), and poll the load tail faster than 20 s in the
observer. Without that, a second failure would be as blind as the first.

## 16. 11:00 — perf55 died at the first owner-lane job; switching to a third worker

perf55 (f6987d4 + stall reporter) deployed, verified in slot 1, campaign
loaded; the rotated log's last line is
`[owner-lane] first participation: 40 queued jobs, 2 workers` and the process
died immediately (Xita returned to the dashboard; no STOP, no stall dump). The
failure is inside the first job run on the owner thread. Most plausible cause:
native stack. Workers start every job at the top of a fresh 512 KiB stack; the
owner fiber is already deep inside the tick when it takes a job. Owner-lane
design abandoned. Replacement: a third worker thread on core 2 (`WORKERS=3`),
which reuses all worker machinery including parking and the private overlaps
(query unlock) on every lane, while the owner keeps only servicing. perf53
redeployed meanwhile so the campaign is safe to launch.

## 17. Perf56 / efc5d20: three worker lanes — stable, neutral; rolled back to perf53

`XV_OBJECT_WORKERS=3` (third worker on core 2; owner keeps servicing) ran the
campaign without a hang: lanes took ≈2,330 / 1,200 / 1,150 jobs per window and
the query unlock was admitted on all three. But the object batch stayed at
25.9–26.6 ms, every lane's guard wait rose to 12.8–15.3 ms (from 8.2–10.9 on
two lanes), and the frame was 72.4 ms median vs 70.8 on perf53. **The batch is
bound by the guard's serialized chain, not by CPU.** Rolled back to perf53.
Runtime SHA-256 `df81b36b…`; receipts in `../three-workers-hardware/`.
Host: 3-worker driver 64/64 plain and TSan; 2-worker standard 40/40.

Implication for the roadmap: more lanes or the owner core cannot shorten the
batch. Only shortening or removing guard holds can (remaining candidates in
section 14), or reducing the per-acquisition cost/handoff latency (about 970
guard acquisitions per frame across lanes; the sleep/poll 50 µs wait mode is
the default, the bounded-mutex mode `XV_OBJECT_TIMED_WAIT=1` is env-only).

## 18. Perf57: guest-phase attribution of the scene half (diagnostic, rolled back)

`XV_PHASE_TIMING_DEFAULT=2` (forced over the device config, which pins
`XV_PHASE_TIMING=0`); phase timing disables object jobs, so tick rows are
serial and the frame ran ≈ 85 ms. Six windows, 360 frames, self time per
frame (`tools/summarize_guest_phases.py`, log
`../phase-timing-hardware/gameplay-forced.log`):

| Scope | calls/frame | inclusive ms | self ms |
| --- | ---: | ---: | ---: |
| `scene_5D410` body (outside child scopes) | 1 | 41.7 | 9.05 |
| `render_54010` ordered callback dispatcher | 11 | 8.1 | 8.13 |
| `render_70110` | 47 | 7.9 | 7.91 |
| `render_63C00` | 114 | 3.8 | 3.13 |
| `render_92890` | 1 | 3.1 | 2.77 |
| `render_5C300` | 84 | 2.2 | 2.19 |
| `render_66510` | 16 | 2.5 | 2.03 |
| `render_51E90` | 238 | 1.8 | 1.75 |
| `render_B5B40` | 502 | 1.5 | 1.49 |
| `render_8D650` | 124 | 1.9 | 1.45 |
| `render_A9330` | 359 | 1.3 | 1.26 |
| `render_5B4A0` self | 16 | 10.8 | 1.05 |
| tick side: `object_update` self 15.95, `object_pose` self 6.49, `tick_driver` self 7.50 (serial config) | | | |

The per-model chain `5B760 → 5B4A0 → A26B0 → A2380` is almost entirely
inclusive (self ≈ 0.5 each); its cost bottoms out in `70110` (47 calls ≈ 170 µs
each, i.e. the draw submission path: draw-HLE is ≈ 9 ms) and `54010`. The
scene routine's own 9 ms body has no finer scope yet. Draw-HLE stage costs
(perf51) remain: streams 3 (post-trust), textures 2.4, indices 1.7, state 1.2,
program 0.8.

## 19. Tick/scene overlap: what a snapshot would have to cover (static inventory)

Over the perf53 generated code (`capture-trust-hardware/build/recomp`):
scene closure (`BCB30`) 1,738 functions, tick closure (`FA920`) 1,209,
849 shared. Absolute guest globals the scene reads that the tick writes: 25,
on nine 4 KiB pages (`0x2E3000`–`0x2E4000` game/object globals, `0x2FA000`
and `0x2FC000` object table and cluster lists, `0x2D2000` visitation epoch,
`0x276000`/`0x270000`, `0x2E8000`, `0x39C000` structure BSP word); 22 of them
are also written by the scene itself. Almost all other shared traffic goes
through pointers from the object table (`0x2FC6AC`) into object records, so
an overlap must snapshot the object records (Halo's game-state region, several
MB) plus effects/particles/decals lists at the tick boundary; the renderer
then reads the snapshot, and the 22 scene-written globals need private
copies. Snapshot exchange primitive: `xk_frame_snapshot.c` (prototype, not
wired). Cost bound: a full game-state copy per frame is several ms on this
CPU; a dirty-region scheme needs write tracking the recompiler does not emit
today. This is the multi-week item behind 20+ FPS.

## 20. Perf58 / 06e8af4: fused-solver guard release — neutral, rolled back

`XV_SOLVER_UNLOCK=1` wraps `ns_solver_fused` with the existing
`xv_object_solver_begin/end` admission. Hardware (480p, three settled
windows): frame 70.6–72.7 ms vs perf53 69–71; batch 26.6–28.4 vs 26.0–26.9;
lane waits unchanged. No admission counter exists for the release, so whether
its private-span checks admitted the fused calls is unknown; either way the
lever's ceiling was ≈1.5 ms. Rolled back to perf53. If anyone revisits it,
first add admit/decline counters to `xv_object_solver_begin`.

**Installed state at 12:25: perf53 (30ff83e), 480p.** Every trial today is
documented above; the honest ceiling of the incremental path in this room is
≈16–17 FPS, and 20+ needs the tick/scene overlap (section 19).

## 21. Perf59 / 76324ac: rendering fix — BORDER texture addressing (installed, slot 0)

Cause found in the perf50 draw trace: the campaign shadow receiver draws
(vs 29 / ps key 18CCED17) sample the 128×128 shadow render target and a
64×64 projection texture with `D3DTADDRESS_BORDER` (11 samplers in the
traced frame), and the runtime mapped BORDER to plain CLAMP, smearing the
map's edge texels wherever projected coordinates leave the map. That is the
mechanism behind the "stretched shadow" (Sept 9) and "oversized, repeated
silhouettes" (Sept 18) reports. Fix: BORDER → `SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER`
(transparent black outside) when the guest border color (texture-state
index 29) is black; otherwise CLAMP with a one-time log. `XV_BORDER_ADDR=0`
restores the old mapping. Runtime SHA-256 in
`../border-fix-hardware/package-check.json`; log confirms
`border addressing: GXM full border (transparent black)`.

Hardware: perf59 loads and plays; checkpoint views show a clean floor with no
smeared or repeated silhouettes; the frame numbers match perf53 (69–73 ms;
one window at 66 ms / 15 FPS). Close-up character-shadow captures are in
`shadow-*.png`; a user confirmation on the device is the final word.

Close-up captures (`shadow-feet2.png`: Marine centred, feet and floor in
frame) show a clean floor with no smear or repeat around the character; that
view runs 17–18 FPS (55–57 ms). Caveat: this room did not visibly reproduce
the defect on perf50 either, so the evidence is the traced mechanism plus a
clean result, not a before/after of a reproduced artifact. Ask the tester
who reported the Sept 9/18 shadows to re-check their scene on perf59.

## 22. Flashlight check on perf59

D-pad Right is Halo's White button (flashlight), so it is drivable through the
remote pad. Captures in `../border-fix-hardware/fl-off.png` and `fl-on.png`
(same view, Marine centred): with the flashlight on, the cone lights the wall
and floor, the cone edge is clean (no smear), and the Marine's body stays
fully visible and lit. The traced lit frame (9044, 67 draws) has three
BORDER-addressed samplers, i.e. the flashlight projection uses the same
addressing path the shadow fix corrected. The September 5 report ("lit cryo
pod body vanishes while the flashlight is on") does not reproduce here;
the pod scene itself was not reachable from this save and stays to be
re-checked by whoever next passes the cryo bay.

## 23. Overlap project: copy-cost gate

The runtime records no game-state bounds; Halo CE's mutable object storage
is a fixed pool (≈2 MB) plus datum tables (objects 2,048 entries, effects,
particles, decals, players; a few hundred KB), so a per-frame snapshot copy
is ≈2–3 MB, i.e. ≈2–3 ms on this CPU: affordable against the ≈30 ms a
tick/scene overlap could hide. The hard parts are unchanged: run the scene
half against the snapshot arena (the recompiled code reads `g_xram` through
globals, so the render thread needs its own arena pointer or TLS), and merge
the 22 scene-written globals plus any render-side datum writes back into the
live state at the frame boundary. First host-testable piece: a fixture that
runs `BCB30` against a copied arena and diffs the live arena afterwards to
enumerate exactly what the render half writes.

## 24. Perf60 / 7eee393: per-frame dirty-page census (diagnostic, rolled back)

Build: `page-census-hardware` (perf53 + border fix + census; version
0.2.0-perf.60, slot 0). The census is one-shot: `POST /trace/pages`
(`tools/vita_remote.py trace-pages`) makes `xd3d.c:census_track()` FNV-hash
every 4 KiB arena page at the next Present (baseline), then again at the
Present after it, and log the pages whose hash differs. Three censuses at the
Pillar of Autumn checkpoint, 480p, ordinary gameplay (`page-census-hardware/
census.log`):

| frames | pages changed (of 17318) | KiB | runs |
|---|---|---|---|
| 5755..5756 | 156 | 624 | 70 |
| 5851..5852 | 111 | 444 | 64 |
| 5949..5950 | 128 | 512 | 69 |

Per-MiB histogram (MiB index: pages), stable across the three samples:
MiB 0-3 (`0x000000-0x400000`, XBE .data/.bss, guest stacks, object table,
game globals) 72-75 pages; MiB 29-31 (`0x1D00000-0x2000000`) 19-22 pages;
MiB 60-61 (`0x3C00000-0x3E00000`) 5-15 pages; MiB 16-17 0-2 pages; MiB 35
had 34 pages in the first sample only (transient). The tag cache
(`0x3A6000`.. 22 MB, MiB 3-25) shows no changes, as expected. Ranges common
to all three samples (the log line truncates the list after ~20 ranges):
`061000-066000 06B000-06C000 093000-094000 095000-096000 0AC000-0AE000
0B3000-0B4000 0C0000-0C2000 0DD000-0DF000 0EC000-0ED000 0EF000-0F0000
113000-115000 141000-142000 149000-14A000 1BF000-1C1000 1C6000-1C7000
205000-206000`.

Caveats: a hash census sees net content change between two Presents only
(a write that restores the old value is invisible), and each scan stalls the
frame ~690 ms (17318 pages), so the game may have run more than one tick
between baseline and diff: the numbers are an upper bound per tick, one
scene. It is a checkpoint-room sample; heavier scenes (more objects, effects)
will change more pool pages but the region set should hold.

What it means for the overlap design (§19, §23): the per-frame mutable set is
~0.5 MB, four to six times smaller than the ~2-3 MB estimate, and it is
confined to three regions. A snapshot does not need write tracking: copying
the whole XBE data+bss span (`0x61000-0x260000`, ~2 MB) plus the MiB 29-31
pool region (3 MB) plus MiB 60-61 (2 MB) is ~7 MB/frame, ~5-7 ms memcpy on
this CPU, which is already too much; copying only the pages that ever
changed in a census (~160 pages after a warm-up census, 0.6 MB) is <1 ms but
needs a learned page list with a periodic re-census to catch new pages.
Recommended: learned dirty-page list (union of several censuses, plus every
page in the object table / game-state pool bounds once those are located),
refreshed by a background census every N seconds. Per-page hash cost is ~40
us; a 200-page verification per frame is ~8 ms, so verification must be
sampled, not per frame. Next concrete step remains the host fixture from
§23 (run `BCB30` against a copied arena, diff afterwards), now with the
census page list as the copy set.

Device after this section: rolled back to perf59 (slot 1 -> 0 as
`rollback` reports), campaign relaunched to the checkpoint.

## 25. Perf61 / d5f2703: TPIDRURW probe — per-thread page table is viable

Why: every guest access in generated code goes through `g_xpt` (virtual page ->
arena offset, `X_G` in `recomp/xv_x86rt.h`). A render thread that owns a
*second* page table whose entries for the mutable pages point at shadow copies
gets a frozen, copy-on-write view of the world at the cost of copying only the
dirty pages (§24: ~0.5 MB/frame). The blocker was how a thread finds "its"
page table cheaply: `__thread` on vitasdk is emutls (a function call per
access, see `scratchpad tls.c`), unusable in 8,062 generated functions.

Probe (`runtime/xv_tpidr_probe.c`, `XV_TPIDR_PROBE=1`, stage
`tpidr-probe-hardware`, boot log `boot.log`): the ARM user read/write
thread-ID register TPIDRURW (CP15 c13,c0,2) starts at 0 on every thread
(unused by the system), accepts writes from user mode, and the kernel
preserves it per thread: 4 threads pinned to cores 0/1/2/any, 400 checks each
across 500 us sleeps, busy spins, preemption and 40 forced core migrations,
0 mismatches; the main thread's value survived the whole probe. TPIDRURO
(c13,c0,3) holds a distinct stable per-thread kernel value (0x8299e800,
0x8299f800, ... the kernel TLS block), also usable read-only as a thread key.

Codegen: a non-volatile `mrc` asm is pure to GCC and gets hoisted, so
`X_G` reading the table pointer from TPIDRURW costs one `mrc` per function
(scratchpad `hoist.c`: one `mrc` for a whole load/store loop). Verdict:
**a per-thread page table is one instruction per function, no generator
change** (the header macro is enough; generated code's cached `xpt_` local is
unused by `X_G`).

Plan (build flag `XV_THREAD_PAGE_TABLE`):
1. perf62: `X_PT` = TPIDRURW under the flag; every thread the runtime creates
   binds TPIDRURW to the live table through a `sceKernelCreateThread` /
   `sceKernelStartThread` wrapper (trampoline, no per-site edits); main binds
   at start. No behavior change; proves cost (frame time vs perf59) and that
   no thread is missed (a missed thread faults at once, log rotation keeps
   the crash log).
2. Shadow region appended to the arena (inside the GXM mapping; 240 MB free
   main after gfx), a render page table, the §24 dirty list, boundary copy.
3. Render thread running the scene half (`BCB30`) on its own table while the
   next tick runs on the owner + workers; merge the 22 scene-written globals
   back at the boundary. Core budget: 3 user cores (render, owner, one
   worker) so the object batch loses a lane; expected frame ≈ max(tick ~40,
   scene ~33) ms ≈ 25 FPS against 70 ms today.

Device after this section: rolled back to perf59, campaign relaunched.

## 26. Perf62 / d9a27bb: per-thread page table on hardware — free (installed, slot 0)

`XV_THREAD_PAGE_TABLE=1` (e8f497f + pins/Makefile fixes): `X_PT` in
`recomp/xv_x86rt.h` reads the thread's TPIDRURW; `runtime/xv_thread_bind.c`
routes every `sceKernelCreateThread` (force-included header macro) through a
trampoline that binds the new thread to the live table before its entry runs;
`main` binds itself; `g_xpt` storage is static (fixed address before the arena
exists). 92,538 `mrc` sites in the ELF (one per function, hoisted). Generator
pins updated: `tools/query_f32_primitives.py` X_G text,
`tools/query_memory_capture.py` private `xv_x86rt.h` hash.

Hardware (stage `thread-pt-hardware`, checkpoint, 480p): boot, dashboard,
menus, level load and gameplay all run; no `[thread-bind]` misses.
Frame: 68.0 ms median (windows 66.3/68.8/69.0/67.3), batch 25.7, draw-hle 8.0
vs perf53 70.8 / 26.5 / 9.2 and perf59 ~73. Cost of the indirection: none
measurable. perf62 stays installed (slot 0; perf59 in slot 1) as the base for
the shadow-page snapshot. Observer script now takes `XV_EXPECTED=<version>`.

Next increments (see §25 plan): (A) shadow region + render page table +
learned dirty-page list + boundary copy, still single-threaded (scene runs on
the owner bound to the render table, full copy-back after) to prove the
mechanics and cost; (B) scene body on a render thread with the owner waiting
(thread portability of the scene half: D3D state, visibility/worker jobs,
pose pipeline all assume the owner); (C) real overlap with deferred Present
and byte-level merge of the 22 scene-written globals.

### 26a. Correction: perf62 did not exercise generated code

The generated shards `#undef X_G` and redefine it over a per-function cache
`xpt_ = g_xpt` / `imgb_ = g_img_base` emitted by the recompiler prologue
(`recompiler/xita_recomp.py` ~line 1293), so perf62's generated code still
translated through the live globals; only hand-written kernel/runtime code
used TPIDRURW. The "free" result is therefore only the hand-written share.
Fixed in 704a162: the prologue caches `X_PT` / `X_IMG_BASE`, one `mrc` per
generated function under the flag, identical code without it. The shards
are not tracked (game code); a stage must re-run `tools/recomp.sh` (venv
python with iced-x86) after any recompiler or `games/halo_ce_3925/hooks.py`
change, then `make`. perf63 (render view) is the first build with the
per-function `mrc` in generated code, so its frame time includes that cost.

## 27. Generated shards are hand-maintained; install hooks selectively

Whole-game regeneration (`tools/recomp.sh`) is not usable on the stages: the
Sep 17 `recomp/code_*.c` carry patches installed by Codex's selective tools
(`tools/gen_owner_phase_hooks.py`, `gen_scene_partition_hooks.py`,
`gen_visibility_portal_hooks.py`, the "eleven bucket1 cuts"), and Makefile
gates (e.g. line ~1308, `XV_SCENE_BUCKET1_DETAIL requires the selectively
regenerated eleven bucket1 cuts`) refuse shards without them. A fresh
regeneration also tripped `model_route_profile.child_hook`'s body pin because
the pose-scope observer (efb502b) was added after the pin; fixed in 4ac22b7
(the gate strips it like the phase scope). Regenerated shards differ from the
maintained ones only by the missing selective patches (phase scopes etc.).

So generated-code changes are installed as text patches on the maintained
shards: `tools/patch_render_view_hooks.py <stage>/recomp` (idempotent) adds
the thread-table preamble to every shard and the `XV_RENDER_VIEW` scope to
`f_000BCB30`, in the same style as the other observers (before the caches).
The recompiler/hooks.py changes (9848f30, 7a04751) remain the source of
truth for a future whole regeneration. Keep a pristine copy of the maintained
shards (`thread-pt-hardware/build/recomp/code_*.c` == Sep 17 originals).

## 28. Perf63 / render view on hardware: mechanics work, first cut too slow, level-load deadlock

Stage `render-view-hardware` (patched shards, §27), `XV_RENDER_VIEW=1`.
First cut (7a04751 + 1ffb7cd configure-in-boot): the scene half ran on the
render table every frame, the merge landed its writes, no faults. Numbers in
the main menu (learning had started at frame 2, before gameplay): 195 shadow
slots, image span 550 pages (2.2 MB, "lowest dirty page to end of image"),
copy-in 24.8 ms/frame for 2.9 MB (two copies: shadow + pristine), merge
45.7 ms/frame (memcmp 2.9 MB + word loop), scene wrote 25 physical pages and
~9 image pages per frame. So: (a) the image span heuristic is far too wide,
(b) memory throughput is ~120-240 MB/s for this pattern, not ~1 GB/s. A
throughput bench now runs at configure (`[render-view] ... throughput`).

Deadlock (user: "I think it froze"): confirming the difficulty menu started
the level load; guest thread 8 (loader, `start 00015C5A`) spun at
`eip 0005846B` in a yield storm for 7 minutes while the loading-screen scene
sat inside BCB30 on the render table. The loader waits on a flag the scene
writes; the write was in the shadow page and would only be merged when
BCB30 returned, which waits on the loader. Recovery needed a manual app
close (remote restart cannot run while the game thread is hung); the
accepted perf62 update applies on the next launch.

Fixes (1201ae1, <yield-drop commit>): learning is armed only after 30 frames
whose tick gap (scene exit to next scene entry) exceeds 8 ms, i.e. gameplay,
or `XV_RENDER_VIEW_LEARN_NOW=1`; image dirty pages are a list plus a
rolling refresh of 32 unlisted .data-tail pages per frame; pristine copies
and the merge cover only the scene-write set (full frames: first 30 active,
then every 30th) so per-frame traffic is ~1x the dirty set; and
`xk_os_fiber_switch` calls `xv_render_view_fiber_switch()`: a guest fiber
switch while the scene is bound publishes the writes and finishes the scene
on the live table (`yield drops` in the report). If gameplay scenes yield
every frame, the view is never effective and that counter says so.

## 29. Render view: two more hangs, in-place mode, watchdog build ready (undeployed)

Run 2 (per-thread table, gameplay-armed learning): learn pass 1 at frame
5370 (150 pages, 129 slots, 21 image pages), then the first viewed frame spun
forever at `eip 000540E3` (thread 8, yield storm 417k/3 s). Cause: native
helper threads write results into guest memory through their own (live)
table; a scene bound to a separate table never sees them. Throughput bench:
memcpy arena->arena 197-239 MB/s, arena->heap 278-314, heap->heap 244-283,
newlib memcmp ~86 MB/s. Plan for the number: a 0.6 MB dirty set costs ~3 ms
to copy in, plus the merge.

Run 3 (in-place mode, 774712d: the LIVE table's entries for the dirty pages
are retargeted to the shadow copies and the flat image base swapped for the
scene's duration, so every thread sees one view): learn pass 1 at 5357 (154
pages, 133 slots), then the first viewed frame spun at `eip 00012AA9` (a
low-level wait helper; stack shows tag-cache alias 803A6024 and 80BAxxxx
buffers). Class: the guest polls a word that host-side code (D3D fences /
push buffer / audio) writes through a cached live pointer. Both runs needed a
manual app restart.

Built, not deployed (7e119a3, stage `render-view-hardware/build/xita.vpk`,
`run-perf63-fixed.sh` staged with campaign-launch4): shadow only physical
pages below `XV_RENDER_VIEW_PHYS_LIMIT_MIB` (default 4: the game-state region;
D3D/audio buffers at MiB 29-31/60-61 and guest stacks stay live),
`XV_RENDER_VIEW_IMAGE=0` toggle, one-pass word merge, and a watchdog on the
remote `/status` handler that restores the live mapping and disables the view
when a scene has been bound over 3 s (my polls hit /status every 5 s), so a
hang no longer needs a manual restart. If the 4 MiB limit still hangs, the
next split is `XV_RENDER_VIEW_IMAGE=0` (image .data live) to separate the
image-global hypothesis from the physical one.

Device state at 16:55: frozen on the third run (frames 5379), awaiting a
manual restart; slot 0 = perf63 run 2 build, slot 1 = perf63 run 3 build.
perf62 is no longer in a slot: redeploy `thread-pt-hardware/build/xita.vpk`
to get back to the known-good baseline.

### 29a. Run 4 (7e119a3, 4 MiB limit + watchdog) and the spin site decoded

Same first-viewed-frame spin at `12AA9` with 75 slots. Decoded: `f_00012AA3`
is `SwitchToThread` (NtYieldExecution, returns eax != 0x40000024). Callers on
the stack: `f_00032B00` polls a request-table entry (`[[2E2D24]+34] + i*12 + 2`,
a status byte) and `f_000325C0` waits on a flag with a 0x84-tick timeout:
Halo's cache-file request wait. The scene issues a cache/data-file request
and spins until another guest thread (the streaming thread, `start 33AF0`,
sleeping/polling) completes it. The watchdog fired after 30 s but only
restored the mapping without merging, so the request the scene had written
into its shadow page never reached the live table and the spin continued;
fixed in d730388 (watchdog merges first). Why the request is not completed
while both threads share the in-place view is still open: the completion may
come from host-side file HLE (async read on a host thread) writing through a
pointer translated before the retarget, or through an alias the reverse map
does not cover. Next: reproduce in Vita3K (`tools/vita3k.sh`, env.txt
`XV_LEVEL=a10 XV_RENDER_VIEW=1 XV_RENDER_VIEW_LEARN_NOW=1`) where a hang
costs nothing, then instrument the request path. Device redeployed to
perf62 (thread-pt-hardware/build/xita.vpk) so it stops freezing.

### 29b. Vita3K reproduces the wait but resolves it; remote env knob

Vita3K cannot run `XV_THREAD_PAGE_TABLE=1` builds (dynarmic: "Unhandled CP15
MCR CRn=13 CRm=0 opc2=2"); the in-place render view needs no per-thread
table, so stage `render-view-vita3k` builds with `XV_THREAD_PAGE_TABLE=0`
(version 0.2.0-perf.63e). Emulator env.txt `XV_LEVEL=a10 XV_RENDER_VIEW=1
XV_RENDER_VIEW_LEARN_NOW=1` boots straight into the level; scratchpad
`v3k-play.sh` presses Launch Game with XTest (Cross = key x).
Result: the view ran 185+ gameplay frames without hanging: copy-in
0.2 ms/frame (950 KiB, emulator memory is 8-10 GB/s), merge 0.06 ms, the
scene wrote only 4 physical pages + 14-16 image pages per frame, and the
scene yields to other guest fibers ~1x per frame. The same spin as on
hardware appears (thread 8 at 12AA9 <- 32B60, request-table wait) but ends
when "APC queued on thread 12: FE0000E8(00032EC0, ...)" — the file HLE's
I/O-completion APC on the streaming thread. On hardware that completion
never arrived. New in this revision: `POST /env?K=V&...`
(`vita_remote.py env K=V ...`) sets env before Launch, so hardware bisects
(`XV_RENDER_VIEW_IMAGE=0`, `XV_RENDER_VIEW_PHYS_LIMIT_MIB=0`) need no
rebuild; the render view logs whether the request table page is shadowed
on the first viewed frame and on a watchdog trip.

## 30. Bisect 1: physical-only render view runs on hardware; D2Vita lessons

`run-bisect.sh physonly XV_RENDER_VIEW_IMAGE=0` (312f1be+ build, env set
through the new endpoint before Launch): the in-place view with only the
game-state physical pages shadowed (79 slots, image data live) ran ~3,000
gameplay frames at the checkpoint without a hang or watchdog trip, rendering
correctly (screen-physonly.png: Marines, corpses, HUD). So the hangs come
from the image view (swapped flat image base): host-side writers reach image
globals through live pointers. Cost: copy-in 1.3 ms/frame (316 KiB), merge
1.4-2.2 ms/frame (~35k words per 60 frames), scene-write set grew 25->40
slots; the scene switches guest fibers 8-17 times per frame (the streaming
and sound threads run inside the scene). The frame-time comparison with
perf62 is invalid (different view: 390 vs 150 draws/frame); a same-launch
`XV_RENDER_VIEW=0` run is in progress for the cost.

D2Vita (Box86-derived dynarec for Diablo II, github.com/Franckrst/D2Vita,
docs at franckrst.github.io/D2Vita/gains/): their measured wins were GPU
async submission +52% (28.8->43.7 fps; overlapping ~11 ms of submission with
the next frame), fork-join with a single worker +12%, native hooks chosen by
gain ≈ N × (T_guest − T_native − 69 ns) at high-frequency call sites, CPU
ring traversal +11.5%; native memcpy and 14 of 15 dynarec micro-opts gave
nothing. For Xita the analogue is a D3D command queue with a submitter
thread: the owner records draws cheaply and a render thread does the
GXM state/uniform/draw work (`draw-hle` 8 ms at the checkpoint, 24 ms in
the heavy corridor view) on a core the object workers leave idle during the
scene. That is host-side overlap: no guest snapshot, no image view, no
fiber problem. Recommended next increment over the guest snapshot.

## 31. Increment A complete: full render view runs on hardware, cost 4-5 ms/frame

Root cause of every image-view hang (§28-29): in-place mode retargeted all
933 image pages to the copy but merged only listed pages back, so a write to
an unlisted image global during the scene (e.g. a pending cache-file request
marker) was lost. Fix (9460b36): under `XV_RENDER_VIEW`, `X_IMG*` translate
through the page table (header, recompiler preamble and
`tools/patch_render_view_hooks.py` preamble, which now replaces an older
preamble), only listed image pages are retargeted, no flat-base swap, no
rolling refresh. Physical pages already worked that way (§30).

Bisect 2 (`run-bisect.sh fullview XV_RENDER_VIEW=1`): ~4,400 gameplay frames
at the checkpoint, no hang, no watchdog, 155 draws/frame like the view-off
run of the same launch. Steady state: 79 slots + 30 image pages (436 KiB),
copy-in 1.9 ms/frame, merge 2.3 ms/frame (~60k words per 60 frames),
scene-write set 27 slots + 20 image pages (3 found late), 7-12 fiber
switches per frame inside the scene. Same-view cost (view off -> on):
70.2 -> 75.3 ms median (+5 ms), tick 36-38 both, scene 31-33 -> 32-34.

Caveats: the checkpoint save advances between launches (the earlier
"physonly" run landed in a post-fight state with 386 draws/frame; compare
only runs with equal draws/frame, `run-bisect.sh` now screenshots each run).
Learning stalls ~1.4 s six times per session; a shipped version needs a
cheaper learner. Fiber switches inside the scene (streaming, sound) mean a
render-thread scene (increment B) must host those fibers or their waits.

Status: the snapshot substrate for the tick/scene overlap exists and is
measured. Next: increment B (scene body on its own thread, owner waiting)
then C (overlap, deferred Present, merge against a concurrent tick using the
pristine copies). Device left on perf62 (fastest known build); perf63
`render-view-hardware/build/xita.vpk` (9460b36+) is the render-view build,
off by default at runtime only via `XV_RENDER_VIEW=0` (build default 1).

## 32. Increments B and C: design notes from what A revealed

The guest scheduler is a single-runner cooperative model: one guest fiber
(a Vita thread each) runs at a time; `xk_os_fiber_switch` signals the
target's semaphore and parks the caller. The scene (thread 8, BCB30) hands
control to other fibers 7-12 times per frame (streaming `start 33AF0`,
sound, the two event waiters). Consequences:

B (scene on a helper Vita thread, owner parked): the fiber identity is the
semaphore, not the Vita thread, so a helper thread that runs BCB30 with a
copy of `c` on the same guest stack effectively *is* thread 8 while the
owner waits on a private semaphore; yields inside the scene keep working
(the helper parks on thread 8's semaphore and is the one woken). Needs: the
helper bound to the live table (in-place view needs nothing per thread),
`xv_object_is_worker_thread`/"presenting owner" checks taught about the
helper, D3D HLE reentrancy audited (single caller at a time, so fine), and
the phase/owner-phase observers (they key on thread id). Gain 0; proves
portability. Estimated 1-2 days including hardware runs.

C (overlap): tick N+1 on the owner while scene N runs on the helper. Two
guest fibers "running" breaks the single-runner scheduler, so the scene
must not enter it: `SwitchToThread` inside the scene returns
NO_YIELD_PERFORMED, event/semaphore waits inside the scene block the helper
only (the owner-side scheduler still services the streaming/sound threads
during the tick's own yields), the scene's cache-file requests are
completed by those threads as today. Present is deferred to scene
completion (the owner's Present HLE marks the frame and returns), the next
scene entry waits for the previous scene (backpressure), and the merge at
scene end applies copy != pristine words onto pages the tick may have
changed meanwhile (already the merge's semantics). Pages both halves write
in the same frame need an ownership rule (the 22 scene-written globals of
§19 first). Expected frame ≈ max(tick ~37, scene ~33 + 5 view) ≈ 40 ms
(25 FPS) at the checkpoint; 30 FPS needs the tick itself under ~33 ms as
well. Estimated 1-2 weeks including the debugging the hardware will demand.

## 33. 500 MHz: kernel module, unsafe SELF, remote /put; PSVshell is what reaches 500

Facts: the user-side `scePowerSetArmClockFrequency(500)` returns 0x802B0000
(cap 444). Xita has requested 500 since before the takeover and fell back.
`third_party/xita_clock` (build.sh) is a kernel module loaded from
`ux0:data/xita/module/xita_clock2.skprx` with
`taiLoadStartKernelModuleForUser` at game start (`xv_configure_cpu_clock`):
module_start reads {mhz, pid}, applies the clock with
`kscePowerSetArmClockFrequency`, and a kernel thread re-applies it every
500 ms while that pid lives; `xita_clock_bind` (syscall) re-arms a resident
module on later launches (a syscall import to a module that is NOT resident
at app load crashes when called, so the game calls it only on the
"already loaded" path). Requirements learned the hard way: HENkaku "Unsafe
Homebrew" AND an unsafe SELF (`XV_UNSAFE_SELF=1`, Makefile) — a safe SELF
gets taiHEN 0x90010009; a changed module must get a new name (XitaClock2:
the resident old copy answers "already loaded" and its exports differ).
Remote: `POST /put?name=` (`vita_remote.py put <file> <name>`) writes
<= 64 KiB under `ux0:data/xita/module/`. Stage `clock-hardware` (perf64,
perf62 + this). Result: module loads (id 4001017F) but the clock stays 444:
ScePower's kernel setter also caps at 444. PSVshell gets 500 by resolving
`SceLowio` export `ScePervasiveForDriver_0xE9D95643` (mul:div 15:16), NOP-
injecting a check at +0x1D in it, calling it, and writing 500/15 into
ScePower data segment 1 offsets 0x41C8/0x41CC (Electry/PSVshell src/main.c,
oc.c). The user has PSVshell installed (overlay in their screenshots), so
the practical path is its profile (SELECT+UP, CPU row, RIGHT to 500, save).
Replicating the patch in xita_clock is possible (vitasdk has
libtaihenModuleUtils_stub, libScePervasiveForDriver_stub) but is a kernel
patch with firmware-specific offsets; only worth it if 500 measurably helps.
The 4th core: reserved by the OS for system use; PSVshell's overlay shows
it at 14-21%; user apps get masks for cores 0-2 only.
Loading screen (user report): the blue effect fills the screen and should
be masked; captures in clock-hardware/load64-*.png; not yet bisected (Sep 18
qualified build at 2026-09-18-tester-release/release/xita-0.2.0-test.1.vpk,
script loading-bisect/run-loading.sh).

## 34. Sept 21: perf81 inline-stack candidate installed; perf82 scene-thread port built

Resumed from Codex's return notes (perf80 / 219c50c installed, lowest graphics
settings left on the device, heavy corridor 115-129 ms). Attribution from
`../external-clock-candidate/lowest-settled.log` per frame: tick FA920 51.6 ms
(object batch 36.5 inside), scene BCB30 64.1 ms (first section 30.8 incl.
5B760 model interval 9.3 with 5B190 model packets 6.4; ordered passes 23.6
incl. the 5B710 second model pass 13.0), draw-hle 14.9 nested. No shaving
reaches 20 FPS here; overlap of tick and scene is the structural route and
alone lands near 14 FPS in this view.

perf81 / 67ce01b+ (`../inline-stack-candidate`, cloned from the retained
perf80 stage; only `recomp/xv_inline_stack.h` and the Makefile block
`XV_RENDER_INLINE_STACK=1`, scoped to the two -Os rendering units): material
unit code_011 text 1,452,216 -> 1,477,764 bytes, zero `bl x87_push/pop`
sites. Package differs from perf80 only in game-a.self/boot-game.txt
(31,000,106 bytes, SHA-256 99b43fa8...). Installed slot 0, boot-confirmed;
env re-armed (XV_CPU_EXTERNAL=1, shader override/loading trace/capture
detail 0, retention 1); campaign sequence reached gameplay. settled.log last
six windows: game 108.1-115.8 ms (8.5-9.1 FPS), 276-291 draws, draw-hle
11.3-12.1 ms, FA920 51.0, BCB30 55.7 ms/frame. Same lowest settings; live
NPC windows, so no precise gain claim, but draw-hle and scene are lower with
slightly more draws than perf80's windows.

perf82 / `../scene-thread-candidate` (built, not yet deployed): increment B
ported as a narrow patch onto the retained perf80 stage (xk_scene_thread.c/h,
xv_owner_thread_id alias in xk_os_vita.c, owner-check substitutions in
object jobs / collision / clip / polygon-edge controls / owner-phase / log,
BCB30 entry hook inserted into the retained shard, Makefile XV_SCENE_THREAD
block, XV_SCENE_THREAD_DEFAULT=0 so it is env-enabled only). Package differs
only in game-a.self/boot-game.txt (30,972,474 bytes, SHA-256 5fc1faaf...).
Bin/rg shim is required by the retained Makefile's `$(shell rg ...)`.

## 35. perf82 on hardware: the scene half runs on a helper thread (increment B proven)

perf82 / 67ce01b+ installed slot 1 (perf81 remains slot 0), boot-confirmed;
env re-armed with XV_SCENE_THREAD=1 plus the usual five before Launch. The
campaign sequence loaded normally; over ~7,800 frames (132 report windows,
menus + loading + gameplay) every BCB30 was dispatched to the helper thread
(dispatched 60/60 per window), owner wait 59-69 ms/frame in gameplay = the
scene time, zero nested declines, zero yield storms, zero aborts, no
watchdog. gameplay.png: corridor, grunts, reticle, pistol, HUD intact;
overlay shows core 1 at ~75% (the helper) with the owner core idle during
the scene. Frame time 111.7-121.0 ms at 278-308 draws (perf81 108-116 at
276-291): no gain, as expected for a no-overlap step, and no measurable
cost beyond noise. The owner-phase BCB30 observer reports 0 entries under
the helper (its begin path is keyed to the owner thread before the alias
applies); the scene-thread wait line replaces it.

Installed state: perf82 with XV_SCENE_THREAD_DEFAULT=0, so ordinary launches
behave like perf81 unless `vita_remote.py env XV_SCENE_THREAD=1` is sent
before Launch (process-only, per Codex's note).

Next (increment C, the overlap): owner continues into tick N+1 while the
helper runs scene N. Required pieces, in order: (1) port the in-place
render view (§31) onto the retained stage as the scene's frozen inputs;
(2) scene-side scheduler isolation: SwitchToThread inside the scene returns
NO_YIELD_PERFORMED and event waits block only the helper, while the owner's
scheduler services the streaming/sound fibers during the tick (perf82
showed the scene switching fibers 7-12 times per frame, so this is the
risky part); (3) deferred Present: the owner's Present marks the frame and
returns, the helper presents at scene end, and the next scene entry waits
for the previous scene (backpressure); (4) merge ownership for fields both
halves write in one frame, starting from the 22 scene-written globals of
§19. Expected in the heavy corridor: max(tick ~51, scene ~60 + view ~5) ≈
65 ms (~15 FPS); 20 FPS additionally needs scene work below ~45 ms (model
passes 19 ms and draw recording 12 ms are the targets). Neither is done.

## 36. No-hardware development: the whole-game host harness works again

`tools/host_build.py --stage <retained stage>/build --commands make-n.txt --out <objdir> [--cc gcc]`
builds `recomp/host/harness` with each unit's exact Vita feature defines,
taken from `make -n -B build/xita.elf <build-command args>` run in the stage
(needs the bin/rg shim on PATH; `make-n.txt` lists 117 compile commands).
Host-only pieces: Vita OS/net/trace-stub units skipped; `xk_os_host.c`,
`host/{harness,trace,softgfx,runtime_stubs,host_reports}.c` added;
`__arm__`-guarded fallbacks for the palette NEON/FPSCR helpers and the
generated query FPSCR helpers (ARM output unchanged); the harness now binds
the image base and configures the owner-phase observer; `host_reports.c`
calls the kernel's 60-frame report functions every 60 presented frames.
Run: in `<stage>/build/recomp`, `XV_LEVEL=a10` plus the Vita cfg keys as
env (XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OBJECT_JOB_WORKERS=2 XV_OWNER_PHASE=1
XV_THREADS=1 XV_VERTEX_WORKER=1 XV_VERTEX_REFERENCES=1
XV_NATIVE_OBJECT_BASIS=1 quality knobs 0 XV_VERTEX_CAPTURE_RETAIN=1),
`harness halo_image.bin <haloce dir> <save dir>`; the Vita3K copy of the
game data works. On x86-64 (-O1) it boots straight into a10 headless at
~28 presented frames/s; the vertex-capture/runtime reports do not exist
here (runtime/ is Vita GXM code). Same builder targets an ARM Linux GCC on
a Raspberry Pi (native compile; no cross-compiler on this PC). Host and Pi
numbers are for attribution and correctness only, never Vita frame times.

## 37. Host harness reaches a10 gameplay: it was the main menu, not a stall

Correction to §36: the harness does not "boot straight into a10". With no
input the game sits in the main menu (ring camera loop, constant "68
Begin/End, 809 SetVertexData", `game_globals loaded 1 active 1` because
ui.map is a scenario too). The earlier hypothesis that the frame-pacing
loop (0xBB060) spun because no vblank thread existed was wrong: the game
registers its callback through SetVerticalBlankCallback, `vblank_thread`
fires at 60 Hz real time, and `XV_HOST_CLOCK_TRACE=1` (host_reports.c)
shows the 64-bit vblank count at 0x1F8C80 tracking the frame-end target
at 0x2E3660 with the game presenting 30 frames/s. `XV_LEVEL=a10` is the
default and a no-op (it only patches the mission-1 table for other codes).

The host pad script (`XV_PAD`, xk_os_host.c) walks the menu:
`150:a,300:a,450:a` -> Campaign, new game, difficulty; a10.map loads at
about frame 500 (loading screen "2 Begin/End, 34 SetVertexData", loaded
0), the level is up at ~720 and the cryo-bay cinematic (`director on 1`)
at ~780; the cinematic plays out on its own and hands over to the player
camera (the run reached 986-draw frames in the ship corridors within five
minutes). `tools/host_run.sh [seconds] [K=V...]` wraps this (HOST_OBJ =
the host_build.py --out dir, HOST_LOG, HOST_PAD, HOST_GAME, HOST_SAVE).
zsh does not word-split unquoted variables: pass env lists as arrays or
through the script, or XV_LEVEL swallows the whole list (level-select log
line shows it).

What the host gives now, in a10 gameplay: both object-worker lanes run
(`[object-jobs] 60 frames passes 72 batches 75 jobs 4392 lanes 1801/2591`,
`[job-split]`, `[query-unlock]`, `[typed-query]`, `[object-quat]`,
`[object-lock-site]` with host PCs), object-pass scope elapsed, and the
whole 60-frame report set. x86 timing is ~60x Vita (a 72-pass window
spends 40 ms in batches where the Vita spends ~2.6 s), so the harness is
for structure, counters, correctness and deadlocks (increment C merge
ownership, scene scheduler isolation, deferred Present), not frame times.
Vita-only runtime reports (vertex capture, frame time, GPU) do not exist
here. Object-lock-site PCs are host addresses (addr2line on the harness).

### 37a. Raspberry Pi cross build is ready before the Pi is

Arm's prebuilt Linux toolchain is unpacked under
`~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf`
(no root needed). `tools/host_build.py` gained `--static` and, for a
32-bit ARM compiler, adds `-marm -march=armv7-a -mfpu=neon -mfloat-abi=hard`
(the Vita's own -mcpu/-mfpu are dropped from make-n, the A72 runs
A9-class code). The full cross build of the retained perf80 stage takes
about two minutes on this PC and links a 40 MB static armhf ELF:

    python3 tools/host_build.py --stage ../external-clock-candidate/build \
        --commands make-n.txt --out <objdir> --static \
        --cc ~/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc

A copy sits in `../pi-bench/harness-armhf-static-<sha>`. It is UNTESTED
beyond linking: this PC has no qemu-user, so the first run happens on the
Pi (32-bit Raspberry Pi OS, scp the binary, halo_image.bin and the 1.8 GB
haloce directory, then `tools/host_run.sh` with HOST_OBJ pointing at the
directory holding it). Static linking means the Pi's glibc version does
not matter. First things to check on the Pi: it boots to the menu, the
XV_PAD script reaches a10, the NEON/FPSCR `__arm__` paths (xk_palette.c,
query_world_run.h) behave, and whether TPIDRURW survives context switches
under Linux (it should; then XV_THREAD_PAGE_TABLE builds can run there).

## 38. Increments A and B run on the host; the overlap stage and the scene-yield census

Stage `../overlap-candidate/build` = scene-thread-candidate (perf82) plus
the render-view series (7a04751..9460b36 as one diff: xk_mem.c,
xv_x86rt.h, xk.h, xd3d.c, xk_os_*.c, Makefile, runtime.mk; xd3d.c's
Present report hunk hand-applied next to the scene-thread report),
xk_render_view.c/.h, the shard hooks (tools/patch_render_view_hooks.py:
32 preambles, 1 BCB30 hook), the host `__arm__` guards from the retained
stage, and `runtime/main.c` calling xv_render_view_configure() after the
owner-phase configure (the series never wired it; the harness now does the
same). Host list: `make -n -B build/xita.elf ${=ARGS} XV_RENDER_VIEW=1
XV_SCENE_THREAD=1` (zsh: `${=ARGS}` or the list is one argument and make
says "Nothing to be done"), 97 units.

Host results, a10 from menu to corridors, 6,460 frames per 4-minute run,
identical camera path in every configuration:
- XV_RENDER_VIEW=1: every scene under the view, learn 6 passes, 31 slots
  + 25 image pages listed, merge 1-5k words/frame, 21-22 scene writes
  "found late" (caught by full frames), 0 overflow, 0 watchdog. Learning
  arms in the menu on the host (the 8 ms tick-gap heuristic trips at 30
  fps pacing); harmless, full frames grow the set in gameplay.
- XV_SCENE_THREAD=1: xk_scene_thread.c gained a host port (pthread +
  POSIX semaphores). Host fibers are ucontext on one thread, so the helper
  becomes the single runner while it runs the scene, exactly the Vita
  semantics ("the helper is thread 8"). Host owner checks were aliased
  like the Vita ones: pthread_self() -> xv_owner_pthread_self() in
  xk_object_jobs.c, xk_owner_phase.c and the three control files.
  60/60 dispatched per window, owner wait 15-45 ms/frame on x86 (the
  scene's cost here includes the software rasterizer), 0 nested declines,
  no hang. A+B together: same.

Scene-yield census (`[scene-yields]`, reported with the scene-thread
line; hook in xk_yield): every scheduler handoff taken by the scene's own
guest thread while the body runs on the helper, keyed by the XAPI
wrapper's return site and its caller ([ebp+4]), split blocking/yield.
First cut counted every fiber that ran on the helper (the vblank fiber's
sleep showed as "eip 0, 60 blocking per 60 frames"); it is now restricted
to the scene guest thread. Wrapper sites: 12D9C = WaitForSingleObject
(NtWaitForSingleObjectEx), 12E6E = Sleep (KeDelayExecutionThread), BD97C
= preempt yields at BCB30's top level (X_PREEMPT -> xv_preempt ->
xk_yield; harmless to drop under the overlap). Present is issued by
f_000BC78B, outside BCB30, so "deferred Present" is an owner-side change
to the Present HLE, not a scene-body change. host_reports.c also requests
the kernel's `[wait]` dump each report (blocking sites of all threads).

## 39. Increment C on the host: the overlap works, with two design corrections

All in `../overlap-candidate/build` (host list make-n-overlap.txt, 97
units) and mirrored into this checkout; every mode is opt-in by env.

Findings that shaped it:
- The scene-yield census (§38), restricted to the scene's own guest
  thread: menu, load and gameplay, 6,400 frames, the thread takes only
  preemption yields inside BCB30 (site BD97C, X_PREEMPT at the body's
  top level), never a WaitForSingleObject, Sleep or SwitchToThread. So
  "scheduler isolation" is just: the helper never enters the scheduler.
- Owner-side D3D calls outside the scene in gameplay: Present only, plus
  D3DResource_Register/IsBusy on texture loads (`[scene-owner-d3d]`).
  The menu and loading screens are drawn by the owner outside BCB30.
- Present is issued by f_000BC78B (not in BCB30); BCB30 is `ret 8` with
  one 8-byte double argument by value, so an emulated return is
  `esp += 12` and the body can run on a private stack.

Mechanism (xk_scene_thread.c, both ports; xd3d.c; xk_thread.c; xv_x86rt.c):
- XV_SCENE_OVERLAP=1: dispatch, copy the top 16 stack bytes onto a private
  256 KiB guest stack, emulate the return, the owner continues; Present
  joins the scene in flight (the frame is unchanged, only the post-scene
  owner work overlaps). XV_SCENE_OVERLAP=2: Present defers the device
  present (`xd3d_present_flush`) to the join at the next BCB30 dispatch,
  so tick N+1 runs against scene N. xv_preempt and xk_yield return at
  once on the helper (counted as suppressed yields; a suppressed
  Sleep/wait now sleeps 100 us instead of spinning).
- XV_RENDER_VIEW_THREAD=1: a private 4 MiB render table mirrored from
  the live one (xv_render_view_mirror keeps it current; a viewed entry
  keeps its copy until leave, which restores from live), bound only by
  the thread running the body (`xk_os_bind_page_table`: host thread-
  local pointer, Vita TPIDRURW through xv_thread_bind_table). The live
  table is never touched, so the tick reads/writes live while the scene
  reads frozen copies. Requires XV_THREAD_PAGE_TABLE=1 (so not Vita3K).
  `xv_host_page_table` is now `__thread`; xv_x86rt.h changed, so the
  generator pin in tools/query_memory_capture.py must be re-pinned
  before a Vita build that runs that gate.
- Shard hook order in code_017.c: scene-thread hook first, render-view
  scope second, so the view is entered/left on the thread running the
  body (tools/patch_render_view_hooks.py emits the old order; swap after).

Results (4-minute host runs, a10 from menu into the ship corridors):
- mode 1, mode 1 + thread view, mode 2, mode 2 + thread view: all reach
  the corridors, no hang, 60/60 dispatched per window, owner-side calls
  Present only. Mode 2 owner wait at the join is 0 ms in most windows
  on x86 (the scene finishes inside the tick), 25-45 ms in the heaviest
  corridor windows (the scene is longer there); thread-mode view adds
  nothing measurable (m2 and m2t wait series match window for window).
  The view merges 4-12k words/frame, 18-22 scene writes found late.
- Design correction 1: the join must not block in a host wait. The
  owner is a guest fiber holding the single runner; blocked, the
  streaming/sound fibers never run and the scene (loading screen) spins
  on its cache request. XV_LOCKSTEP=1 runs deadlocked at the a10 load
  until the join became poll + xk_sleep_us(100). Now both modes load.
- Design correction 2 (open): under lockstep two *baseline* runs
  diverge after the load (29 of 100 windows identical, tick offset 3):
  the load advances real time, so the comparison cannot yet prove the
  overlap is semantically identical. Needs a lockstep variant without
  the 200 ms real-time advance, or alignment by tick count.

Vita build of the stage (XV_RENDER_VIEW=1 XV_SCENE_THREAD=1, both
defaults 0) started 2026-09-21 evening in `../overlap-candidate`
(build.py + build-command.json); untested on hardware, not deployed:
no bench device. Hardware to-do when one exists: the runtime's vertex
capture/upload workers must bind the render table when serving the
scene (that is what hung increment A's first cut), D3DResource_Register
from the tick vs the scene's draws needs a runtime-side lock, and the
merge ownership of the 22 scene-written globals (§19) is still the
in-place merge semantics.

### 39a. Determinism proof of the overlap (host), and the build gate

`XV_LOCKSTEP=2` = lockstep without the 200 ms real-time advance during
loads: two baseline runs are then identical window for window (99/99).
`XV_TICK_TRACE=<file>` (xd3d.c Present) writes one line per Present:
frame, game tick (gg+C), player 0 object and position, i.e. tick-side
state. Comparing at equal ticks (the scene thread shifts *when* Presents
land relative to ticks, because lockstep's on-demand vblank counts the
owner's yields, so window-by-window comparison is meaningless):
- mode 1 + thread view: 5,414 common ticks, 5,413 identical;
- mode 2 (full overlap) + thread view: 3,976 common, 3,975 identical;
the one difference is tick 468, the spawn tick, where one run's Present
precedes the unit's appearance. So tick N+1 running against scene N on
the render view leaves the simulation bit-identical on the host. What
this does not cover: the scene's own output (draws use frame-old data by
design), Vita runtime workers, and ARM memory ordering (the Pi's job).

Vita build gate: tools/query_memory_capture.py pins sha256 of the
generator's *in-memory* inputs (query_fusion.c stays at Codex's pin;
query_f32_primitives/xv_x86rt.h is the selective header rebuilt from
xv_x86rt.h, now 4272656c...; query_world_run.h is the on-disk file). The
gate now prints the actual hashes on mismatch. Re-pinning from disk is
wrong for query_fusion.c: run the build once, copy the printed values.

### 39b. perf83 built, not deployed

`../overlap-candidate/build/xita.vpk` = 0.2.0-perf.83 / 427df56+
(sha256 b7e76c16b363bffd..., rebuilt 2026-09-21 18:08 with the conflict
census and the tick-wins default; the earlier a433fca6 build lacked them) with
XV_RENDER_VIEW=1 XV_SCENE_THREAD=1 (defaults 0: an ordinary launch
behaves like perf82). Env to arm before Launch (vita_remote.py env):
XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1
XV_RENDER_VIEW_THREAD=1 (start with XV_SCENE_OVERLAP=1 and the view off,
then add pieces; kill switches are the same names =0). Untested on
hardware: no bench device. The Pi binary of the same stage is
`../pi-bench/harness-armhf-overlap-d145ca0`. Expected hardware issues,
in order: the runtime's vertex capture/upload workers write live memory
the scene (bound to the render table) cannot see (A's first cut hung on
exactly that) - they need per-job binding to the render table; the
D3DResource_Register/IsBusy calls from the tick during the scene need a
runtime-side lock; the object-job lanes are shared with the scene's
visibility jobs (untested concurrently on Vita).

### 39c. Same-frame write conflicts: measured, policy chosen

`[render-view-conflicts]` (xk_render_view.c merge): a word the scene
changed whose live copy the tick also changed during the same scene, to
a different value. Mode 1 (no tick during the scene): 0 conflicts, the
control. Mode 2: 130-2,600 conflicting words per 60 frames in gameplay
(up to ~44/frame), at five places:
- the light records in the game-state block (0x80091908 + n*0x7C, a
  124-byte stride array; scene writer f_00092890 = lights, called only
  from the scene's 5D410; tick writer f_0008CBC0 via 109050): both
  write a counter-like field, the scene's value a few ahead;
- 0x2FC684 and 0x2D2FAC: the collision-query counter and time stamp,
  written by f_00171F10/1721B0/17DD40 (world BSP queries) from both
  halves;
- 0x2FC324: lights (f_000939C0/92890/92120);
- 0x2E364C: f_001744F0/1733E0/121110 (director side), rare.
Policy `XV_RENDER_VIEW_CONFLICT`: 0 = tick wins (now the default), 1 =
scene wins (the original merge). Tick wins keeps the simulation
bit-identical at equal ticks (3,315/3,316, §39a method) and survived a
10-minute mode-2 soak into the corridors; scene wins would write the
scene's frame-old copies of light/collision state over the tick's newer
values. Scene-only writes (words the tick did not touch) still merge.
Open for review with symbols: whether the scene's dropped light-record
writes matter across frames (at worst a one-frame stale render stamp).

Tool: `XV_WRITE_WATCH=<guest hex addr>` (recomp/host/write_watch.c,
host only): the live page and its shadow copy are made read-only at
scene entry; the first write to each from either half is logged with
PIE-relative code offsets (`addr2line -f -e harness <offset>`). Uses
per-thread alternate signal stacks and a page-aligned (mmap) arena.
Lesson: an earlier version resolved the address through the helper's
render table and protected the shadow page twice; that crashed the tick
in f_00091A80 (object parent-chain recursion) at the same frame in
every run. With the page resolved through the live table, no crash in
two runs, and a control watch on an unwritten page also passes.

## 40. Scene cost on the host: profile, oracles, first native helper

Tools (host, all env-gated, committed): `XV_HOST_SAMPLE=<file>` 1 ms
in-process sampling profiler (no perf on this PC) + `tools/host_profile.py`
(aggregates by guest function with addr2line); `XV_SOFTGFX_RASTER=0`
skips pixels so the profile is the guest scene, not the rasterizer (which
is 89% otherwise); `XV_DRAW_HASH=<file>` per-frame FNV of every D3D HLE
call (name + first 8 stack words, kernel-object addresses masked), meant
as the scene-output oracle; `XV_DRAW_HASH_TRACE=<frame>` writes that
frame's calls to `<hashfile>.trace`.

Profile of the scene helper thread in a10 (frames >= 1500, 92 windows,
guest only): flat. Top: f_0001EC1F 6.2%, f_00019E7B 5.6%, f_00054010 4.7%
(ordered-material dispatcher), f_00061270 4.4%, HaloBuildVisibleIndices
4.2% (native HLE), f_00053E90 3.6%, f_0001EABA 3.3%, xd3d_r_clear 3.0%,
__popcountdi2 2.6%, x87_load_f32 2.5%, then a long tail of 1-2% per-model
functions (52xxx-63xxx). No single guest hot spot; the cost is call
frequency and per-access page-table translation. The three CRT helpers
are MSVC's float machinery: 1EC1F = _controlfp, 1EABA = _frnd, 19E7B =
floor-style rounding under the control word at 0x1F2840 (134 call sites,
up to 17k calls/frame in heavy tick areas).

XV_NATIVE_CRT_FLOAT (recomp/kernel/xk_crt_float.c, hooks installed by
tools/patch_crt_float_hooks.py, Makefile flag, runtime.mk source list):
native versions using the emulator's own x87_round, NaN/inf and the
unmasked-inexact path left to guest code. Exact: tick trace identical to
the baseline at 5,415 of 5,416 ticks (the spawn tick again). Host scene
guest samples -11% in the same windows. On the Vita expect a few percent
of the scene, to be measured. XF_P now uses __builtin_parity instead of
popcount (every x87 compare tests PF; the libgcc call was 2.5%); this is
an xv_x86rt.h change, so the generator pin moves again.

Next candidate: f_00061270 (5%, 38 sites) packs a clamped float3 into
11/11/10-bit integers (floor + fistp per component); exact native needs
the same double-precision order and x87_round, verified by the draw hash,
which is NOT yet deterministic: two baseline lockstep runs agree on only
~55% of frames while their per-call traces of a differing frame are
identical, so the difference is outside the traced words (under
investigation: trace-all mode).

## 41. Virtual clock, and where the draw-stream oracle stands

`XV_LOCKSTEP=2` is now a fully virtual guest clock: tick count, uptime,
system time, KeQueryPerformanceCounter and the guest's rdtsc all derive
from the vblank count (1/60 s each, starting at 1 s so a zero deadline
still means "none"); idle time is skipped (the scheduler advances the
clock to the earliest deadline instead of sleeping), a lone polling
thread advances it 100 us per yield (the boot's XNetGetTitleXnAddr poll
and Sleep loops needed both), and 200 vblank-fiber spins without a
Present add a vblank. Tick-side state is identical run to run (2,840 of
2,840 ticks). Runs under different clock modes are not comparable (the
level starts at a different tick).

Draw-stream hash (`XV_DRAW_HASH`, real arguments only via
tools/gen_d3d_argc.py, audio calls excluded, line = frame, tick, hash):
menu frames agree well between runs, level frames do not even at equal
ticks. The per-call trace showed screen-space quads and UI fades that
follow wall time (fixed by the virtual clock) and then a frame-to-tick
phase that drifts differently per run (the streaming thread's I/O
completion timing changes the extra vblanks during the load). Not
pursued further: scene-side native replacements will be verified
in-process instead (run the native and the guest body on the same
inputs, compare outputs, count mismatches), which needs no cross-run
alignment; the tick trace remains the oracle for anything the tick uses.

Housekeeping: `tools/host_run.sh` now removes the temp save directory it
creates (each run copies ~300 MB of cache files; 88 leftovers filled the
31 GB /tmp). Trace-all draw logs are ~500 MB per run; delete them.

## 42. Remote restart: vitacompanion on the Vita (2026-09-21 21:10)

vitacompanion 1.07 (devnoname120) is installed on the user's Vita:
`ur0:tai/vitacompanion_kernel.skprx` + `ur0:tai/vitacompanion.suprx`,
registered at the end of the `*KERNEL` and `*main` blocks of
`ur0:tai/config.txt` (backup of the previous config in
`xita-backups/vita-remote/3357-9AA2/tai-config-backup-*.txt`; spare
copies of the plugin files on `ux0:tai/`). Clone + release binaries:
`~/github/third_party/vitacompanion`. It answers on 192.168.0.205:1338
whether or not Xita runs: `tools/vita_companion.py launch XITA00001 |
quit all | reboot | press cross | nosleep on | screen off`. Verified:
quit all -> launch -> the in-app remote (/status) answered on perf82.
`tools/vita_watchdog.py --config <remote-client.json> --env K=V...`
relaunches after a crash (dialog dismissed with press cross, env
re-armed), reboots after 3 failed relaunches, logs every event. Not
yet exercised on a real crash dialog. Whole-console hangs still need a
power button.

## 43. Overlap on hardware, night of Sept 21-22: seven builds, one root class

Stage `../overlap-candidate` (perf83..93, each deployed through
run-overlap.sh + tools/vita_watchdog.py; slots now hold perf92/93).
What each build found, from device logs and core dumps
(`tools/vita_core_threads.py <core> <elf>` prints every thread's PC, LR
and a return-address scan; vita-parse-core + pyelftools 0.29 in
`~/github/third_party/venv-core`, c_str patched for python 3):
- perf83/84: helper entered the scheduler through xk_wait_u32 (the Vita
  runtime's capture-completion wait) while the owner slept in the join:
  data abort on the helper. Fix: xk_sleep_us / xk_wait_u32 / xk_wait
  never touch xk_cur or the scheduler on the helper.
- perf85/86: dispatches stopped at the a10 load; overlap gated to
  gameplay (game_globals loaded+active); loads use the perf82 path.
- perf87: private scene stack moved to the kernel region (game pool
  allocation at the first menu frame).
- perf88: proxy wait (owner performs the helper's guest object waits);
  join poll 1 ms. perf90: helper pinned to core 1. perf91: remote thread
  at owner priority, any core. perf92: remote accept-loop trace.
- perf93: logger report ownership by the real thread id (owner+helper
  both appended to the grouped report buffer under the alias).
Result: the overlap runs the main menu at 29 fps on every build from
perf85 on, and the level load never completes under it. The remote
server stops answering after the scene thread starts (TCP connects,
accept loop keeps sleeping; the log itself stops being written), and
every forced-quit core shows the helper stopped with a data abort in
xk_NtReleaseMutant, reached from the scene body (f_000C84D0 ->
... f_00050A00 -> ReleaseMutex): the scene acquires and releases guest
mutexes (cache-file request table) with zero timeout on the helper,
while the owner side runs the same kernel-object code on another thread
(xk_wait's fast path and NtReleaseMutant use xk_cur, the *owner's*
current fiber, as the acquiring thread; wait lists are unlocked).
This is the general form of every earlier fix: two threads inside the
single-threaded guest kernel.

Three baseline runs (scene thread off; natives on/off; scene thread on
without overlap) all load and play at 8-9 fps in the cinematic area:
the base build is intact. The overlap has not rendered one gameplay
frame on the Vita; the 15 fps figure remains an estimate.

The host never showed the mutex race in 10-minute mode-2 runs: x86
timing, not correctness. Options for the fix, in order of preference:
(1) a recursive lock around the guest kernel's object and scheduler
state (xk_wait, try_satisfy, wake paths, Nt/Ke object calls, the
scheduler pass), released across fiber switches and idle waits, with the
helper using the scene's own thread record (thread 8) instead of xk_cur;
(2) proxy every kernel-object call from the helper to the owner (the
proxy-wait mechanism generalized; latency = the owner's next service
point, fine in mode 1, up to a preempt slice in mode 2). About a day
either way, verifiable on the host with the existing runs plus a
deliberate stress (the census tool can count helper-side object calls).

Remote-control lessons: vitacompanion `press` does not reach Xita's
input (dashboard or game); the in-app pad endpoint remains the only
scripted input, so campaign entry needs the remote alive. `quit all`
before `launch` is the working relaunch recipe; a console reboot
followed by launch worked once the user unlocked the screen (a lock
screen or system dialog after reboot cannot be dismissed remotely).

## 44. Sept 22 morning: the proxy at the right place, and the remote's real symptom

- The kernel-call proxy first hooked `xk_dispatch_magic` (xv_call), but
  the shards call kernel imports DIRECTLY through `XV_HLE_CALL(slot,
  xk_Name)` in `recomp/xv_recomp_protos.h`; xv_call only sees indirect
  calls. perf98 hooks the macro: `XV_HLE_PROXY(fn)` routes `xk_*`
  functions from the scene helper to the owner (`xv_scene_thread_proxy_hle`),
  D3D HLEs (`xv_hle_*`) run on the helper as before. The generator
  (`recompiler/xita_recomp.py`) emits the same guard. The header is
  pinned by the query gate: the build script re-pins from the printed
  hashes automatically now (overlap-candidate/build.py chain).
- The remote server never died: perf97's iteration-count trace shows
  its loop accepting and serving my probes every 5 s while the client
  saw no reply. Responses are lost/late under the overlap (send path or
  the handler), the server thread itself is fine. Workaround for
  scripted input: `tools/vita_campaign_fire.py` sends pad requests
  without waiting for replies; progress is read from the FTP log.
- Log lines from the scene helper are dropped (counted) since perf96;
  the logger's grouped-report ownership is by real thread id (perf93).
- The Vita sometimes ignores `launch` (no new launcher.log line, no new
  game log) until someone looks at the screen: seen after last night's
  reboot and again 14:42 UTC today. The plugin answers; the app does
  not start. Unknown system dialog or lock.

## §45 perf99 on the Vita (2026-09-22 12:18-12:30 CDT): first overlap build past the a10 load

perf99 = perf98 (kernel-call proxy at the XV_HLE_CALL call site: every `xk_*` import the scene helper issues is
executed by the owner on the scene's thread record) + XV_NATIVE_PACK (native f_00061270, host-verified 7.8M calls /
0 mismatches, default off). Deployed with `run-overlap-fire.sh m1m 6 XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=1 ...`.

- Mode 1 (Present joins) ran the full 6 minutes: menu 23 fps, a10 loaded at t+150 s, cinematic 7.7-8.6 fps (game
  118-128 ms), 60/60 scenes per window on the helper, owner wait at Present 62-66 ms/frame (max 104 ms), no new core
  (53 before and after). `[scene-proxy]` shows the helper's kernel calls are all proxied (NtSetEvent + NtYieldExecution,
  74 in the first window, then 10-16 per window, direct 0). This is the first overlap build that completes the load
  (perf83-93 died inside the load or on the first scenes); the call-site proxy is the fix for the two-threads-in-the-
  guest-kernel class (§43).
- Frame time in mode 1 equals the baseline (8-9 fps cinematic) by construction: Present waits for the scene. The scene
  costs ~65 ms on the helper; the rest of the frame (tick + Present + pump) is ~55 ms, so mode 2 (deferred Present)
  has room for roughly max(65, 55) + join.
- New: `yield storm` lines (~2,500 yields / 3 s) in gameplay - the owner's 200 us join poll at Present (~100 yields
  per frame). perf81/82 logs have none. Harmless in mode 1; watch that mode 2 makes it disappear.
- Bench Pi: Raspberry Pi 4 (4 GB, 32-bit Raspbian 13 userland, 64-bit kernel) at 192.168.0.9, ssh alias `pi`, key
  ~/.ssh/xita_pi, layout ~/xita/{harness,halo_image.bin,haloce,runs}; `tools/pi_run.sh <tag> [s] [K=V..]`. The static
  armhf harness (host_build.py --static --cc arm-none-linux-gnueabihf-gcc) runs the game: base1 reached a10 player
  control in 5 min (697 Begin/End frames, ~18 fps vblank-paced). Overlap mode 2 run (ov2a) in progress.

## §46 perf100: real overlap on the Vita, first hardware gain (2026-09-22 12:57-13:05 CDT)

perf99 mode 2 was mode 1 in disguise: the Present HLE's entry counter (XD3D_COUNT -> xd3d_count) ran the generic
"owner-side D3D call joins the scene" hook before the Present policy could defer, so every frame joined at Present
(`[scene-overlap] ... owner d3d 60`, `[scene-owner-d3d] D3DDevice_Present 60`). Fix (commit c0dea71): Present is exempt
from that join (identity-compared name); the generic join stays for every other owner-side D3D HLE (GXM is single-threaded).
Host: joins `next dispatch 60 / owner d3d 0`.

perf100 = perf99 + that fix, `run-overlap-fire.sh m2b 8 XV_SCENE_THREAD=1 XV_SCENE_OVERLAP=2 XV_RENDER_VIEW=1
XV_RENDER_VIEW_THREAD=1 ...`, 8 minutes, no crash (cores 53 before and after):

| window | perf99 (join at Present) | perf100 (tick N+1 over scene N) |
|---|---|---|
| a10 cinematic, 190 Begin/End, ~305 draws | 118-128 ms, 8.0-8.6 fps | 70-90 ms, 11.4-14.0 fps |
| owner wait for the scene | 62-66 ms/frame at Present | 15-26 ms/frame at the next dispatch |
| cores (C0 owner / C1 helper / C2 workers) | 27 / 51 / 23 % | 27 / 85 / 57 % |

The scene (~65 ms on the helper) is now the critical path: owner side = tick + pump (7.6-9 ms texture pump) + wait.
Still the cinematic, not the corridor; still with the learned-subset render view (hazard below).

**Pi bench finding (ov2b, real overlap, all pages NOT frozen):** hung at frame 4437 in the corridor. gdb: the helper
spins in x_str_movs inside f_000A26B0 (model draw); ctx.r: ebp=01914660 is not a model tag ([ebp+4]=C22F0000, not the
'mode' magic) and [ebp+B8] (node count) = 41E87F7B, a float, so the `movsx eax,dx; cmp eax,ebx` loop at A27D4 never
ends. The object's model tag reference was torn by tick N+1 while the scene read it live: the render view had learned
only 7-8 game-state pages (cinematic passes). Just before the hang: a 4 MB cache-file read completing on thread 16.
Fix (commit 70a121c): XV_RENDER_VIEW_ALL=1 lists all 1024 game-state pages (the shadow capacity) up front; =2 also all
933 image .data pages. Host: copy-in 4.1 MB (mode 1) / 7.8 MB (mode 2) per frame; Pi: 2.98 ms/frame for mode 1;
Pi soak all1a (15 min) past the previous hang point without incident (in progress at 13:06). perf101 = perf100 + the
knob (default off), deploying with XV_RENDER_VIEW_ALL=1 to measure the copy cost on the Vita.

**Trap:** a background shell that waits with `pgrep -f '<script> <tag>'` matches its own command line and never exits
(the m2b chain sat for 8 minutes); use `pgrep -x` on the process name or a pid file.

## §47 Vita render view was never on; scene phase split; frozen-page cost (2026-09-22 13:05-13:50 CDT)

- **Every Vita overlap build (perf83-102) ran with the render view disabled**: `[render-view] no shadow region; disabled`
  printed BEFORE `[xv/boot] arena 75 MB`. main() called xv_render_view_configure before xv_boot_recomp allocated the
  arena (g_xram NULL); the host harness configures after xk_init and was unaffected. Fix f735a79: configure in
  xv_boot_recomp after xk_init. perf103 = first Vita build with the view configured (1024 slots); perf104 (9337c9a) also
  lets all mode enter before the learning passes (perf103 logged `entered 0` all cinematic: enter() waited for 6
  passes x 150 frames). So the perf100/101 gains (8 -> 11-14 fps cinematic) were real overlap with NO frozen pages and
  survived 8-minute cinematics only because the cinematic has little object churn.
- Vita memcpy is slow: `[render-view] 1024 KiB throughput MB/s: arena->arena 234, arena->heap 312, heap->heap 236,
  memcmp heap 86`. All mode 1 copy-in (4 MB) is ~17 ms/frame on the owner; the owner currently waits 15-30 ms/frame for
  the scene, so it may be absorbed - measure with perf104. The learn passes hash 7.7 MB twice per pass (~90 ms each).
- **Scene phase split on the Vita** (XV_SCENE_PHASES=1, tools/patch_scene_phase_timers.py, a227e28; perf102 cinematic,
  scene 76-79 ms on the helper): 5DBC0 78.8 = 5D990 77.5; direct callees: 606B0 13.3, 54010 10.2 (9 calls/frame),
  60560 8.8, 5B760 8.7, 5B710 6.9, 54740 6.6, 539C0 5.9, 92890 5.2, 93C00 4.0, D6B00 3.1, 28320 2.8-6.7, 542F0 2.8.
  Flat: the top 8 are ~65 ms; no single native rewrite gets the scene under 45 ms. 28320 (scene start: bookkeeping +
  a subsystem callback through ds:[2E3008]+14) varies 2.8-6.7 ms: back-pressure/wait, not compute.
- GPU: `[gpu-packet]` in the cinematic: submit 7-11 ms/frame, completion bounds lo ~40 ms / hi ~90 ms, bracket ~50 ms
  (coarse polling), so the GPU is between 40 and 90 ms per frame - possibly the wall at a 50 ms target. Untested: a
  lower render resolution run (single observation, not an A/B).
- Clean rebuild: perf102 = every object rebuilt (the stage had xk_mem.o from 09-21 21:37 compiled without
  XV_RENDER_VIEW; harmless in the end but the rule stands: after changing the build command, delete build/recomp/**/*.o).
- Pi: hangs/aborts are the host reporter thread running owner-only report invariants (owner_drained,
  xv_object_math_report_check) - reports now run on the owner at the device present (a227e28); all1d soak (all pages
  frozen, real overlap, corridor) 20+ min without incident. The Vita's save/ tree is on the Pi (~/xita/saves/vita;
  `PI_SAVE=vita PI_PAD=150:a,300:a,450:a,600:a tools/pi_run.sh ...`) for "Continue" runs; the corridor checkpoint is
  the profile save of 2026-09-21 01:02.

## §48 Frozen pages on the Vita: cost and hangs (2026-09-22 13:45-14:25 CDT)

- perf104/105 (render view really on, all mode 1): copy-in 20.9 ms + merge 3.3-5.5 ms per frame ON THE HELPER (enter()
  runs inside BCB30's body), helper core 97%, cinematic back to 8.3-9 fps (from 11-14 without the view). Both runs
  hung (perf104 in the first gameplay window, perf105 at 7.5 min): thread dump = owner in the join poll (yield storm),
  a game thread (start CFDE0) waiting on a mutant, the helper not finishing. The render view's own watchdog comment
  names the class: "a scene bound for over 3 s is stuck (guest polling a word a host writer updates through a live
  pointer)". With all 1024 game-state pages frozen, a completion word the kernel/I-O thread writes live is polled by
  the scene in its shadow copy. The Pi never hit it (host fibers, no real I/O concurrency). That watchdog only ran from
  the remote status poll, which is dead under overlap.
- Fixes (commits after 40e4d9d): XV_RENDER_VIEW_DMA=1 (one sceDmacMemcpy for the contiguous slot range, link
  SceKernelDmacMgr_stub) for the copy cost; XV_RENDER_VIEW_ALL=3 freezes only the object header table + the live
  objects' data range (re-listed as it grows; host: 8 slots at load), so completion words stay live; the scene join
  loop logs the helper's guest registers + stack code words after 3 s and runs the watchdog (recovery, view disabled).
  Thread dump prints a mutant's owner/count. The two cores dated 13:35/13:49 are UTC (08:35/08:49 CDT, morning perf97
  runs, not this session; core names carry the epoch, the FTP listing is UTC): helper in xk_NtReleaseMutant through the
  CRT wrapper from 50560 during a non-overlapped scene -> the proxy is now unconditional for helper scenes (2f84a63).
  No core was produced by any run of this session (perf99-108).
- Deploy trap: a hung Xita cannot take an update (in-app remote dead): `run-overlap-fire.sh` prints DEPLOY NOT
  CONFIRMED; companion `quit all` + `launch XITA00001`, wait 45 s, then deploy. The "cores" count in the run summary
  was meaningless (folder capped at 53): it now prints the newest core's time.
- perf107 = ALL=3 + DMA + stuck dump, deploying at 14:25.

## §49 The overlap hang named, mitigated; owner-side snapshot; resolution check (2026-09-22 14:45-15:30 CDT)

- Every frozen-page hang on the Pi (all1a, all1b, all3a, prep1) and the two on the Vita (perf104/105) had the same
  signature: helper in f_000A26B0's node loop with ebp 01914660 (not a model tag) and node count 41E87F7B (a float).
  `tools/patch_model_draw_guard.py` (at A26B0 entry: node count > 0x400 -> log + `ret 2Ch`) fired once on the Pi:
  `tag index C0A8 (eax 8083C0A8 ecx 0 edx 8083BAF8) entry 80527524 class 3CBA5D69/... data 01914660 ... ret 0005B3B1`.
  The caller (f_0005B190 at 5B3A9: `mov eax,[edx+34h]`) read a POINTER (8083C0A8 = edx + 0x5B0) where the model
  tag datum should be; edx (8083BAF8) is the object's definition tag data resolved from the object's datum at [ebp]
  (5B23D..5B24E) - both live in the tag data region ABOVE the 4 MB game-state limit, never frozen in any mode. So the
  scene reads a torn object datum / wrong tag under real overlap; the host (x86, ucontext fibers) never reproduces it
  (12,480 frames mode 2 clean), the Pi hits it at frame ~4400-5600 (corridor) every run. Guarded run guard1: one skip,
  then 15 minutes clean (448 windows). The guard is the mitigation shipped in perf110 (one object skipped for one frame).
  Open: which write tears [ebp] / the tag pointer - candidates: object deletion/reuse during tick N+1 seen through a
  structure outside the frozen ranges (visibility/cluster object lists, tag data runtime fields).
- Owner-side snapshot (8915823): `xv_render_view_prepare` runs the copy-in on the owner before `go`; on the helper it
  raced tick N+1 (a real defect, though not the cause of the signature above). "owner-side NN ms" in the report was
  cumulative until ca05ab3 (never reset); the real cost is ~0.4 ms (Pi) and a few ms (Vita, memcpy or DMA alike:
  DMA=0 and DMA=1 both give ~84 ms frames). perf108/109/109b: 8-minute cinematic runs at 11-12.7 fps, no stuck.
- Resolution: XV_RENDER_HEIGHT accepts only 360/400/480 (else native 544); the cfg file wins over env (edit
  ux0:data/xita/xita.cfg over the plugin FTP, restore after). Native 544: 96.7-107 ms vs 85 ms at 360 -> resolution
  is a 12-20 ms factor; at 360 the CPU scene is the wall.
- Scene phases on the Vita with the copy off the helper: 5DBC0 63.7 ms (perf108); the flat split of §47 stands.

## §50 Root cause of the overlap hang, mode 4 (2026-09-22 16:00-16:20 CDT, Pi only - the Vita is locked)

Pi core.4415 (XV_MODEL_GUARD_ABORT=1, `tools/patch_model_draw_guard.py`): at the guard, the helper's edx (8083BAF8) is
tag entry 0 = the scenario tag "levels\a10\a10" and [edx+34h] = its skies block pointer; the caller f_0005B190 had
ebp = 0: `mov ebp,[ecx+edi+8]` (object header table slot -> object pointer) returned 0 for the datum the render list
passed in, i.e. an object whose header slot was already free in the snapshot. Guest page 0 is mapped, so the NULL
object read gave datum 0 -> tag 0 -> the skies pointer as a "tag index" -> garbage tag -> float node count -> the
A26B0 loop. So: the render list was built from arrays outside every frozen range (cluster / object reference arrays,
cached object render states) while the tick deleted the object. Not memory ordering; a stale list vs a consistent
snapshot (the host never showed it because its fibers keep the tick from interleaving there).
Mode 4 (commit above) freezes, by name, `cached object render states`, `cluster light reference`, `light cluster
reference`, `object`, the four `cluster ... object reference` arrays, `object looping sounds`, `object list header`,
`list object reference`: 248 slots on the host. The full data-array list (42 arrays below 4 MiB) is in the
`[render-view] arrays:` log lines (mode 3/4, once per map). Pi soak mode4a in progress; the guard stays as the
backstop (it fired once per run in modes 1/3 and the run continued).
- Mode 4 soak mode4a (Pi, 25 min, 639 windows): 0 guard fires, 0 stalls. Every earlier mode fired by window ~75-95.
  The stale render list was the whole hang class; mode 4 is the fix, the guard stays as backstop. mode4b (60 min) next.
- HLE entry overhead on the Vita: xd3d_count asks "am I the helper?" twice per D3D HLE call and that was
  sceKernelGetThreadId (a syscall) - ~3,800 HLE calls/frame in the corridor (host histogram at frame 1500:
  SetTextureState_Deferred 1152, SetRenderState_Simple 898, SetRenderStateNotInline 782, SetTexture 226,
  SetStreamSource 151, SetVertexShaderConstant 145, SetIndices 100, DrawIndexedVertices 99). 85b4dd6 replaces it with a
  stack-pointer range test (helper_main records its 1 MiB stack). Expect a few ms off the helper's scene on the Vita
  (the `draw-hle` 12-15 ms figure is the HLE time inside the scene). perf112 = perf111 (mode 4, guard, owner-side
  snapshot, report fix) + this; built, NOT deployed (Vita locked since 15:36).
- Vita scene structure (from the Pi split + host histogram): 54010 (9/frame, 10.6 ms Vita) is the structure-BSP
  material loop with a per-material draw callback; 606B0 (13 ms Vita, 1 ms Pi) is draw submission; 5B4A0 (60/frame)
  the per-object render with 5B190; i.e. most of the Vita's scene is D3D HLE recording cost, not guest arithmetic.
  Next levers in order: HLE per-call overhead (this), then the recording path itself (state calls ~2,800/frame), then
  native object/BSP loops.

## §51 Vita afternoon: flicker, the proxy fiber, perf112-115 (2026-09-22 17:05-17:55 CDT)

- perf112 (mode 4 default "object,cluster"): NPCs flicker, 54-62 draw batches instead of 187. Freezing `cached object
  render states` loses the scene's own writes to it (tick-wins merge) so objects vanish next frame. Mode 4 default is
  now "cluster,list" (30232a7): m4cl run drew 186-192 batches at 79-88 ms (11.2-12.6 fps), 6 min, guard 0.
- perf112 froze at 17:14 after 4 min: the owner spinning (yield storm, no STUCK line) inside a proxied
  RtlEnterCriticalSection loop on its own fiber, while the streaming thread that holds the section waits for the owner's
  tick. Structural: proxied calls executed on the owner's fiber. Thread 24 ("state 3 wait mutant") in every dump is a
  finished thread (state 3 = exited), a red herring. The remote wedges after such a freeze (accept fails forever):
  device reboot + user unlock needed.
- Proxy fiber (77c61ab): a kernel-internal guest thread (xk_thread_create_host) runs the helper's proxied calls.
  perf113 froze at the menu: its 50 us poll made it the shortest-deadline sleeper and the scheduler starved everyone
  (owner 40 ms overdue, 6,000 yields/s) -> 2 ms poll (perf114). perf114 ran, but the object pass never began
  (`[object-jobs] passes 0 jobs 0` vs 33 jobs/window on perf112), objects not updated, flicker, ~90 draws. Host and Pi
  with the fiber: full draws (Pi mode4c 605 batches). Vita-only (XV_THREADS=1 real threads + owner-phase admission by
  thread id is the suspect). Parked: XV_SCENE_PROXY_FIBER=1 opt-in (default off, perf115). The perf112-class freeze
  is therefore still possible (~once per hour of runs); the mitigation candidates are (a) fix the admission so the
  fiber works, or (b) make proxied blocking calls time-slice (return to the join loop between yields).
- HLE timing (XV_HLE_TIMING=1): macro + xv_call hook + [hle-time] report exist; the D3D vtable calls come through
  xk_dispatch_magic (magic ordinals), where the hook is NOT yet installed (the edit was interrupted) - on the host
  the report shows only sound calls so far. Build-gate pin for xv_recomp_protos.h re-pinned (d9ee557).
- Quality sweep script `overlap-candidate/quality-sweep.sh <baseline-tag> <knob>...` written (one knob removed per
  6-min run, gameplay-window median vs baseline, THRESH=3 ms, writes sweep-final.cfg, restores the original cfg);
  not yet run - needs a clean baseline run first (perf115).
- perf115 (fiber off, mode 4 cluster,list, guard, syscall fix) at DEFAULT graphics (user's call: only XV_RENDER_HEIGHT=360
  kept; the old low-quality cfg is overlap-candidate/xita.cfg.lowquality-20260922): 8 min clean, 79-93 ms
  (10.6-12.4 fps), 198-209 draw batches, object pass 34 jobs/window, guard 0. The ten quality overrides together were
  worth ~7 ms (m4cl at low settings: 79-88 ms), below the 3 ms/knob noise floor, so they stay default.
- Trap (18:20): tools/host_build.py rebuilt shards only when xv_x86rt.h/xk.h/xk_os.h changed, so every "host check" of an
  xv_recomp_protos.h macro edit ran stale shards; now xv_recomp_protos.h and xk_object_jobs.h are triggers too
  (656f138). HLE timing verified after a clean host build: ~9,850 timed HLE calls/frame on the host, the D3D ones
  0.04-0.07 ms/frame each there (SetVertexData4f 1769/frame, SetTextureState_Deferred 1817/frame, Begin/End 446).
- perf116 = perf115 + HLE timing hooks; deploy blocked at 18:41: the perf115 process died/froze at 18:24 (log ends,
  launch ignored -> the device needs the user again). run-overlap-fire.sh now restarts to the dashboard before a deploy
  when a level is running (the remote's replies are lost mid-level). Armed loop: launch every 30 s, deploy on a fresh boot.
- Pi mode4c (18:48): 60 minutes, 1,791 windows / 107,460 frames of real overlap in the corridor, mode 4 (cluster,list),
  proxy fiber ON (host variant), guard 0, stuck 0, not-drained 0. The overlap is stable on ARM by construction now.

## §52 HLE time split on the Vita (perf117, XV_HLE_TIMING=1, a10 cinematic, 19:30 CDT)

Timing itself costs ~24 ms/frame (two sceKernelGetProcessTimeWide per HLE call, ~9,500 timed calls/frame), so read
the split, not the absolute frame (114 ms under timing vs 90 without). Per frame, steady state:
DrawIndexedVertices 19.2 ms (228 calls, 84 us each) | XInputGetState 6.4 (1 call!) | SetTexture 2.9 (503) |
End 2.9 (205) | SetTextureState_Deferred 2.6 (2653) | SetRenderState_Simple 2.1 (1781) | SetStreamSource 0.4 (282) |
SetIndices 0.3 (229). The waits (NtWaitForSingleObjectEx 216 ms/frame over 3 calls) are the streaming threads' event
waits summed across threads, not owner time.
So of the ~65 ms scene: ~19 ms is the draw call's runtime work (xd3d_r_draw in runtime/xv_ui_gxm.c: SetVertexShader,
SetTrackedConstants, DrawIndexedVerticesBase, ps_sync; stages timed by XV_DRAW_PROFILE=1: SETUP/STATE/INDICES/PROGRAM/
STREAMS/CONSTANTS/TEXTURES/DIAGNOSTICS) and ~10 ms the state/texture setters. That is the native side of the scene and
the next lever; the run with XV_DRAW_PROFILE=1 is queued (dprof). XInputGetState at 6 ms/call is unexplained
(xk_os_pad_poll: sceCtrlPeekBufferPositive + xv_remote_pad, both cheap in code) - measure inside next.
- perf117 = perf116 + [cs] STUCK 3 s -> log holder + steal in RtlEnterCriticalSection (c134800): no trip in this run so far.
- perf118 (proxy yields inline + events serviced from xk_yield on the owner thread; 3f-commit after 5ffa939): passed the
  load->cinematic transition where 116/117 froze. Draw-stage split, steady state (XV_DRAW_PROFILE=1, 353 draws/frame):
  streams 10.4 ms, textures 4.1, indices 3.9, program 1.7, state 1.6, setup 0.6, constants 0.5, diagnostics 0.3
  (~23 ms/frame of draw work; the 13.6 ms textures figure was the load transient). First native target: the per-draw
  vertex stream binding (~29 us/draw).

## §53 The streams stage is the vertex snapshot copy; Pi mode4d deadlock (2026-09-22 20:00-20:20 CDT)
- perf118 ran its 8 minutes clean at default graphics + 360 rows (10.3-12.2 fps in the cinematic, 81-96 ms).
- Streams stage (10.4 ms/frame, xv_d3d.c draw path) = `xv_vertex_capture_submit`: the owner memcpy's each stream that
  misses the reuse table into the 4 MB staging arena. `[vertex-capture]` per 60 frames: ~7,800 jobs, ~60 MB copied
  (1 MB/frame at the Vita's ~240 MB/s = the stage), capture 360-470 ms, 15 arena reclaims (every 4 frames the arena
  fills, `cap_reuse_reset` forgets every identity, everything re-copies). Reuse: 8-9k checks/60 frames, all exact hits,
  trust (XV_CAPTURE_TRUST_TAGS) skips the compare for tag-resident sources. What is NOT known: whether the 1 MB/frame is
  eviction churn (tag-resident, fixable by a bigger arena / smarter reclaim: Makefile accepts XV_CAPTURE_ARENA_KIB
  2048/4096/8192) or geometry the game rewrites each frame (game-state region, needs a different path).
  perf119 = perf118 + `[vertex-capture-census]` (0122b3f): copied bytes by region (tag / game state / other) and the top
  64 KiB source bins. Read it, then pick: arena 8192 (tag churn) or a no-copy path for rewritten geometry.
- Build recipe recovered: `ce-build-command.json` in the unified-games dir is the exact make line (RECOMP=1 + ~60
  XV_* options + HALO2_PACKAGE); `overlap-candidate/make-vars.txt` is the stamp-reconstructed equivalent for the overlap
  stage (`PATH=build/bin:$PATH make -j6 xita.vpk $(cat ../make-vars.txt)` from the stage; the rg shim in build/bin is
  required by the Makefile's OWNER_PHASE hook check). Bump version.json by hand; the Makefile's option stamps
  (build/*.config) are what decide rebuilds, so a wrong variable silently recompiles half the tree.
- Pi mode4d soak DEADLOCKED after 2 h (18:53, load 0.02, no log lines): gdb: owner in xv_object_jobs_join waiting on
  owner_wake (service_owner), both workers in xv_object_math_lock (nanosleep loop, guard held by someone else), scene
  helper in xv_scene_thread_proxy_hle waiting for proxy_done. The helper holds the math guard (scene-side math native,
  non-lane path takes math_mutex) and its proxied kernel call is only serviced by the DISPATCH join loop, which is not
  running while the owner is inside the object pass. Fix 3e2dd53: owner waits in the object pass (owner_wake, dones,
  visibility dones) are bounded 500 us and call `xv_scene_thread_service_owner_blocked()` (full proxy service, owner
  thread only) between attempts; `[object-jobs] owner bounded-wait timeouts N` counts them. Pi soak yp2 (1 h) and
  Vita perf120 carry it. This is a candidate for the ~once/hour Vita freezes that left every thread asleep.
- perf120 (owner-wait fix + census) 8 min clean, 11.0-12.4 fps; `[object-jobs] owner bounded-wait timeouts` ~115/60
  frames (normal waiting for workers, now with proxy service). CENSUS ANSWER: copies tag 4200 (96 MB/60 frames = 1.6
  MB/frame) state 48 (15 KiB) other 0. All of it is tag-resident BSP vertex data; top bins 0x1480000 (43.6 MB/380
  copies = ~115 KB each), 0x1450000, 0x1470000. Cause: `cap_reuse_find/add` refused sparse-referenced streams (their
  worker result is per index mask), so every sparse BSP draw re-copied its whole source buffer, and that churn filled
  the 4 MB arena every 2-3 frames (24 reclaims/60 frames), evicting the non-sparse entries too.
  Fix 811a282 (perf121, running 20:57): sparse streams share the arena CPU snapshot (entry flag `sparse`, key includes
  it); the job's reuse id is cleared so worker results are never stored/borrowed across masks; hit path copies the mask
  into the job like a miss. `[vertex-capture-sparse]` counts copies avoided. Expect the streams stage (10.4 ms) to fall
  to the publish/join residue and reclaims to ~0. Not a knob: it is unconditional (XV_VERTEX_CAPTURE_REUSE=0 disables
  all reuse).
- Pi yp2 (owner-wait fix): one `[scene-thread] STUCK 3000 ms` at line 46772 with an owner yield storm (thread 8 in the
  56670 cache-request wait), proxy fn nil / pending 0, helper stack words 7A3B0 6229C 541C4 58B38 10C356 110CDF 5D7FC
  5DBAC; the render-view watchdog restored the live mapping and DISABLED the view, the run continued (deadlock test
  still valid, frozen-page coverage ended there). Open: a 3 s scene with nothing proxied.
- Trap, again: `until ! pgrep -f 'run-overlap-fire.sh perf120'` in a background shell matches the shell itself and
  never ends (perf121 sat unfired for 15 min). Use `ps -eo pid,args | grep '[r]un-overlap-fire.sh perf120'` or a pid
  file. Also: the fire script's FTP fetch is starved for ~60-90 s at the load->cinematic transition; that is not a
  freeze (I relaunched a healthy perf119 on that signal). The script now keeps the previous log copy on a failed fetch
  and the longest copy as gameplay-<tag>.log.max.
- CORRECTION on Pi yp2: it did NOT recover from the 3 s STUCK at 20:37 (line 46772). The frame counter froze at 19260
  (line 46605) and the log only grew with yield-storm thread dumps (~1,170 storms). gdb at 21:16 (runs/yp2-gdb.txt):
  helper (50% CPU) spinning in f_001105E0 -> xv_trap -> xk_yield -> xv_scene_thread_note_suppressed_yield (callers
  14188 <- 13A77); owner in xk_os_scheduler_wait with guest thread 8 yield-storming in the 56670 cache-request wait
  (stack BD97C 56A6E 56978); workers idle in wait_sem. A LIVELOCK, not the mutex deadlock: the scene (helper) spins on
  a memory flag for a cache request while the owner side spins in its own cache-request wait, and nobody serves it.
  The owner-wait fix (3e2dd53) does not cover it (no proxied call is pending: proxy fn nil). Same class as the Vita
  perf116/117 transition freeze. Open: who completes cache requests (which guest thread / native file thread) and why it
  cannot run while both sides spin; the helper's suppressed yields never let the owner's scheduler run the completer.

## §54 Where the a10 cinematic frame goes (perf121/122, 21:00-21:35 CDT) and what 20 fps needs
- perf121 (sparse snapshot reuse, 811a282) 8 min clean: 76-86 ms, 11.5-12.9 fps; same-phase windows vs perf120
  averaged ~5 ms better (83.5 -> 78.6). `[vertex-capture]` 0 KiB copied, 0 reclaims, streams stage 10.4 -> 4.5 ms.
- perf122 = perf121 + per-thread phase timers (ce69d09) + f_000BD420 (the owner's frame loop) instrumented; 6 min
  clean, timers cost ~3 ms. THE SPLIT (60-frame window, ms/frame, inclusive):
  tick side (owner): FA920 54.5-57.0 (the per-frame tick; the old [object-pass-scope] "1 completed 0" was broken
  accounting), BCB30 29-29.5 (the scene dispatch + the join wait for the previous scene + render-view prepare),
  7EDF0 0.4-1.6, FA500 0.8; scene side (helper): 5DBC0 74.9 / 5D990 74.1 (the scene), 54010 13.9-14.1 (10/frame,
  the ordered callback dispatcher), 5B4A0 11.3-12.9 (17/frame, per model), 606B0 9.5-11.2, 60560 10.1, 62240 8.7
  (182/frame, 48 us each), 5B760 7.9-8.8 (per-model loop), 5B710 6.6, 539C0 5.9, 92890 5.2, 54740 5.0, 602F0 4.1,
  93C00 3.3, 93DD0 3.2, 544D0 3.3, D6B00 3.0, 542F0 2.8, D8C40 2.3, 28320 2.3. Draw HLE inside the scene ~14 ms
  (draw-hle in the frame-time line), of which the stages are indices 3.9, streams 4.5, textures 2.9, program 1.8,
  state 1.6.
  So under the overlap: frame ~85 = scene 75 (helper, the wall) + ~10 ms not overlapped; the tick (55) hides under it.
  The "[scene-thread] owner wait 1-3 ms" figure is NOT the join wait; the owner's BCB30 call (29 ms) is.
- Index stage is not a trust win: `[index-reuse]` per 60 frames hits 3381 / rebuilt 3541 / ineligible 4528, compared
  4 MB, reuploads 1.2 MB: the camera moves every frame so the visible-world index lists genuinely change.
- HONEST: 20 fps in the cinematic = 50 ms = the scene must fall from 75 to ~40 ms AND the tick from 55 to <45.
  Nothing left that is cheap: the draw HLE is ~14 of the 75, the other ~60 ms is the guest render code (ordered
  passes 54010, per-model chain 5B760/5B4A0, 606B0/60560, 62240 per-object). That is the native rewrite of the
  render loops or a second split of the scene across cores (§18 item 4): weeks, not an evening.
- perf123 (built, skipped) / perf124 (running 21:33, 8 min): + the helper trap bail-out (82f149d) and FA920's callees
  timed; read `[tick-phases]` for the tick split (object update vs the rest) - the only side with a possible cheap
  lever left if one callee dominates and has a native path already (object jobs engage 1 pass/60 frames here).
- Pi yp3 (trap bail-out build): 12 min clean at 21:33, no ABANDON yet (the yp2 trap came at ~25 min).
- perf124 (trap bail-out + FA920 timers) 8 min clean, 78-90 ms. Tick: FA920 55 = 109760 x2/frame (the 30 Hz sim tick
  runs 2-3 times per 85 ms frame; at 50 ms it would run 1.5x, so the tick side SHRINKS with the frame: the scene is the
  one target). Per sim tick 27 ms = 900E0 19 + 14A162 7 + rest.
- perf125 (parent/callee timers d0c961b, self time): scene 5D990 incl 72-75, SELF 13.3-13.8 (direct HLE setters and
  pointer calls, untimed); children: 60560 10.4 = 62240 = 63C00 8.75 (181/frame, 48 us each: the lens-flare
  VISIBILITY TESTS, BeginVisibilityTest + Begin + 4x SetVertexData4f + End + EndVisibilityTest per flare, 482 lines,
  124 x87 ops); 54010 8.9 + 5.5 via 54740 = 14.4 (the ordered-pass callback dispatcher, callbacks through pointers so
  untimed: mostly draw HLE); 5B760 8.6 + 5B710 5.9 = 14.5 (per-model chain, 17 models/frame, 5B4A0 self 5.7 incl. its
  pointer calls into A26B0/draw HLE; D8C40 self 2.0); 539C0 6.0 (self 4.5); 92890 5.3 = 602F0 4.3 (5/frame); 93C00 3.3 =
  544D0 3.2; 28320 3.0-3.6 (27F40 1.8-2.7). Flat: ten chunks of 3-14 ms, no 30 ms function.
  REVISED ROADMAP (days, not "weeks" in the vague sense): (1) flare visibility tests 63C00: native rewrite of one
  482-line function or batch the 181 immediate quads + queries in the HLE (-6 to -9 ms, ~1 day); (2) the scene body's
  own 14 ms + the callback draw paths: HLE setter/dispatch cost, measure with XV_HLE_TIMING=1 on this build first
  (-5 ms plausible, 1-2 days); (3) draw HLE stages indices 3.9/textures 2.9/program 1.8/state 1.6 (-3 to -5, 1-2 days);
  (4) 539C0 self 4.5, 602F0 4.3, 544D0 3.2, D8C40 2.0 natives (-8 to -10, several days). (1)-(3) land near 60 ms
  (~16 fps); (4) is what 50 ms needs. Xk query-reuse ([query-reuse] declines) is the COLLISION world query, unrelated
  to the D3D visibility tests.
- perf126 (perf125 build + XV_HLE_TIMING=1), cinematic, ms/frame (calls/frame): DrawIndexedVertices 15.85 (218 =
  73 us per draw; the draw-profile stages sum to the same: indices 3.9 streams 4.5 textures 2.9 program 1.8 state 1.6
  setup 0.6 constants 0.5), D3DDevice_End 4.03 (205 immediate quads = 20 us each, the flare visibility quads via
  draw_immediate_flare), SetTexture 2.72 (472), SetTextureState_Deferred 2.39 (2493), SetRenderState_Simple 1.86
  (1620), SetRenderStateNotInline 1.34 (1129), SetVertexData4f 1.20 (751), Begin 1.14 (205), SetVertexShaderConstant
  0.63, EndVisibilityTest 0.56 (172), SetVertexData2f 0.48, SetStreamSource 0.32, SetIndices 0.27. TOTAL ~32 ms of the
  75 ms scene is OUR HLE code, no guest rewrite needed to attack it. The 63C00 flare cost (8.75) is ~7 ms of HLE
  (Begin + 4 SetVertexData4f + End + Begin/EndVisibilityTest per flare). NtWaitForSingleObjectEx 214 ms/frame (3) is
  other threads' waits summed, not scene time.
  Per-draw 73 us is the lever: the Xbox did a draw in ~5 us. Suspects per stage: indices 18 us (xv_index_cache_select
  + compare + the per-draw reference-mask/bounds scans over every index, [index-coverage] 134 mask builds/frame),
  streams 20 us (capture submit: reuse probe, per-draw xv_gpu_flush of the index bytes, event-flag wake syscall),
  textures 13 us (record_material, 4 stages), program 8 us (ps link lookup), state 7 us. Flare quads 20 us each
  (draw_immediate_flare -> the full draw record for a 4-vertex quad). Plan: micro-profile inside the HLE draw with
  finer XV_DRAW_PROFILE steps, then cut each stage; -15 ms is realistic in days and lands ~60 ms with the sparse fix.
- perf127 (draw sub-stages 6ba3d1d, [draw-prep-sub]), cinematic, ~350 draws/frame: indices 4.0 = index-cache 1.33
  (select+compare+copy, 210/frame) + index-scan 1.39 (bounds/reference scans on the ~130 misses/frame) + ~1.3 before
  the cache (prim_to_gxm/new_cmd/early paths); streams 5.0 = flush 0.29 + capture-submit 3.5 (10 us/draw with ZERO
  bytes copied: reuse probes, cap_collect per submit, publish + event-flag wake) + ~1.2 stream loop; textures 3.9;
  program 1.7; state 1.5; setup 0.5; constants 0.5. FLAT AGAIN: no sub-step above 4 ms. Realistic cuts: submit -1.5
  (collect/wake batching, cheaper probe), textures -1.5 (record_material caching by handle), program -0.5 (strstr
  "halo_vs_16" + 256-entry pspair scan per draw), End flare quads -2 (batch the 4-vertex quads), deferred setters -2
  (SetTextureState_Deferred 2493 + SetRenderState 2749 calls/frame ~1 us each): about -8 ms = scene ~67, frame ~77
  (13 fps). Beyond that the guest natives (63C00 math 2 ms, 539C0 4.5, 602F0 4.3, 544D0 3.2, D8C40 2.0, 27F40 2)
  and the 5B4A0/54010 pointer-called draw setup are what 50 ms needs.
- Pi yp3 (trap bail-out build): 60 min, 1,791 windows clean (no trap/abandon/stall); yp4 2 h started 22:19.

## §55 Gameplay freeze on perf127 (22:28 CDT) and the freeze watchdog
- The user took the controls after the perf127 run and the game froze (image stuck; the system UI still worked, so an
  app-level freeze, no core). The log (first 2.5 MB fetched before the FTP died; xita.1.log holds the rest) shows a
  healthy cinematic at 12.6-13 fps to the last line at 544 s of runtime, then EVERY thread stopped logging at once,
  including the [cpu] poller, and the FTP plugin died too. No STUCK / ABANDON / TRAP / [cs] marker. That is not the
  guest-side hang classes fixed today; suspects: the logger lock (xv_logf) held by a thread that blocked, ux0 I/O
  stalling (the fire script reads the 2.5 MB log over FTP every 30 s while the game writes it), or all cores busy at
  guest priority. Recovery: vitacompanion `quit all` worked; the FTP data channel stays broken until a device reboot.
- perf128 = perf127 + program-stage trim (94ec1fb) + freeze watchdog (cdad823): XV_FREEZE_ABORT=<seconds> makes a
  native thread trap after that long without a presented frame, writing ux0:data/xita/freeze.txt first with sceIo, so
  the next freeze produces a core dump with every thread. Run gameplay sessions with XV_FREEZE_ABORT=20.
- Log fetch caveat: an FTP fetch of xita.log stops at 2,621,440 bytes (2.5 MiB, curl rc=18); resume with -C to get
  the rest. The runtime rotates xita.log -> xita.1.log at every launch, so pull xita.1.log BEFORE relaunching.
- 23:07 CDT, end of session: after the reboot the LiveArea reports Xita as CORRUPTED (unclean shutdown during the
  I/O stall is the likely cause); the companion plugin answers but the FTP server (port 1337) does not, even with
  VitaShell launched (its FTP needs SELECT pressed in VitaShell). NEXT SESSION, in this order: (1) VitaShell + SELECT,
  pull ux0:data/xita/xita.1.log (the freeze log, resume with curl -C past 2.5 MiB) before ANY Xita launch; (2) list
  ux0:app/XITA00001 for the damaged file; (3) upload overlap-candidate/build/xita.vpk (perf128 = perf127 + freeze
  watchdog + trims, 56903ef) to ux0:/ and install it from VitaShell; (4) play sessions with XV_FREEZE_ABORT=20 and
  WITHOUT XV_DRAW_PROFILE/XV_SCENE_PHASES/XV_HLE_TIMING (each costs 2-15 ms). The Vita keepalive was stopped; the Pi
  yp4 2 h soak keeps running unattended (runs/yp4.log), clean at 30 min.
- 23:38 CDT, Pi yp4 (perf128-equivalent harness): the SAME guest trap recurred (idiv at 0011092C, edi=0x40, frame
  ~111,360, about 62 min in) and the new bail-out did its job: `[scene-thread] ABANDON scene 1`, one frame dropped,
  the soak continued (frame 139,620+ and counting, 2,322 windows clean otherwise). Before 82f149d this was the yp2
  two-hour hang. The root cause (a torn/NULL object pointer read by the frozen-page scene in f_001105E0, same class
  as the A26B0 model-guard skip) is still open; the guard-style fix is to validate [edi] before the idiv or freeze the
  array that pointer comes from (`[render-view] arrays:` names them).
- 00:20 CDT: Pi yp4 finished its full 2 h: one trap (the 1105E0 idiv at 62 min) abandoned as one dropped frame, no
  other event, run clean to the end. The trap bail-out is qualified on the Pi; the Vita has not run it yet (perf128 is
  built, not installed - see the recovery order above).
- 08:15 CDT Sept 23: the full freeze-run log (xita.1.log, 6.4 MB, 50,630 lines, ends at 1357 s = my 22:42 quit)
  says the GAME NEVER HUNG: frames presented at 10-12 fps to the end, the game-time counter (gg+C) advancing, the
  camera moving with the user's stick (pad raw lx 236 ly 22 while cam went -44.98,18.25 -> -47.12,16.09), no fault
  lines. Thread 24 (start CFDE0) waiting on "mutant obj .. owner t-1 cnt 0" is the steady state of EVERY overlap run
  (p115/p118/perf121/perf127 alike, from the first storm) and the user played fine on p115, so it is not the cause.
  What the user saw ("stuck", LiveArea reachable) plus the dead FTP and the app flagged corrupted after the reboot
  points at the SYSTEM side (display/network/card), not the guest. Open: get the user's description (image frozen vs
  input ignored), then reproduce on perf128 with XV_FREEZE_ABORT=20 (which only catches a stopped frame counter).

## §56 Sept 23 morning: perf128 baseline, the Pi sampler's owner hotspot (08:00-08:40 CDT)
- The user reinstalled Xita (perf127) after the "corrupted" flag; perf128 (freeze watchdog, capture timing gate,
  program-stage trim) deployed 08:09 and ran 8 min clean with NO profiling knobs (XV_DRAW_PROFILE=0, no phases, no
  HLE timing): 71.7-83 ms, 12.0-13.8 fps in the cinematic - the best run so far. The profiling knobs were costing
  2-8 ms; play sessions must run without them. XV_FREEZE_ABORT=20 is armed (no trip).
- Pi in-process sampler (XV_HOST_SAMPLE, tools/host_profile.py with the cross addr2line via a PATH shim, 7 min run
  sample1, 210 windows): OWNER THREAD (all guest fibers except the scene helper): f_000B8980 63-66 %, f_000B8840
  12 %, f_000AB3E0 3-4 %, everything else < 1.5 %. B8980 (961 lines, called only by f_000B9678, no direct-call
  references to B9678 anywhere: a callback/thread entry) loops over 0x1C-byte records with a valid flag and
  x87 dot products (looks like per-emitter/per-node distance math). HELPER/WORKERS: flat (f_00060000 ~7.6 %,
  54DAE 3.9, 52F50 3.8, 52D50 3.7, nq_run_impl 3.7, ns_solver_fused 3.4, 62B90 1.9). The Pi owner is only ~21 % busy
  (30 fps paced), so B8980 may be a spin/idle pattern there; whether it is hot on the Vita's tick is what perf129
  (timers under 900E0/14A162/B9678/B8840) answers.
- CORRECTION to the Pi sampler numbers above: the sampler's offsets are relative to __executable_start, which is the
  link address 0x10000 for the static armhf harness, so every symbol resolved 64 KiB off (f_000B8980 was really
  f_000BB060, the frame loop's idle spin on a 30 fps-paced Pi). tools/host_profile.py now adds the base (a7deff0).
  Corrected helper+workers profile (gameplay windows): flat, top f_00053E90 4.8 %, x87_load_f32 4.1, xd3d_r_clear
  3.7 (softgfx), f_00061270 3.3 (XV_NATIVE_PACK=2 exists, host-verified, never measured on the Vita -> perf130),
  f_00054010 3.1, getenv 1.1 + strncmp 1.0 (per-call getenv in xv_hle_D3DDevice_Begin and SetTexture, fixed
  2e22c3a), xk_audio_mix 2.0, f_00063C00 1.4, f_000602F0 1.1, f_00070110 1.1, xv_native_crt_float 1.0.
- perf129 [tick-phases] (Vita, cinematic): FA920 57.1 = 109760 55.6 (x2/frame) = 900E0 38.3 -> f_0008FB70 37.5
  (4,061 calls/60 frames = ~34 per sim tick, 0.55 ms each: the PER-OBJECT UPDATE, the work the native object pass
  (FA920 scope, 33 jobs) does in the corridor but engages ~1/60 frames in the cinematic) + 14A162 14.3 -> f_0014E2C0
  14.1 (1/tick, 7 ms) + E7140 1.8. BCB30 22.5 this run. Pi gdb chain for it: 565E0 <- 8D760 <- 8FB70 <- 900E0.
  When the scene drops below the tick, the lever is making the native object pass engage per tick in cinematics
  (or the 14E2C0 tick step, 7 ms, unknown).
- perf129 6 min clean (77-87 ms with the tick timers on). perf130 = same build, XV_NATIVE_PACK=2 (native f_00061270),
  no profiling, launched 08:47 via NO_DEPLOY (note: NO_DEPLOY needs a companion quit/launch to the dashboard first,
  the env is armed there). perf131 built (not yet run): getenv caching in Begin/SetTexture (2e22c3a), the inline
  on-helper fast path in xd3d_count (91375c2), the per-frame texture source memo (edbf2ef, XV_TEXTURE_SOURCE_MEMO=0
  disables, [texture-source-memo] counts). Each is a sub-ms to ~2 ms candidate; measured together against perf128's
  71.7-83 ms window set. Flare quads: not batchable (one visibility query per quad); their path is
  draw_immediate_flare -> xv_d3d_DrawImmediateStrided -> record_draw with stream 0 immediate (no capture submit),
  so the memo and the program stage are what they pay.
- perf130 (XV_NATIVE_PACK=2, engaged: [crt-float] pack 22,180/60 frames) vs perf128 per window: 81.5/77.9,
  80.8/80.4, 80.0/83.2, 80.9/81.0, 80.2/74.0, 67.4/73.1, 71.1/73.6 - a wash within run-to-run variance (the cinematic
  content shifts a little per run; compare same-phase windows and expect +-5 ms noise). perf131 (three trims, pack
  off) and perf132 (perf131 + pack) are chained to run back to back.
- FOUND (09:05): the Vita's `[crt-float]` line shows _controlfp/_frnd/floor 0 while the Pi shows floor 186k/60 frames.
  The hooks are in the stage shards' .c, but build/recomp/code_002.o and code_003.o (compiled 09-22 18:25, the
  "clean rebuild" of the previous session) contain NO xv_native_crt_float reference: those 22 shard objects were
  compiled with a different (smaller) flag set than make-vars.txt; only the 10 shards rebuilt today carry the
  current flags (code_011's pack hook fires). The host-measured XV_NATIVE_CRT_FLOAT gain (-11 % scene samples,
  handoff §40) has therefore NEVER been on the Vita. perf133 = all shard objects deleted and rebuilt with
  make-vars.txt. Trap: after any flag-set change, delete build/recomp/*.o; make's option stamps do not cover the
  shard flags. Verify with `arm-vita-eabi-nm build/recomp/code_002.o | grep xv_native_crt_float`.
- Pi packv (XV_NATIVE_PACK=1 verify mode, 10 min): 20,234,240 native pack calls, 0 mismatches against the guest body
  on ARM. XV_NATIVE_PACK=2 is exact on ARM; it stays on from perf133 (the Vita gain is within noise, ~0-3 ms).
- perf131 per window vs perf128: 81.3/77.9, 81.6/80.4, 81.9/83.2, 78.2/81.0, 71.6/74.0, 72.2/73.1 (texture memo:
  32,133 of 45,503 stage lookups per 60 frames reused = 70 %).
- perf131 complete, 8 min clean: 68.1-81.9 ms, 12.1-14.5 fps; last four windows 70.1/68.2/74.8/70.9/68.1 vs perf128's
  73.6/77.7/71.7/73.1/74.3 (about -3 ms). Best build so far. perf133 (all shards rebuilt with the full flags, native
  CRT float hooks live for the first time on the Vita, native pack on) deploys automatically after the rebuild.
- perf133 (all shards rebuilt, CRT float hooks live: floor 42k/60 frames, pack 22k), 8 min, no core: game 67.6-82
  ms, steady windows 67.6/68.6/70.1/69.1/69.9/71.4 = ~14 fps, about -2 ms vs perf131. BUT two present-path stalls
  of ~6.6 s (windows reporting "wait 111.6/112.5 ms" = one 6.6 s wait each), never seen in perf128-131. GPU
  completion and frame-retire latency normal in those windows, busy-slot waits 0, no fault line. perf134 = same
  install with XV_NATIVE_CRT_FLOAT=0 (kill switch) to test the natives; perf135 = + [present-stall] stage logger
  (7bc3d72: EndFrame / flip / flush / present / BeginFrame / frame_begin / purge split when a present exceeds 300 ms).

## §57 The freeze watchdog's first core (perf134, 09:36 CDT Sept 23)
- perf134 (perf133 install, XV_NATIVE_CRT_FLOAT=0) froze right after the load->cinematic transition (frame 5447,
  326 s); XV_FREEZE_ABORT=20 trapped and the Vita wrote psp2core-1790174187 (1.58 MB gz). ux0:data/xita/freeze.txt
  carries the marker. Symbolized with tools/vita_core_threads.py against a rebuilt perf133 ELF (git show
  7bc3d72~1:runtime/xv_ui_gxm.c into the stage, relink, save, restore); pyelftools' note-name parse had to be made
  tolerant (venv-core .../elftools/elf/notes.py, backup .orig-20260923: a note name without NUL).
- Threads: owner xk_fiber in xv_object_jobs_join <- f_000900E0 <- 109760 <- FA920 (inside the object pass, host wait);
  xv_objects_c0/c1 in xv_object_math_lock (sceKernelDelayThread loop) under f_0004C980 <- 44AD0 <- 90710 <- 90950;
  xv_scene (RUNNING) in xv_scene_thread_proxy_hle's strcmp fast path <- xk_NtYieldExecution <- f_00012AA3 <- 325C0
  <- 80250 <- 80360 <- 70110 <- A2380 (the per-model draw chain, SetTexture area: a guest wait loop with yields for
  a resource the streaming thread produces); every other xk_fiber (streaming, sound, vblank) parked in waits; pump
  and vertex threads idle. The owner's bounded host wait (perf120) serviced the proxy but never let the guest
  scheduler run the streaming fiber, so the helper's wait could never be satisfied and the workers' service never
  completed. Fix (perf136): owner_wait polls the semaphore, services the proxy, and parks with xk_sleep_us(300)
  like the dispatch join. Residual risk: another guest fiber taking the math guard while a worker holds it and
  awaits an owner service (the non-owner-lane path blocks); watch for it.
- Trap: never delete/rebuild build/xita.vpk while a fire script may be deploying (perf135's deploy found no file:
  "DEPLOY NOT CONFIRMED"). Build into a temp name and rename, or wait for "env set".
- perf136 (owner waits parked in the guest scheduler) CRASHED on both benches within minutes of the level: Pi yp6
  SIGSEGV in xv_worker_query <- f_00056670 <- f_00092330 on a guest fiber thread (LWP 8470) while the owner thread
  was in xv_object_jobs_join -> xv_cluster_runtime_begin -> xv_cluster_snapshot_build (xv_cluster_snapshot_geometry
  returned NULL: the snapshot was being rebuilt); Vita core psp2core-1790175027 (game-b) at 09:50. Parking the owner
  lets other guest fibers run natives that assume the object pass's exclusive window. REVERTED (dd1781b, perf137):
  bounded host waits again + `[object-jobs] STUCK <what> <ms>: running, math_holder (lane+1 / 100 owner lane / 200
  other), worker service words` every 4 s of a stalled pass. The perf134 deadlock remains open; the next occurrence
  will name the guard holder. Idea not yet tried: on the helper, a yield-spin longer than N ms could hand the wait to
  the owner as a proxied wait (so the helper blocks in the proxy and the owner's join loop parks the scheduler).
