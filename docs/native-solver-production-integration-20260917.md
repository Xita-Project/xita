# Guarded collision-solver wiring — 2026-09-17

`XV_NATIVE_SOLVER_FUSION` is an explicit build option, default **0**. It selects
the qualified whole solver only at the matched `172CB8 → 170C10` call beneath
`172BF0`. It does not change the generic entry, game speed, actor transaction,
precision, or production defaults. No hardware result is claimed here.

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

Production owner fibers have **32 KiB native stacks**; object workers have
512 KiB. The untraced solver frame is 1,928 bytes plus a 32-byte adapter. The
independent fixture measured an isolated peak of 2,424 bytes through actual
preempt/stop routines, versus 664 bytes for the original. Formatting/firmware
stack and complete ancestors are outside that measurement. The query and solver
frames are sequential under the same caller, not additive. Full owner native
high-water remains unmeasured.

Private evidence is under `native-solver-production-integration` beside
`native-solver-yield-followup`, `native-solver-independent-review` and
`native-solver-production-boundary-review`. Generated guest sources and objects
remain outside the repository. Integration must retain the exact qualified
candidate code and verify final built stack reports. Hardware enablement and
deployment are separate decisions owned by the main worktree.
