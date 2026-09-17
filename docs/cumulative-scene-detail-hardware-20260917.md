# Cumulative build: visibility and model preparation

The physical Vita now runs source `fd993a0`, runtime
`01a03548f8bd071393752b6763fdb7c184aeed2e302bcb3adf5a860e18bd077a`,
in remote-update slot 0. Installation and the new boot hash were confirmed.
The preceding scene/pass build `4d099fac` remains in slot 1 for rollback.
All twenty-one selected optimization paths remain enabled together. This
update adds the [five scene boundaries](scene-bucket0-detail-20260917.md);
the counters are not another optimization or an FPS gain.

Package verification found only the scene and owner-observer objects changed;
92 other retained objects are identical, including the qualified query and
solver. All 1,588 package members preserve the update contract; only the game
executable and boot selection differ. The executable is 31,974,174 bytes.
The additional boundaries preserve the existing primary scene accounting.

## Ordinary hardware gameplay

Startup confirmed native 960×544, triple buffering, normal graphics settings
and the retained model-batch startup selection. Effective clocks were CPU 444,
bus 222, GPU 222 and crossbar 166 MHz. The requested 500 MHz CPU setting was
unavailable. No settings were saved or benchmark mode entered.

New002/New003 Blood Gulch was launched through the normal split-screen menus.
The route viewed red base, strafed and walked away, turned toward the valley,
charged/released the plasma pistol, walked again and paused. Screenshots,
controller inputs, the complete log and boot receipt are preserved privately
in `scene-bucket0-startup`. The final capture at frame 16,811 contains 2,724,003
bytes; the searched fault markers and logger error counters are empty/zero.
This bounded check does not clear the historic intermittent crash.

The six detail intervals sum exactly to the enclosing first scene interval
in all 271 paired reports. Independent review also reconciled 112 raw worker
clock deltas without converting their units into CPU percentages. Selected
windows require complete valid owner/main/detail reports, accepted-pass counts
and clocks, zero inactive-scope passes, and exact agreement between same-pass
joined time and the existing object-batch time. Both neighboring windows must
also be qualified and consecutive, with unchanged rounded camera/direction
and active flags. This removes a pause-straddling window whose camera and
loaded/active flags alone had appeared suitable. Endpoints cannot establish
that every interior frame had no movement.

| View | Windows | Median FPS | Visibility interval | Model interval | Whole scene |
| --- | ---: | ---: | ---: | ---: | ---: |
| Red base | 19 | 7.6 | 18.47 ms | 24.91 ms | 75.22 ms |
| After moving, toward the hillside | 5 | 9.6 | 15.63 ms | 15.87 ms | 60.05 ms |
| Same area, turned toward the valley | 10 | 7.6 | 22.15 ms | 22.06 ms | 76.72 ms |
| After firing and walking farther | 6 | 9.5 | 18.39 ms | 19.81 ms | 62.15 ms |

These are four views of one executable, not before/after comparisons. The
spawn, camera and scene differ from the preceding blue-base trial. No gain or
regression against that trial follows from these FPS values. Intervals are
inclusive elapsed time; model preparation includes ordered draw recording.
Whole-frame draw/stream times are nested and cannot be added to these rows
or subtracted as if they all occurred in the model interval. GPU retirement
and query-prefix reports use autonomous counters; they are contextual, not
measurements of exactly these same display-frame windows.

## Next implementation work

Both visibility and model preparation are substantial in the retained stack.
Two isolated prototypes are being qualified:

- An ordered native portal loop within `532E0`, preserving recursive traversal,
  clipping/math children, shared publication and exact budget/callback state.
- A caller-qualified native material builder within `70110`, preserving shader
  selection, constant uploads, combiner publication and draw order. Existing
  palette/hierarchy batches and stream/query optimizations stay intact.

These candidates are not installed and have no demonstrated savings. Their
inclusive parent timings are not estimates of removable work. Qualification
must cover actual retained children and complete observable guest state.

Compatible changes with inconclusive individual FPS results remain in the
cumulative trial. Correctness qualification, compatibility with the stack and
demonstrated performance improvement are separate findings. FPS gains are not
added arithmetically; the combined executable must be judged after restart in
representative gameplay. Stable 20 FPS and another 5 FPS remain unverified.
