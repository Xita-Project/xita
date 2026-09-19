# Detailed draw timing in ordinary performance builds

The draw stage profiler previously treated an absent `XV_DRAW_PROFILE` setting
as enabled. It reads the monotonic clock at draw entry and each stage boundary.
Those repeated reads and calls are diagnostic work; their elapsed hardware cost
has not been isolated. This does not establish that profiling explains the
remaining campaign frame time.

`XV_DRAW_PROFILE_DEFAULT=0` now lets a research build disable these per-draw
clocks at process startup. The project build default remains 1. An explicit
`XV_DRAW_PROFILE=0` or nonzero value still takes precedence. Closing and
relaunching Xita applies the startup policy. The owning object's Make stamp
tracks changes without regenerating guest code.

Joined vertex-preparation/cache counters continue to report and reset when
fine-grained timing is off. FPS, whole-frame intervals, owner intervals, capture
worker timing, CPU utilization and other existing observers remain unchanged.
The `[draw-prep]` stage-time row is absent when disabled; its absence must not
be interpreted as zero draw cost. No game state, shader or submission path is
changed by this policy.

Private direct checks exercise both compiled defaults, absent/explicit startup
settings, and an absent weak clock provider: twelve cases verify that disabled
mode makes no clock reads and both joined reports still run. Native object
builds validate 1→0→0→1→1→0 transitions, no-op timestamps, restored hashes,
and invalid selections. Evidence is under `../draw-profile-policy/`.

## Why overall frame time remains the decision

Recent camera-matched campaign captures show the following nearby medians.
They vary in NPC activity and are not a controlled repeated experiment. Each
observer has its own reporting window; these columns are contextual and must
not be treated as precisely joined exclusive costs.

| Build | Tick interval | Scene interval | Remaining flare wait |
| --- | ---: | ---: | ---: |
| perf13 | 35.56 ms | 40.56 ms | 4.47 ms |
| perf14 | 35.52 ms | 40.66 ms | 5.34 ms |
| perf15 | 35.71 ms | 40.44 ms | 4.03 ms |
| perf17 | 35.79 ms | 40.34 ms | 4.74 ms |
| perf18 | 36.03 ms | 40.20 ms | 4.92 ms |

Perf14 lowered capture preparation by about 0.9 ms while its remaining flare
wait was about 0.9 ms larger than perf13. This is consistent with some CPU
savings becoming additional time waiting for the same visibility dependency;
it does not prove causation. Existing exact query deferral and scene-boundary
publication are already active. Removing their remaining wait without a
replacement dependency proof would change visibility or break resource lifetime.

The next combined research candidate reduces vertex-worker event calls and
turns off the detailed draw clocks, retaining the perf18 optimization stack.
Measure actual event suppression, frame intervals and gameplay after restart.
No predicted FPS gain is assigned to either mechanism before hardware evidence.
