# Render preparation on physical hardware — September 15

The draw-scan option saves a small amount of whole-frame time and is retained
alongside exact query overlap. Moving per-draw vertex preparation to core 0
regresses the tested view and remains disabled.

## Comparison conditions

All comparisons used the same query-default runtime, SHA256
`8f7113bca776aa0b1547cd20ccb41a115d66e08d2b1fac6bd5d50d94c7769843`,
on the physical Vita in the Pillar of Autumn cryobay look-around tutorial.
Rendering stayed at native 960×544 with the existing standard graphics,
indexed vertex validation, triple buffering, exact query overlap, and both
experimental object workers. No quality setting changed between arms.

Each off/on/off trial settled for 60 frames and measured 120 frames per arm.
All completed with matching camera checks and restored settings. FPS is
calculated from total measured frames divided by elapsed time, not averaged
from rounded log values. These are live fixed-view comparisons, not deterministic
replays or campaign averages.

| Candidate | Trials | Pooled off ms/frame | On ms/frame | Result |
| --- | ---: | ---: | ---: | --- |
| NEON draw scans | 3 | 173.940 | 172.285 | 1.655 ms saved; enable |
| Core-0 vertex preparation, 16 KiB cutoff | 1 | 172.714 | 177.109 | 4.394 ms slower; keep off |

The scan trials save 1.073, 2.420 and 1.473 ms/frame respectively. Pooled
throughput is 5.749 versus 5.804 FPS, a 0.96% improvement. Later off arms drift
slower, so this is evidence for retaining a modest optimization, not a promise
of the same saving elsewhere. The option combines indexed coverage reduction
with the existing constant comparison path. It preserves all submitted indices,
vertices and draw order. Nearby profiling windows show about 8.0 versus 6.8 ms
in index preparation and 133 large NEON coverage batches per frame.

The preparation worker dispatches about 87 jobs per frame in its enabled arm.
Worker elapsed time is about 19.2 ms, dispatch about 1.0 ms and remaining join
about 21.9 ms per frame. These overlapping categories are not additive. Stream
preparation rises from about 27.3–27.6 to 30.8–31.0 ms. Extra core activity does
not establish a speedup; the complete-frame result decides this default.

A subsequent single depth-preparation comparison on the same original runtime
measures 5.764 / 5.753 / 5.718 FPS. Its on arm lies between the off arms; pooled
frame time changes by only 0.373 ms. This does not establish a repeatable gain,
so early depth-only texture preparation remains off pending further evidence.

## Validation and remaining work

The default-on scan change preserves an explicit `XV_DRAW_SCAN_NEON=0` opt-out
and runtime benchmark restoration. Production host tests pass for these policies,
shader cache/state, index copies, retained GPU snapshots and constant tracking.
ASan/UBSan checks cover 4000 draws over 500 slot generations and 8192 tracked
constant sequences. The index algorithm itself is unchanged by this default
selection; its earlier ARM correctness evidence remains applicable.

The candidate keeps the existing package contract and changes only
`game-a.self` and `boot-game.txt`. Runtime SHA256:
`c761d36856c9b88b12253ef708a9bd7588a29413bb6f36f28aab1060accdd861`.
Private raw logs, phase totals, screenshots, builds and source receipts are
retained under `2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`;
the comparison subdirectory is `render-preparation-followup`.

The exact candidate boots through the normal Split Screen flow into Blood Gulch
in the owned Vita3K instance. Logs confirm default scan activation without an
environment override, real NEON coverage batches, exact query overlap, both
object workers, and disabled vertex preparation. A charged plasma shot consumes
100 to 89 energy, followed by a turn and walk, without a fatal marker in the
captured log. The emulator is capped at 20 FPS; this is functionality evidence,
not a hardware speed measurement or clearance of all previously reported crashes.

Pause-menu navigation still needs investigation: during remote Blood Gulch
testing, directional input moved the character while the visible menu remained
on Resume. Existing UI-root recognition was active in the log, so the cause is
not established. No speculative input change accompanies this optimization.

Representative driving, NPC combat and the previously reported crashes still
need broader validation. Neither the 20 FPS milestone nor the 30 FPS objective
is complete. Next prioritize the remaining draw-preparation cost and shared
object-update dependencies, using larger owned work units where possible.
