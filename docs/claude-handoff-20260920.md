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
