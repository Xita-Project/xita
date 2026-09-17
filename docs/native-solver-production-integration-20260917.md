# Guarded collision-solver wiring — 2026-09-17

`XV_NATIVE_SOLVER_FUSION` is an explicit build option, default **0**. It selects
the qualified whole solver only at the matched `172CB8 → 170C10` call beneath
`172BF0`. It does not change the generic entry, game speed, actor transaction,
precision, or production defaults. Hardware checks are recorded below; no
confirmed frame-rate gain is claimed.

## Build and ownership

Enable with `XV_NATIVE_SOLVER_FUSION=1` alongside `XV_NATIVE_QUERY_FUSION=1`
and that query option's existing prerequisites. This is a cumulative option;
the previously retained paths remain enabled by their existing flags.
`XV_OBJECT_SOLVER_EXPERIMENT=1` is rejected because that old experiment releases
the enclosing transaction. Blank, multiword and non-boolean values fail.

One generator owns `code_028.c`: `tools/gen_native_query_fusion.py` composes
the query wrapper and optional solver wrapper. It reverses only its exact
previous wrappers, validates the owned executable and complete retained closure,
then publishes outputs and finally the shared build receipt. Unexpected caller,
profile-scope or source drift fails before publication. Python `-O` is rejected.
This prevents independent query/solver generation jobs overwriting each other's
caller edits during a parallel build.

The solver body is a separate `solver_fusion.c` unit with its own generated
`solver_primitives.h`. Only this private header forces the previously qualified
inline primitives. The generic header and `code_000.c`, `code_013.c` and
`code_016.c` remain unchanged. The generic solver functions in `code_028.c`
remain available for every other caller and exact-continuation fallback.
The original generic functions cannot re-enter fusion.

Enabling/disabling rebuilds the caller and shared query unit; enabling also
builds the solver unit. Relevant archives are recreated so disabled members
cannot survive. Repeating an unchanged build does nothing. Missing generated
units, primitive header or receipt forces regeneration. A shared primitive
header edit intentionally rebuilds the ordinary generated units too. Both
fused units track the existing motion-profiling configuration.

## Preserved execution contract

The fixed four-record continuation array retains the complete original state.
Overflow calls the original child at its current continuation, after publishing
the original context; it never restarts a partially executed query. Guest
memory order, captured integer/image roots versus global x87/POP roots, PC53,
FPSCR, and all context bytes retain the qualified semantics.

Observers, mapping changes, REP operations and expired budgets see the original
context pointer. Active motion profiling retains site 4 and its original
cleanup scope. Actual worker budget exhaustion is terminal through
`xv_preempt → xv_object_job_stop → abort`; it is not synthetic yield/resume.
The independent production-boundary fixture covered five such stops and two
normal profile-cleanup returns without changing the candidate.

## Evidence and limits

`tools/test_solver_fusion_generation.py` checks owned-input failures, reversible
combined generation, unchanged generic sources, OFF caller code, generic solver
function code, qualified separate-unit ARM code, external imports and stack
usage. `tools/test_solver_fusion_build.py` exercises the real Makefile, host
compiler, dependency files and archive recipes with synthetic generator inputs.
The existing query build regression remains applicable.

Earlier full-call qualification covers all 14 expired-budget sites, all four
rounding modes, FZ/DN and sticky flags, aliasing and callback mapping changes,
plus bounded fallback. Independent review adds real REP observation and moving
aliases. These are authored synthetic packets, not captured gameplay. None of
these instruction counts establishes a frame-rate improvement.

Production CE guest threads and object workers have **512 KiB native stacks**;
the CE bootstrap has 2 MiB. `RECOMP=1` defines `XV_RUN_RECOMP` (Makefile:238),
selecting the actual CE bootstrap in `runtime/main.c:2163`; its `#else` alone
launches the mock scheduler with 32 KiB fibers. Actual guest creation passes
`512 * 1024` in `xk_thread.c:175` to the SCE thread's `host_stack` parameter in
`xk_os_vita.c:339`. The untraced solver frame is 1,928 bytes plus a 32-byte adapter. The
independent fixture measured an isolated peak of 2,424 bytes through actual
preempt/stop routines, versus 664 bytes for the original. Formatting/firmware
stack and complete ancestors are outside that measurement. The query and solver
frames are sequential under the same caller, not additive. Full native
high-water remains unmeasured.

Private evidence is under `native-solver-production-integration` beside
`native-solver-yield-followup`, `native-solver-independent-review` and
`native-solver-production-boundary-review`. Generated guest sources and objects
remain outside the repository. Integration must retain the exact qualified
candidate code and verify final built stack reports. Hardware enablement and
deployment are separate decisions owned by the main worktree.

## Cumulative package integration

Root source `7301a1b` builds with `XV_NATIVE_SOLVER_FUSION=1` and all ten
preceding candidate selections retained. The generated caller, query, solver
and primitive header match the qualified production outputs. The solver object
retains the qualified 73,532-byte section and 1,928-byte local frame.

Compared with the ten-path package, only `code_028.o` changes and
`solver_fusion.o` is added. Existing query, generic and runtime objects match
the parent. The selected caller has one relocation to `ns_solver_at_172cb8`
and retains the earlier query wrapper. The original solver remains linked.
The package preserves all asset payloads and the updater contract.

Runtime `ece322ea…` was installed and boot-confirmed in updater slot 1;
`4c025a02…` remains in slot 0. Ordinary solo Blood Gulch gameplay at native
960×544 and standard graphics completed walking, camera turns, a charged
plasma shot, Warthog driver entry, forward/reverse movement, steering, canyon
wall contacts and exit. The device remains paused on foot beside the vehicle.
The captured log has no searched fault markers and zero logger errors.

The initial spawn (41.10, −90.26, 0.74), direction (−0.16, 0.99, 0), differs
from the previous build. Initial passive windows reported 11.0–11.4 FPS;
vehicle captures showed 8–9 FPS, with a later stationary wall view at 10.5.
These views and workloads do not support an optimization gain/regression
comparison. No five-FPS gain or stable 20 FPS has been demonstrated.

This Blood Gulch run did not repeat campaign, rocket/death or prolonged combat checks.
All eleven paths remain cumulative, and no built-in benchmark or CE emulator
was used. Evidence: `solver-fusion-startup/installation.json`,
`bloodgulch-smoke-receipt.json`, `bloodgulch-smoke.log`, and captured PNGs
under the private validation directory.

The same eleven-path process subsequently loaded New001's Normal campaign
pistol checkpoint through the ordinary menus. Two fire inputs, camera/strafe,
corridor movement, wall contact, reverse and pause completed. The full log has
no searched fault markers; logger error and failed-write counters are zero.
This is a short transition/gameplay check, not prolonged combat qualification.

A separate passive counter check observed 372 completed display callbacks
over a host-timed interval bounded by 30.000–30.239 seconds: 12.302–12.400 FPS.
Benchmark mode remained zero, with no screenshots, inputs or setting changes
during the interval. The active campaign screenshot showed 12 FPS. The counter
comes from the completed display callback and counts presentations, not unique
simulation ticks. This cross-check supports the current reading; it neither
validates every historical benchmark comparison nor establishes a speedup.
The preceding map/load history and live simulation are unmatched.
Evidence: `campaign-smoke-receipt.json`, `campaign-smoke.log`,
`passive-counter.json` and the campaign captures in `solver-fusion-startup/`.
