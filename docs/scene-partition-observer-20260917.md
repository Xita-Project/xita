# Optional scene partition observer

`XV_SCENE_PARTITION=1` divides the primary Halo CE 3925 `5D410` scene
function into six sequential elapsed-time buckets. It requires `RECOMP=1`,
`GAME_PROFILE=halo_ce_3925`, and `XV_OWNER_PHASE=1`; the repository default is
OFF. The existing owner observer must also be enabled at process startup.
This is a diagnostic extension, not a rendering optimization or an FPS result.
It leaves all retained optimization selectors, workers and graphics settings
unchanged.

The current owner measurements put approximately 41 ms/display frame inside
`BCB30` at the sampled campaign checkpoint, with approximately 16 ms in nested
draw HLE work. These are inclusive intervals: the approximately 25 ms difference
is unattributed elapsed time, not a proven CPU self-time floor. The new buckets
identify a smaller source boundary before further restructuring.

| Bucket | Entry → next boundary | Examples of contained calls |
| --- | --- | --- |
| 0 | `5D410 → 5D500` | `539C0`, `10C7E0`, `D80C0`, `92890`, `54740`, `60560` |
| 1 | `5D500 → 5D7ED` | `5B710`, `93C00`, `7BFE0`, `54010`, `93DD0` |
| 2 | `5D7ED → 5D80A` | `10C300`, `59D80`, `110C80`, `17F350`, `5E270` |
| 3 | `5D80A → 5D8C1` | later `7BFE0`, `54C10`, `73FD0`, `54010`, `73A80` |
| 4 | `5D8C1 → 5D8DD` | `606B0`, `D6B00`, `6BF30`, `D35A0` |
| 5 | `5D8DD → return` | existing capture, clear and cleanup tail |

These address-based labels intentionally do not classify a whole bucket as
CPU preparation, GPU work, or a wait. Draw HLE, stream preparation and query
waits can be nested in them. Divide elapsed totals by the report's display
frame count; inspect entry counts separately for repeated scene invocations.
Never add nested draw/object/query timers to these totals. Work outside primary
`5D410`, including the menu path through `5D340`, remains outside these buckets.

## Admission and accounting

Only the exact presenting native owner, live guest context, current real fiber,
and owner generation are admitted. Object workers and foreign native threads
are rejected before reading owner-only scheduler state. A per-invocation serial
prevents an old cleanup from ending another invocation in the same generation;
serial or generation exhaustion fails closed instead of wrapping.

Each boundary takes one timestamp to close the previous bucket and start the
next. A normal invocation takes seven clocks in total, and the existing
`5D4D2 → 5D8DD` shortcut takes three. Cleanup handles every emitted return.
Nested primary invocations are explicitly declined and remain included in the
outer bucket. No pointer into a guest or native stack is retained. A presenting
owner change abandons an open interval; stale callbacks cannot close the new
one. Reporting splits open outer scopes and the current bucket using one shared
timestamp. Entries can therefore be zero in a later report containing the end
of an earlier open interval.

`xv_owner_phase_active(context, phase, &generation)` exposes clock-free ancestry
for the separate object-pass observer. A zero token binds after admission; a
nonzero token must match the same generation. Results are `-1` for unknown or
invalid, `0` for a valid owner outside the phase, and `1` inside. This API does
not change guest state, clocks, scheduling, or worker policy.

## Selective generation and build

Run `tools/gen_scene_partition_hooks.py` with the matching owned XBE, manifest,
symbols, retained stage and a new private output directory. It verifies the
profile and re-emits only primary `5D410`, comparing the complete original body
before writing anything. Install the returned body in its existing unit without
changing the unit prologue or any other function. The original instruction/body
hash and every duplicated frontier count are checked. Independent interior root
`5D7F7` remains untouched. Python optimized mode is refused.

Only that selected guest unit and `xk_owner_phase.o` own the build flag. A
content stamp handles OFF/ON/OFF transitions; repeated values are no-ops.
Missing hooks, an unsupported profile, or missing owner observation are build
errors. The selector does not enable legacy `XV_PHASE` instrumentation.

## Bounded validation

`tools/test_scene_partition.py` checks observer accounting and admission, the
actual generated primary CFG with deterministic child/HLE stand-ins, and real
retained ARM object transitions. The callback fixture compares the complete
host context, final 8 MiB guest arena, and context plus arena/page-table hashes
at each child or expired-budget callback. It tests both scene shortcut paths,
optional ordered passes and indirect callbacks. It does not execute complete
child game logic, all game paths, or physical hardware.

The saved qualification passed 24 generated-CFG cases and 646 callback/preempt
frontiers, 18 existing owner-observer fresh processes, and six real retained ARM
build transitions. After bootstrap, only `code_010.o` and `xk_owner_phase.o`
recompile on a selector change; repeated values compile nothing. All 92 other
retained objects remain byte-identical. OFF restores the original scene object
exactly. ON preserves the independent `5D7F7` function bytes and relocations,
but GCC changes some other compiled functions within the selected unit; this
is not a claim that all other functions in `code_010.o` are byte-identical.
The primary scene's local ARM stack frame remains 112 bytes in both builds.

The observer fixture covers early return, nested entry, split reports, worker
and foreign-thread rejection, mismatched context/fiber/state, stale generations,
invalid boundary order, backwards clocks and serial exhaustion. The existing
owner fixture also checks the generation-token API and retains its ASan/UBSan
and thread-sanitizer checks. Physical timing interpretation still requires an
ordinary gameplay run with the compiled diagnostic enabled.
