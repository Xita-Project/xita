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
Hardware pass timings and any optimization benefit remain unverified.
