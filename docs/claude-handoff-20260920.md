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
