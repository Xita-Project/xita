# Firing slowdown attribution — perf259

September 26, 2026. Build and package checks passed; deployment pending.

Perf258's ordinary lifepod AR sample remained about 75.8 ms/frame while firing
versus 56.8 ms idle. Its inverse-transform register lowering did not establish a
frame-rate gain. The earlier owner-only perf257 profile shows two distinct
contributors worth separating before another native replacement:

- Particle impacts: 10E240 → 1746F0 rises from unreported in the settled idle
  window to 2.79 ms/frame in the mixed firing/cooldown window. Static inspection
  shows calls to 112070 and 2B610, constructing effect and sound inputs.
- Object updates: 109760 → 900E0 rises from 49.03 to 56.00 ms/frame between the
  idle and mixed idle/firing reports; 90950 → C0EA0 and C3A00 also appear among
  the larger entries. These are inclusive instrumented wall times, with waits
  and scheduling included, and cannot be added to their parents or treated as
  an exact decomposition of the uninstrumented firing penalty.

The uninstrumented perf258 reports provide a separate check. Window ending
6360 is wholly inside idle; window ending 6480 overlaps firing and its first
cooldown frames. FA920 inclusive elapsed rises from 45.053 to 64.719 ms/frame;
scene-helper CPU from 48.36 to 56.68 ms/frame; scene wall from 49.05 to 59.59.
Draw count rises from 275 to 312, while the reported final wait changes from
1.1 to 0.8 ms. These overlapping paths must not be added. The final wait does
not cover every possible GPU dependency; it does not prove the GPU irrelevant.
The evidence favors examining extra update and scene-preparation work before
attributing this firing penalty to a full-GPU finish stall. Receipt:
`collision-transform-candidate/ar258-frame-paths.json`.

The diagnostic retains perf258 and adds direct-call scopes inside 1746F0,
112070, 2B610, C0EA0 and C3A00. The 158 emitted call sites include duplicate
translated paths; that count is not a per-frame call count. No gameplay,
physics, effects, sound, draw ordering or worker policy is changed.

```sh
python3 tools/patch_scene_phase_timers.py PRIVATE_STAGE/recomp \
  --parents 001746F0,00112070,0002B610,000C0EA0,000C3A00 --any-call
```

All translated shards were compared against perf258. Only the five selected
function bodies differ, and stripping observer pairs makes every shard
identical. The patcher's two host tests pass. The private candidate directory
`impact-phase-candidate/` contains the build, patch audit and scripts; generated
game code stays outside Git.

Use the protected `XV_TEST_SAVE=a30-perf211` launch with owner-only
`XV_SCENE_PHASES=2`, then settled idle/fire/cooldown input brackets. Inspect the
before/after screenshots and complete frame intervals. Reject scope overflow or
abandoned scopes. Timers perturb performance: this run selects the next target;
it is not an FPS comparison against perf258. Return to a cold launch with
`XV_SCENE_PHASES=0` for performance acceptance.

The next decision is whether impact allocation/initialization, sound creation,
or the growing object-update subtree warrants implementation work. Do not
assume that removing particle math can recover the entire firing penalty.
The sustained-20-FPS, NPC combat, cutscene and long-session gates remain open.

## Build receipt

Full developer Vita build succeeded. The compatible package replaces only
`game-a.self` and `boot-game.txt`, preserving the installed asset contract.
Runtime SHA-256: `ff7ff12ee5e425136fceb82d1058e379098cd1d601ab0676c609c598224ea37d`.
Runtime size: 34775314 bytes. Hardware results remain pending.

## Deployment checkpoint

The companion accepted quit/launch, but the in-app HTTP endpoint remained
connection-refused during the initial 60-second readiness check. The first
update command failed before upload; perf259 is not confirmed installed.
FTP and companion remained reachable. The previous gameplay log stopped at
2026-09-26 18:16:02 UTC; no new freeze-lights dump was present. This observation
alone does not establish a crash or justify repeated restarts.

A bounded `impact-phase-candidate/wait-dashboard.py` observer is running (exec
session 63856). It polls the existing launch for up to ten minutes and only
retries the update after receiving perf258 dashboard status with timing frame
zero. Poll that same handle and `wait-dashboard.log` / `deploy-retry.log` before
any further action. It does not relaunch the app. If installation becomes
boot-confirmed, run `launch-a30.py`, inspect readiness with `wait-game.py`, then
`ar259.py` (two four-second bursts with a reload) and `read-phases.py`. The latter
labels only complete 60-frame reports wholly inside an input phase as such;
mixed windows remain explicitly marked. Do not run the firing script before
visually confirming the protected gameplay scene.

Build 56643 and packaging 36333 completed successfully. Initial deployment
session 7208 is terminal failure. Keep-awake refresher PID 134059 is still alive;
its in-app renewal cannot succeed until HTTP returns. Do not use companion
press/nosleep as a substitute. Source changes are locally committed, not pushed.

## Pi attribution (completed)

The 180-second ARM run on cores 0/1 completed its planned timeout (124), with
66 frame reports. It used the existing native-object-query host harness and
four recompiled shards with the same five instrumented function bodies as the
Vita candidate (byte equality checked). The surrounding host runtime is older,
headless, and has different scheduling; these are supporting attribution data,
not Vita timings, a visual test, or an optimization acceptance result.

Script: campaign buttons at frames 150/300/450; fire at 1800 for 120 frames,
reload at 1930, fire at 2040 for 120 frames. The report labels are host frame
markers and asynchronous reporting may straddle an input boundary. No exact
weapon-tick alignment is claimed. The new impact/weapon call reports establish
that the input exercised these paths.

- Near report labels 1800 and 2040, C0EA0 costs 1.62/1.76 ms per host frame,
  including C0C60 at 1.52/1.66. C0EA0's residual is only 0.09 in each.
- At labels 1860 and 2100, 1746F0 costs 0.75/0.75, including sound entry 2B610
  at 0.68/0.69; its child 26B10 accounts for 0.62/0.65. This directs the impact
  investigation toward sound setup. It does not mean effects are absent or
  dispensable: small children can fall below the report's detail threshold.
- C3A00 at label 2040 costs 0.56, including C2D30 0.31 and C26A0 0.15. It is
  a smaller lead here than C0C60.

Private evidence: `impact-phase-pi/{body-audit.json,phase-windows.json,run.log}`
and `d3d-record2-work/pi-runs/codex-impact-phases-20260926.log`. No tick-phase
overflow/abandon notices were found. Parent times overlap with their children.
Next inspect C0C60 and 26B10's children to choose a useful replacement boundary;
rewriting only their small caller wrappers would miss most of this measured cost.

The Vita readiness watcher 63856 subsequently ended without an HTTP response;
no retry upload happened and perf258 remains the last confirmed installed build.
Companion display-on was requested without pad injection or nosleep. A user
screen-status question is pending. Do not silently restart the failed watcher.
