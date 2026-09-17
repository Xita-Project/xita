# Optional detail within the first scene bucket

`XV_SCENE_BUCKET0_DETAIL=1` adds five coarse boundaries inside bucket 0 of
the existing [scene partition observer](scene-partition-observer-20260917.md).
The repository default is `0`. It requires `RECOMP=1`,
`GAME_PROFILE=halo_ce_3925`, `XV_OWNER_PHASE=1`, `XV_SCENE_PARTITION=1`, and
the existing owner observer enabled at process startup. This is diagnostic
attribution, not an optimization or a measured frame-rate improvement. It
preserves the retained optimization selections, workers and graphics settings.

The sampled campaign checkpoint spends about 25 ms/display frame in this first
section; Blood Gulch observations show that it also grows when looking down the
valley. Its children include both visibility preparation and ordered rendering,
so treating the whole interval as CPU visibility work would be premature.

| Detail | Entry → next boundary | Direct work in that interval |
| --- | --- | --- |
| 0 | existing `5D410` entry → `5D4B0` | prologue/REP and `539C0` |
| 1 | `5D4B0 → 5D4E7` | `10C7E0`, `800E0`, `590F0`, `17FCC0`, `D80C0` |
| 2 | `5D4E7 → 5D4EC` | `92890` |
| 3 | `5D4EC → 5D4F1` | `5B760` |
| 4 | `5D4F1 → 5D4FB` | `542F0`, `54740` |
| 5 | `5D4FB →` existing `5D500` boundary | `60560` |

The new `[scene-bucket0-detail]` row reports six disjoint elapsed intervals.
Their sum equals bucket 0 in the paired `[scene-partition]` row. The six original
main bucket meanings and log format remain unchanged. These are inclusive wall
times: child drawing, callbacks and waits remain included. Do not add nested
draw, stream, query or object timers to them, and do not infer self-time or a
speedup from them.

## Accounting and lifetime

Each additional boundary uses one timestamp to account both the current detail
interval and the enclosing main bucket, then starts the next detail interval.
There is no second detail clock, invocation token, guest local, TLS entry or
work queue. A normal primary invocation takes twelve clocks in total; the
`5D4D2 → 5D8DD` shortcut takes four, visiting only detail intervals 0 and 1.
Early cleanup closes whichever interval is open. Nested primary invocations
remain declined and included in the outer interval.

All admission checks use the existing exact native owner, guest context, live
fiber, generation and invocation token. Foreign workers, stale callbacks and
invalid boundary order cannot take detail clocks. Reporting splits an open
interval at the same timestamp as the enclosing scene report. A rebind abandons
the original open scope without allowing stale cleanup to close a replacement.

`detail.completed` counts closures of **bucket 0**, not completed full scenes.
It can be one while the main scene is still open in a later bucket. Pair the
detail row with the main row's invalid, abandoned, stale and exhausted status;
the detail row does not independently establish validity. As with the original
observer, a split report may contain elapsed time but no new entry.

Compile OFF restores the original scene and observer objects exactly. Compiled
ON with the runtime owner observer disabled still executes five zero-token
detail calls on the normal path; they return without clocks or thread lookups.

## Generation and validation

Run `tools/gen_scene_partition_hooks.py --bucket0-detail` with the matching owned
XBE, manifest, symbols, and the retained stage that already contains the six
main buckets. It validates the complete retained primary body and emits only
`5D410`. Install that body in its existing unit, preserving the unit prologue
and every other function. Python optimized mode, image/profile/symbol drift,
body drift, and missing or duplicated detail frontiers fail closed. Stripping
the five new guarded calls reproduces the complete retained body byte for byte.

Only the selected scene unit and `xk_owner_phase.o` own the new compile flag.
The content stamp supports OFF/ON/OFF transitions and repeated-value no-ops.
Invalid selector values, missing prerequisites or a missing generated marker
fail before compilation.

`tools/test_scene_partition.py --bucket0-detail` passed the observer accounting
and admission cases, then 24 actual generated-CFG cases with 646 matching
child/preempt callback frontiers. Comparisons cover complete host context,
final 8 MiB guest memory and context/arena/page-table hashes at each callback.
Child functions are deterministic stand-ins; this is not whole-game validation.
A separately compiled duplicate-frontier negative control was rejected.

Six retained ARM build rows passed: default, repeated OFF, ON, repeated ON, OFF,
repeated OFF. After bootstrap only the two owning objects recompile; all 92
other retained objects remain byte-identical. OFF restores both original
objects. ON preserves independent `5D7F7` function bytes and relocations, and
the primary scene's local native frame remains 112 bytes. GCC changes some
other compiled functions within the selected unit, so this does not claim
byte identity for every function inside `code_010.o`. No device run, emulator
run, full-game replay or FPS result is part of this qualification.
