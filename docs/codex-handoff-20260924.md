# Handoff to Codex: Halo CE a30 at 20 fps on the Vita (Sept 24, 2026, 21:40 CDT)

Written by Claude at the end of the Sept 24 session. Read this first, then `docs/claude-handoff-20260920.md`
(long history, §63 has the Sept 24 morning) and the per-feature docs linked below.

## The goal

The user's goal: **20 fps (50 ms/frame) in Halo CE's second level a30, in the intro cutscene (lifepod crash) and
where the Chief spawns inside the pod.** The user measured 10-12 fps there. "The second level is a perfect benchmark."

**Current: pod ~74 ms (13.5 fps), cutscene windows 80-90 ms, canyon-flyover shot spikes to ~170 ms.** Start of the
day: pod ~105 ms, cutscene 100-115 ms.

## Where everything is

| what | where |
|---|---|
| authoritative source | `/home/birchwoodgod/xita-backups/2026-09-18-unified-games/source`, branch `work/2026-09-18-packet-followup` (HEAD `0e3f32a` + this doc). Not pushed. |
| Vita build stage | `.../overlap-candidate/build-x87` (a copy of the tree + generated shards `recomp/code_*.c`, patched in place). Build: `cd overlap-candidate && python3 build_x87.py` (~4 min, log `build-x87.log`). Flags: `overlap-candidate/build-command.json` (Vita) and `make-vars.txt` (host harness) - keep both in sync; make does not rebuild on flag changes, touch the files that test a new flag. Version string: `build-x87/version.json`. |
| package for the Vita | `overlap-candidate/package-codeonly.sh $PWD/xita-perfNNNc.vpk`, then check `update-contract.txt` equals the installed one (compare with `xita-perf192.vpk`). The in-app updater REFUSES a package whose packaged assets differ ("install once through VitaShell"); the 34 a30 combiner `.gxp` added today are embedded in the executable (`shaders/xv_ps_gxp.h`) and moved aside by this script (`new-a30-gxp.list`). |
| deploy | `overlap-candidate/deploy.sh <vpk>` from the dashboard (the chain scripts relaunch first and retry 3x; "Connection refused" = the app was not listening yet). Confirm `"boot_confirmed": true` in `deploy.log`. |
| benchmark | `overlap-candidate/bench-a30still.sh <tag> <minutes> K=V...`: relaunch, set env (with retry; refuses to enter the level if the env did not land), Launch Halo > Campaign > New001 > mission 1 > Normal via `vnav.sh`, NO movement (cutscene, then standing in the pod), fetch the log every 30 s into `gameplay-<tag>.log` / `run-<tag>.out`. `XV_LEVEL=a30` (set by the script) makes mission 1 load a30 from its start. `bench-a30new.sh` = same with scripted walking (`pad-driver-a30.sh`). |
| summarise | `python3 overlap-candidate/gpu-summary.py gameplay-A.log gameplay-B.log` -> pod mean frame (game + wait), scene wall, GPU after-submit, draws; first 8 windows = cutscene. |
| per-run chains | `overlap-candidate/chain-p2NN.sh` (deploy + verify run + speed run + screenshots), e.g. `chain-p208.sh`. |
| screenshots | `overlap-candidate/vnav.sh shot <png>` (in-app remote), `vnav.sh press cross|down|circle`. |
| Vita core dumps | `ux0:data/psp2core-<epoch>-...psp2dmp`; symbolise: `~/github/third_party/venv-core/bin/python source/tools/vita_core_threads.py <core> build-x87/build/xita.elf`. |
| host harness / Pi | `tools/host_build.py` (+`--runtime`), `tools/host_run.sh`; Pi 4 at `ssh pi` (key auth). Agent work dirs with ready harness builds: `d3d-defer-work`, `d3d-record2-work`, `native-1721b0-work`, `native-effects-work`, `native-70110-work`. |

## Device state right now

- Installed: **perf208** (= perf207 + deferred recording, off unless `XV_REC_DEFER` is set).
- Built, not deployed: **perf209** (`overlap-candidate/xita-perf209c.vpk`) = perf208 + the effects native (`XV_NATIVE_EFFECTS`, off by default). Next: verify it (`XV_NATIVE_EFFECTS=1`, add `XV_NATIVE_EFFECTS_FUNCS=all` to include the two slower helpers) then speed-test `=2` against perf207s/perf208s.
- `ux0:data/xita/xita.cfg` = 68 lines (local copy `overlap-candidate/xita.cfg.play-20260924e`; earlier versions `xita.cfg.device-20260924b` (55 lines) etc.). Everything below marked "cfg" is enabled there. The cfg is read at boot AND re-read when Halo is launched from the dashboard; keys set through the remote `env` now win over it (source `ec9e2e1`).
- a30 test save: profile New001 holds the Sept 12 a30 udata save (no game in progress shown); the user's own a10 files are in `overlap-candidate/saves-20260924/udata-B9E-*`. The user's a30 checkpoint of Sept 24 09:26 was overwritten by a test Continue at 10:08 (see "Open issues").

## What was done on Sept 24 (all exact unless stated; each has verify mode 1 and a report line)

| change | knob (cfg) | result |
|---|---|---|
| a30 sky combiners: 34 missing combiner programs captured (`XV_SCENE_THREAD=0 XV_PSPAIR_LOG=1`), generated with `tools/ps_pipeline.py`, compiled in Vita3K (`XVSC00001`, launched directly, no clicks) | built in | black a30 sky fixed |
| looping-sound obstruction: reuse a ray's answer while its ends stay put (`xk_sound_obstruction.c`) | `XV_SOUND_OBSTRUCTION=6` (cfg) | ~28 ms/frame of audio-only collision rays in 108FD0 -> 2B460; verify: 0-13 of ~3,300 answers/60 frames differ (audio only) |
| 4B9D0 natives (BSP sphere query 88110 + solver feature test) | `XV_NATIVE_4B9D0=2` (cfg) | 0 mismatches on the Vita |
| collision vector native (88E90 BSP segment cast, under 1721B0) | `XV_NATIVE_1721B0=2` (cfg) | first version crashed on the Vita (see TRAP 1); fixed; Vita verify 0 mismatches (~4,200 casts/60 frames, ~440 on the scene helper); pod 78.7 -> 74.4 ms |
| D3D recording round 1 | `XV_REC_QUERY/INDEX/DRAW/HLE/CAPTURE=2` (cfg) | 0 mismatches |
| D3D recording round 2 (quad lists read the uncached scratch pool; constant copies; HLE entry) | `XV_REC_QUAD/VSC/ENTRY=2` (cfg) | 0 mismatches; no pod change in its usual state |
| hidden-object skip of the model pass (`xk_occlusion.c` + kind-3 proxy commands in `xv_d3d.c`) | `XV_OCCL=2` (cfg) | the pod renders ~57 of ~180 objects; rules: own samples always win, `XV_OCCL_STATIC=4`, camera cut = >25 deg or >8 units/frame; rectangle proxies (cube proxies cost GPU, reverted) |
| alpha-test proof: drop `discard` where the output alpha provably passes (`tools/gen_ps_alpha_kind.py`) | `XV_ALPHA_PROOF` default on | the GPU was the pod's wall because discard disables hidden-surface removal on the SGX; GPU after-submit 156 -> ~55-70 ms |
| deferred D3D recording on a worker thread (`xv_rec_defer.h`, `xv_rec_queue.h`) | `XV_REC_DEFER` (NOT in cfg) | Vita verify 0 mismatches (1.5 M checks). Works (helper CPU 63 -> 53 ms) but **no frame gain yet** - see next section |
| effects helpers native (7E530, 56F20, 11B60, 11610, 11BD0; 7E420/80360 off by default) | `XV_NATIVE_EFFECTS` (perf209, not on the Vita yet) | host/Pi 0 mismatches; estimate 2-4 ms/frame on the helper |
| parameter buffer size knob | `XV_GXM_PB_MB` (cfg-only) | 48 MiB = no change |

Per-feature docs: `docs/native-4b9d0.md`, `docs/native-1721b0.md`, `docs/native-effects.md`, `docs/d3d-record-speed.md`,
`docs/d3d-record-speed2.md`, `docs/d3d-record-defer.md`; commit messages carry the measurements.

## Where the frame goes now (pod, perf207/208, no phase timers)

- **Scene helper (core 1) is the only wall: ~66 ms.** The owner/tick (core 2) is 41-44 ms and waits ~20 ms/frame
  for the scene. GPU is not limiting (small wait). Core 0 (pump ~8 ms, audio mixer, vertex capture/upload,
  texture decode, render-view assist) ~51%.
- Pi profile of the helper (pod, `d3d-record2-work/pi-runs/prof2.helper.txt`): guest code 60% (flat; f_00070110
  material setup 8.3%, 54010 3.9%, 56F20 2.8%, A2380 2.4%, 66510 2.2%, 53E90 1.9%), recording 19%, libc 7%, heavy
  I-cache misses (1 refill / 25 instructions on the Pi).
- Cutscene (timed): models 23-50 ms, effects 5E270 10-28 ms, 59D80 7-9, 54010 7-9.
- Phase timers (`XV_SCENE_PHASES=1`, built into the stage shards) inflate the tick by ~15 ms; `[hle-time]`
  (`XV_HLE_TIMING=1`) inflates each HLE call by ~0.8 us. Use them for splits, not totals.

## Deferred recording on the Vita (in progress at handover)

`XV_REC_DEFER=2` moves draw recording to a worker (`XV_REC_DEFER_CORE`, `_PRIO` relative to the helper, `_BATCH`
records per wake, `_SPIN_US` poll before sleeping, `_RING_KIB`). Pod results:

| run | worker | frame | scene wall (helper cpu) | notes |
|---|---|---|---|---|
| perf207s inline | - | 74.5 | 65.8 (63.2) | C0 51% |
| perf208s core 0, prio +1 | 44 ms/frame busy (wall) | 74.1 | 67.6 (52.6) | helper waits ~15 ms at drains; C0 86% |
| perf208p0 core 0, prio -16 | 29 | 74.9 | 63.4 (54.6) | pump starved: pump 23.5 ms, present wait 9.4 |
| perf208c2 core 2, prio +1 | 44 | 98.0 | 91.4 | drains wait 38 ms |
| perf208c2p core 2, prio -16 | 12.5 | 74.7 | 66.0 (55.6) | tick FA920 42 -> 63 ms (worker preempts it) |
| perf208p0s core 0, prio -16, `XV_RENDER_VIEW_SPLIT_CORE0=0` | - | 79.4 (wait 15.4) | **52-60 (51.4)** | the scene nearly reaches the target, but core 2 goes to 97%: the render-view early copy that the core-0 assistant did now lands on the owner, and the owner/tick becomes the wall |

Reading: the helper sheds ~10 ms of CPU as predicted, but all three cores are loaded and the worker costs ~2.5x the
recording's inline time on the Vita (core 0 +35 points). **The most promising combination is the last row**: the
scene helper is at ~52 ms there; what is missing is room on the owner core (tick 41-44 ms + the early copy) or on
core 0 for the render-view assist next to the worker. Next ideas: `XV_REC_DEFER_BATCH=16 XV_REC_DEFER_SPIN_US=200`
(fewer wake syscalls: one per draw today), move other core-0 work (vertex capture worker, render-view assist) off
core 0, or find why the worker is so much slower than the inline path (cross-core cache traffic on the records?).

## Open work, in priority order for the goal

1. perf209: verify and speed-test the effects native on the Vita; add to the cfg if it gains.
2. Make deferred recording pay off (above), or drop it.
3. f_00070110 native (material setup, 8.3% of the helper): agent branch `work/native-70110-20260924`
   (worktree `native-70110-wt`, work dir `native-70110-work`); was told to wrap up and document its status in
   `docs/native-70110.md` on that branch. Check it, finish verification, integrate like the others
   (`tools/install_native_*.py` pattern).
4. Hidden structure (BSP) draws in the pod: ~65 zero-sample draws/frame (vs_11/16/40/06 passes behind the pod
   walls) - a draw-level skip in `runtime/xv_d3d.c` could save ~5 ms.
5. Cutscene: the effects pass and the canyon flyover spike (~170 ms, check whether it is streaming/loading).
6. The user can choose a CPU overclock plugin (CPU runs 444 MHz; 500 is requested but not granted to apps).

## Open issues (not perf)

- **Load Level unlock**: after reaching a30 the "Halo" tile stays locked (New001's blam.sav byte 0x1C went 1 -> 2;
  the profile screen still says Pillar of Autumn). Not investigated.
- **Save**: a30 checkpoints only reach the profile through Save & Quit; the user's Sept 24 a30 checkpoint was lost
  (overwritten by a test Continue). Backups: `overlap-candidate/saves-20260924`, old a30 saves in
  `xita-backups/2026-09-12-222123-phase-followup/usb-install/save-backup`.
- Warthog: rider floats out of the seat (seen on the old path, so not the overlap; suspects: native model
  hierarchy, native object basis, x87 splice, CRT float); blocky squares on the Warthog (likely missing combiners
  like the a30 sky: capture Blood Gulch with `XV_SCENE_THREAD=0 XV_PSPAIR_LOG=1` and run the combiner pipeline).

## TRAPS

1. **`__thread` is emutls on vitasdk and was SHARED between the owner and the scene helper threads** (the first
   1721B0 native crashed on hardware; `__gthread_active_p` in the core). Natives and anything the scene helper
   calls must keep no `__thread`/static mutable state. 4B9D0/92330 still have `__thread` journals but only in
   verify mode (fine at `=2`, never run their verify modes concurrently with the helper).
2. The dashboard hand-off re-reads `xita.cfg` (fixed: remote env keys win, `ec9e2e1`). The remote `env` call can
   fail with "Connection refused" right after a relaunch; the bench scripts retry and refuse to continue.
3. Code-only packages only (see "package for the Vita" above).
4. `XV_D3D_HIST_LEVEL=<frames>` for a full per-draw trace (with `XV_SCENE_THREAD=0`); `vita_remote.py
   trace-draw` only arms the vertex trace. psdef/pspair and draw-trace lines are dropped from the scene helper;
   phase/hle-time reports only print WITH the scene thread.
5. Never use vitacompanion `press`/`nosleep` on the user's console (buttons went dead system-wide once). Keep-awake:
   `python3 tools/vita_remote.py --config <cfg> lease 3600` every 5 min. A lock screen blocks launches - ask the user.
6. Never print or copy `/home/birchwoodgod/xita-backups/vita-remote/3357-9AA2/remote-client.json`. Repos stay
   private, do not push unless asked; never commit game assets, generated guest code (`code_*.c`), captures, or
   generated shaders/psdefs (they are .gitignored on purpose).
7. Kill processes by PID. `pkill -f`/`pgrep -f` with a pattern in your own command line matches your own shell
   (also a remote `bash -c '! pgrep -f X'`).
8. Don't deploy or relaunch while the user is playing; ask first. No on/off/on automated A/B toggling on the Vita:
   compare runs window by window with `gpu-summary.py`. No Vita3K performance claims.
9. Launch variance is ~5 ms between launches; judge pod means over 20+ windows.
