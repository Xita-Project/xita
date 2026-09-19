# Ordered scene-pass attribution

`XV_SCENE_BUCKET1_DETAIL=1` refines the second main scene interval in primary
`5D410`. The [heavy campaign capture](campaign-critical-path-20260919.md)
measured about 43.5 ms/frame in that interval. This option identifies which
ordered pass grows; it is not a claimed optimization. Default is Off, with the
existing owner/scene observers and Halo CE 3925 required.

Eleven boundaries divide main bucket 1 into twelve disjoint intervals:

| Interval | Start | Next boundary | Included work |
| --- | --- | --- | --- |
| 0 | 5D500 | 5D517 | Optional 5B710 model list |
| 1 | 5D517 | 5D5C3 | 93C00 and two 7BFE0 passes |
| 2 | 5D5C3 | 5D5FC | Optional 54010 with callback 62870 |
| 3 | 5D5FC | 5D693 | Two more 7BFE0 passes |
| 4 | 5D693 | 5D698 | 93DD0 |
| 5 | 5D698 | 5D6F2 | Conditional setup and 54010 with 62810 |
| 6 | 5D6F2 | 5D72F | Conditional setup and 54010 with 622A0 |
| 7 | 5D72F | 5D759 | Conditional 54010 with 627F0 |
| 8 | 5D759 | 5D77F | Conditional 54010 with 627E0 |
| 9 | 5D77F | 5D7B0 | Conditional 54010 with final callback 62980 |
| 10 | 5D7B0 | 5D7DB | Conditional setup and 54010 with 62270 |
| 11 | 5D7DB | existing 5D7ED boundary | State cleanup and optional indirect call |

A skipped branch advances directly to its next executed boundary; its small
branch/setup cost belongs to the previously open interval. These are ordered
intervals, not exact callee self-times. The scene entry shortcut skips this
whole section. Nested drawing, callbacks and waits remain included.

Each cut reuses the main scene token and timestamp. No guest state, draw order,
worker policy, resource ownership, per-draw clocks or extra synchronization is
changed. Owner/thread/context/generation checks occur before timing. Invalid,
stale, foreign or worker calls do not read clocks. Reporting splits the active
interval once and resets totals without ending it. Early cleanup and leaving
main bucket 1 close the detail ledger; its sum reconciles with that bucket.

The `[scene-bucket1-detail]` row reports twelve entry counts and elapsed totals,
completed bucket-1 intervals and current open state. It adds up to eleven clocks
per normal scene invocation; skipped optional passes take fewer. Divide totals
by the row's display frames and inspect the main scene invalid/abandoned/stale
status. Do not add these times to the enclosing scene or nested draw timings.

The generator pins the original instruction body after stripping existing
observers and normalizing only the phase line. It checks every duplicated
frontier count. Stripping the new calls restores the retained body byte for
byte, including bucket0 and existing optimizations. The selective generator's
`--bucket1-detail` mode expects a retained bucket0-detail stage. Only the scene
unit and owner observer own the compile flag; a content stamp handles toggles.

## Validation

Host ASan/UBSan checks cover shared-ledger accounting, skipped cuts, split
reports, early cleanup, invalid order, worker/foreign contexts, stale generations
and backwards clocks. The actual generated primary CFG passes 24 cases and 646
matching child/preemption frontiers, comparing complete context, 8 MiB guest
memory and callback hashes. Children are deterministic stand-ins; this is not a
whole-game or physical concurrency proof.

Six retained ARM build transitions cover default, repeated Off, On, repeated
On, Off and repeated Off. Off restores the preceding complete scene and observer
objects; 106 other objects remain identical. The scene compiler may change other
functions inside that one translation unit, so no finer byte-identity claim is
made. Invalid selector, missing prerequisite and missing-hook checks reject
before compiling. The harness was corrected to isolate this feature's missing
prerequisites from earlier guards for other enabled cumulative features; the
remaining negative checks were run separately after the completed compile rows.

The selective generator also passes against the owned XBE, symbols and retained
stage. Its output equals the directly qualified body. Private receipts, build
rows and generated CFG fixtures are in `../scene-bucket1-detail/`.
Hardware observations follow; no optimization benefit is claimed for the observer.


Perf.24 (`af88b93+`) builds successfully. The updater hash-verifies runtime
`e5624e540cb748e0a4fb54494444012d7854e8e2eedac64cfff7416429fecd71` and
confirms boot in slot 0; perf.23 remains in slot 1. Of 1,744 package entries,
only `game-a.self` and `boot-game.txt` differ. The timed campaign launch sequence
reached the Normal checkpoint and the new observer reports on hardware.


## Hardware: model-list work grows most in the crowded view

The original checkpoint view remains at 78.058 ms / 12.81 FPS and 152.17 draws
per frame across twelve settled windows, essentially unchanged from perf.23.
The new twelve intervals reconcile exactly with main bucket 1 in every selected
window, with no invalid, abandoned, stale or exhausted main scene reports.

After movement, six stationary corridor windows show camera
`(-28.81, 36.54, 0.62)`, forward `(-0.99, 0.06, -0.15)`. The screenshot shows
multiple marines, elites and grunts. These windows average 153.33 ms / 6.52 FPS
and 466.5 draws/frame. They are a different workload from the initial checkpoint,
not a regression measurement. Intermediate side-room and wall-facing captures
are excluded from this table.

| Interval | Original view ms/frame | Crowded corridor ms/frame |
| --- | ---: | ---: |
| 5B710 model list | 1.860 | 22.196 |
| 93C00 / first two 7BFE0 passes | 0.101 | 0.378 |
| 62870 callback interval | 1.859 | 4.739 |
| Next two 7BFE0 passes | 0.037 | 4.348 |
| 93DD0 | 0.032 | 0.052 |
| 62810 callback interval | 0.295 | 0.479 |
| 622A0 callback interval | 1.274 | 3.341 |
| 627F0 callback interval | 0.215 | 0.391 |
| 627E0 callback interval | 1.682 | 4.240 |
| 62980 callback interval | 0.213 | 0.353 |
| 62270 callback interval | 0.861 | 2.002 |
| Cleanup / indirect tail | 0.002 | 0.003 |

All six crowded-view detail ledgers also reconcile with their enclosing bucket,
with no invalid/stale/abandoned/exhausted reports. The captured log has no
searched fatal/stop/GPU-fault/data-abort marker. This short observation does not
establish long-session or combat-input stability. Native resolution remains
selected and remote controls are neutral.

The model-list interval accounts for over half this second section in the
crowded view and grows by about 20 ms relative to the easier view. The whole
scene dispatcher averages 101.42 ms and the tick driver 48.54 ms here; they
remain inclusive elapsed scopes. The model-list interval includes descendants,
draw recording and waits, so 22.20 ms is not promised removable CPU time.

Static inspection confirms `5B710 -> 5B4A0 -> 5B190 -> A26B0`, with recursive
attachment traversal in `5B190`. The same `5B4A0` path is also used by the first
model interval. Its preparation/cache helpers and ordered material publication
share state; the entire list cannot simply be dispatched as concurrent guest
calls. The next candidate should target repeated model state/packet preparation
across these passes with explicit input ownership and ordered publication,
checking downstream draw commands and game state against the retained path.
Do not revisit tiny query-cache capacity changes as the primary frame-time fix.

Private hardware evidence is in `../ce-perf24/`, particularly `corridor/` for
the selected crowded view. `heavy/` is an intermediate side-room view, and
`combat/` is the wall-facing approach; their directory names are not evidence
of a matched heavy workload. `scene-bucket1-detail/model-children.json` and
owned function extracts preserve the static call-chain audit outside Git.
