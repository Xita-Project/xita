# Scene preparation follow-up — September 15

The finer September 14 physical capture accounts for the large scene wrapper:
`5D410` takes 107.84 ms/frame inclusive, but only **0.242 ms/frame** after its
selected children are removed. The earlier 48.90 ms selected-self figure included
those children. Replacing the wrapper itself would miss the measured expense.
These are elapsed times, including native calls and waits, not CPU-cycle totals.

The next diagnostic keeps 48 scopes but replaces inexpensive boundaries with
children of the expensive regions. The addresses remain in the Halo 3925 game
adapter, behind the existing complete-image revision guard. Ordinary generation
without `--phase-timing` still has no timing sites; tracing defaults off.

| Physical parent | Inclusive ms/frame | Newly separated work |
| --- | ---: | --- |
| Scene setup `539C0` | 9.86 | `53540`, recursive `532E0`, `52E10`, `537E0`, and their geometry helpers |
| Model preparation `5B760` | 11.04 | `5A7B0`, `D8C40`, `5B4A0`, and their immediate preparation helpers |
| Flare query preparation `60560` | 10.29 | Packed direction decode `60E90`, query rectangle projection `637A0`, and submission wrapper `63C00` |
| Light work `92890` | 7.27 | `5FE80`, `602F0`, `61270`, `92330` |
| Later rendering `5E270` | 6.82 | `66510`, with shared point/vector helpers measured separately |

Rows come from the same 180-frame historical hardware trace and may overlap.
They are priorities for investigation, not estimated savings. The unchanged
`54010` scope still contains ordered material callbacks and draw HLE. Do not add
its inclusive time to independently logged draw-preparation time.

## Scheduling consistency

The collector requires the serialized guest baton. The object-job experiment
already declines jobs while tracing is enabled. Previously, a bounded trace on
that experiment could therefore compare parallel off arms with a serial on arm.
That difference cannot be described as instrumentation overhead alone.

The Present controller now holds **serial object callbacks in all three arms**
of a `guest-phases` capture. Completion, cancellation and loss of the first-person
view restore the configured object-job policy. It applies after the existing
Present drain, with guest jobs already joined. Other benchmark kinds retain their
own scheduling and settings. This is a serial reference diagnostic; use the
separate object-jobs comparison to evaluate parallel gameplay performance.

Earlier September 14 traces predate the object-job experiment and remain valid
within their original scope. New captures must identify their runtime, selected
scope table and scheduling policy; do not mix reports from different selections.

## Candidate batch boundaries

`60560` walks the retained flare list; the existing deferred-result helper
supports batches of up to 1,024 records. Each matching record decodes a direction,
calculates a query position, projects a rectangle, then submits a visibility
query and writes its area. A possible next split is native preparation
of independent rectangles followed by ordered query submission. The new scopes
must first establish whether projection or submission dominates. Preserve float
spill points, coverage, query generations and brightness barriers.

The scene-setup traversal writes shared visibility structures and the model
wrapper changes shared render globals. Launching their complete translated
callbacks concurrently is not a justified batch boundary. Inspect their output
ownership and merge order before splitting them.

## Validation

Production controller/accounting tests cover serial scheduling in all three
arms, configured-policy restoration on completion/cancel/lost view, complete
60-frame reports and zero disabled timer reads. ASan/UBSan pass. The production
frame-acquisition fixture checks the selected override and preserves every other
benchmark's existing behavior.

The private stage preserves every generated instruction body after removing only
scope prologues; the function table differs only in timing metadata. The VitaSDK
build and package verification pass: all 1,588 entries retain the existing updater
contract, with only `game-a.self` and `boot-game.txt` changed. Runtime SHA-256:
`f2b5de42024a8d9f75f9ec60af0c7d8d521eb6af2e8bcd87af01a2de282fd678`.

That exact runtime booted in the isolated Vita3K instance and entered the Pillar
of Autumn cryo room through the normal campaign menus. A remote capture completed
three valid 60-frame windows, with zero dropped/invalid scopes and a matching
stationary camera. The final off arm emitted no phase windows. No object-job batch
reports occurred during the capture; 16 reports afterward confirm resumed work
on both worker lanes. The host checks establish the same policy for cancellation
and loss of the first-person view.

The 180 traced emulator frames entered `63C00` 52,560 times (**292/frame**),
`637A0` 68,040 times (378/frame), `60E90` 136,620 times (759/frame) and `5C300`
49,860 times (277/frame). These are function entries, including rejected queries;
they are not GPU draw counts. Projection has callers outside `63C00`, so its
whole-program count cannot be assigned entirely to the query wrapper.

The capture runs at the emulator's 20 FPS cap. It validates integration and
identifies frequent paths; it cannot rank Vita costs or show an optimization gain.
The next audit is query submission after projection, which still builds four
immediate vertices through separate HLE calls before recording the draw. Any
batching must preserve query identity and ordered submission. The physical remote
service remains unreachable; no new hardware build has been installed.

Private generated-body proofs, host logs, exact package, boot receipt, trace and
restoration evidence are under `engine-restructure-20260914T2300Z`, particularly
`scene-children-evidence.json` and `emulator-scene-children/`.

The [query submission follow-up](query-submission-audit-20260915.md) records the
actual immediate draw path and the withdrawn projection experiment.
