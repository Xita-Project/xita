# Perf266 current scene attribution

A bounded physical a30 run enabled XV_SCENE_PHASES=1 with other perf266 settings
retained. Lifepod and outdoor screenshots confirm the intended views. This adds
observer overhead; its FPS is not an ordinary performance comparison.

Complete guest-report windows: 10 lifepod, one inside the 12-second held-trigger
segment (includes reload opportunity), 12 outdoors. No depth-overflow messages
or long slot-stall receipts appeared. Reporter output is selective; absence of
a displayed callee is not proof of zero work. Inclusive times overlap and must
not be added across nested scopes or concurrent threads.

| Scope | Pod inclusive/remainder ms | Trigger inclusive/remainder ms | Outdoor inclusive/remainder ms |
| --- | ---: | ---: | ---: |
| Scene 70110 | 7.659 / 5.413 | 7.82 / 5.53 | 10.598 / 7.168 |
| Scene 5B4A0 | 15.315 / 2.337 | 16.51 / 2.37 | 18.488 / 2.318 |
| Scene 53540 | 3.047 / 2.890 | 3.05 / 2.89 | 2.939 / 2.781 |
| Tick 8DDF0 | 12.337 / 5.623 | 12.87 / 5.91 | 10.940 / 4.989 |
| Tick 8FB70 | 46.506 / 3.901 | 56.43 / 4.14 | 27.936 / 3.508 |

Native 70110 remains admitted (5,760 calls per 60-frame pod window, zero guest
fallbacks); its internal phase taps are compiled off. Its remainder includes
untimed native callees, not just native body overhead. The trigger window also
reports C0C60 2.74 ms: prior aligned Pi work already traces this through C02F0
to effect creation and sound/collision paths. Do not repeat those wrapper
investigations as new discoveries.

Static extraction of current 53540 is 140 translated lines, calling 5B9A0,
5BFD0, 5C5E0 and recursive 532E0. Its reported remainder includes that untimed
portal traversal; it is not 2.8 ms of wrapper arithmetic. The next narrower
visibility audit should inspect 532E0 and its existing typed/native subpaths,
then identify repeated work or a verified native boundary. The larger material
and object-transform work remains a parallel conceptual priority, not grounds
for unsafe state caching or reordered transparent draws.

Private evidence: ../scene266-profile/ phase-summary.json, gameplay-phases.json,
full logs, marks, images and owned function extracts. Normal perf266 stays the
installed binary; a cold restart with XV_SCENE_PHASES=0 is being initiated in
../normal266-restored/. No new optimization or FPS gain is claimed by this run.
