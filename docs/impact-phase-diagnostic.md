# Firing slowdown attribution — perf259

September 26, 2026. Diagnostic candidate building; not yet deployed.

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
