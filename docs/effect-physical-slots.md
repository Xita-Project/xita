# Effect-processing physical x87 slots candidate

Status: perf260 deployed; hardware qualification in progress. Goal remains
sustained 20 FPS on Vita. Entries below are chronological records.

The Pi effect profile identifies 1122A0 as a recurring cost. Existing x87
register lowering rejects it as `slot-range`: its logical slots span -3..5,
nine names mapping onto eight physical slots. Removing the check without
changing the representation would retain stale aliases.

The opt-in `--x87-regs-physical-slots 1122A0` canonicalizes operand names,
declarations/reloads, and dirty spills modulo eight. Logical depth analysis,
join rejection, call effects, and runtime call guards remain intact. Default
lowering is unchanged; no game build enables this option automatically.
The report marks selected physical-slot plans explicitly.

Validation so far:
- `tools/test_x87_physical_slots.py` passed 16,384 cases each on host
  ASan/UBSan and Cortex-A9-targeted Pi ARM (cores 0/1). It exercises multiple
  wraps in both directions, aliases, popped slots, every initial stack top,
  bit-pattern preservation, and callees replacing all slots/status.
- Default prologue/fill/spill/context output matched the preceding revision
  across 4,200 synthetic configurations. This is not full-regeneration proof.
- Target regeneration succeeded: 613 x87 instructions, 42 sync sites,
  12 guards, logical slots -3..5. Only 1122A0 was selected.

These tests cover representation and synchronization, not the full function's
arithmetic, branches, memory writes, or real callee behavior. They do not
establish speed or gameplay correctness. Full-function differential tests and
hardware qualification are required before integration.

Private artifacts: `effect-physical-slots/host`, `arm`, `regen`, `regen.log`.
The regeneration completed (session 93762 terminal). A matching no-register
baseline regeneration completed successfully (session 81276 terminal). Output is `effect-physical-slots/baseline`
and `baseline.log`. Nothing is running on the Pi now.

Next: compare regenerated baseline with the maintained target body; build a
full-context/memory differential harness with explicit callee contracts and
representative paths, then validate actual game queries/captures before any
Vita deployment. Preserve the known-good rollback and all existing hooks.

The regenerated baseline target body is byte-identical to the maintained
point-location stage target (`baseline-identity.json`). No jobs remain active.

## Full-function synthetic differential test

Added `tools/test_effect_registers.py` and `tools/tests/effect_registers.c`.
Both complete generated bodies run from identical context and 4 MiB memory;
the fixture compares all context bytes, all memory, and hashes of context and
arguments observed at every synthetic callee. Stub callees modify scratch
registers, flags and every x87 slot. The iterator returns one item then ends;
the callback can change stack depth to exercise guarded fallback. Label-based
step limits and a process timeout reject runaway fixtures; a callee-count
assertion rejects vacuous all-early-exit runs.

Initial fixture attempts were rejected: one exited before reaching callees,
and another never terminated its synthetic iterator. Both fixture defects were
corrected before accepting results. Final 1,000-case runs passed on host
ASan/UBSan at O1 and Cortex-A9-targeted ARM at O2 on Pi cores 0/1. Each recorded
7,875 synthetic callee observations. Build commands and body hashes are in
`function-host/build.json` and `function-arm/build.json`; result receipt is
`function-results.json`. All test sessions are terminal, no jobs remain live.

This covers selected finite-input effect paths, not every discovered branch,
real callee implementations, or arbitrary exceptional floating-point inputs.
The 4 MiB fixture deliberately aliases the guest address space and is not a
model of the full game memory map. Per-label observers affect code generation,
so test execution time is not an optimization benchmark.
Next integrate only 1122A0 into a private Pi gameplay harness, preserve all
other maintained bodies/hooks, and inspect actual gameplay/profile behavior.
No candidate has been deployed to Vita and no FPS improvement is established.

## First gameplay harness run

Integrated only 1122A0 into the private `gameplay/` shard, restoring the same
callee timers in its register and fallback paths. `splice-audit.json` proves
the original target matched regeneration after removing observers, everything
outside the target stayed byte-identical, and candidate changes beyond lowering
are observer insertions only. ARM build and link succeeded; session 32271 is
terminal.

`codex-effect-physical-20260926` completed its planned 180-second timeout (124),
with 68 host reports; run 40867 is terminal. The headless run reached both
scripted firing bursts. It is not a visual/gameplay equivalence test. Across
36 common host report indices in ranges 900–1740 and 2400–3600, the target's
reported mean self time was 0.5417 ms baseline versus 0.4836 ms candidate;
inclusive was 0.9814 versus 0.9056 ms. Timings are rounded, instrumented elapsed
values from independent runs and include scheduling effects. Do not extrapolate
the difference into Vita FPS or call this a controlled paired experiment.
Receipt: `gameplay/profile-summary.json` with all selected rows.

Both logs print 39 guard-miss messages at the existing unrelated A43DD site.
The logger prints only the first 32 misses and each 1024th, so these messages
cannot prove no candidate guard misses occurred. There were no fatal/trap
messages found by the recorded scan; absence alone does not prove correctness.
The candidate ARM function is 103,228 bytes versus 66,648 bytes baseline
(instrumented O1 build), a material instruction-cache tradeoff.

A separate Vita SDK compilation of the candidate shard is still active as
local exec session 42571; poll that handle. Output/log are
`gameplay/code_024-vita.o` and `gameplay/vita-unit.log`. No VPK has been built
from it or deployed. Vita port 8080 still refuses connections; FTP1337 responds.
Next finish the SDK compile, assess release-shard integration and code size,
and prepare hardware qualification without replacing the installed rollback.

## Hardware candidate preparation

The separate SDK unit compile completed successfully (42571 terminal). Its
instrumented Thumb target is 61,740 bytes versus 40,812 bytes in the perf259
release object, but differing timer coverage makes that an imperfect size
comparison (`gameplay/vita-code-size.json`). Use final release objects instead.

Prepared private `effect-physical-slots/vita-candidate/build-x87` by copying
perf259. The target body matched the regenerated memory baseline byte-for-byte
before replacement. Only 1122A0 and build identification changed in the stage;
no new sound-cache runtime is included. Label: `0.2.0-perf.260`, build revision
97c49789. `splice-audit.json` records the candidate body hash. All preexisting
asset/runtime contracts and the developer remote build mode are retained.

Full build is active as local exec session 21283. At last inspection make and
cc1 were live, with cc1 using a CPU; no build error was reported. Poll this
handle instead of restarting. `build-x87-result.json` is authoritative only
after this build terminates. After success, run the prepared `package.py` in
`vita-candidate/`: it preserves the perf258 package's dashboard/assets and
replaces only game-a.self and boot-game.txt, requiring the same update contract.
Expected output `xita-perf260c.vpk`; it has NOT yet been packaged or deployed.

FTP log retrieval succeeded (`effect-physical-slots/vita-before260.log`,
8,963,092 bytes), but MDTM remains 20260926181602. It ends with prior gameplay
telemetry, providing no new startup/failure evidence. HTTP8080 still refuses
connections; do not assume a newly launched dashboard or renew a lease by
claim alone. Last confirmed installed runtime remains perf258.

Packaging follow-up: full build 21283 and package session 79689 completed
successfully. The update contract remains
775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897
(consult package-receipt.json for authoritative hash spelling).
Runtime SHA-256: 83b9e5eddb352e2c3afd84ee3daa8aec973f87361cdd631b5e34405287dbb733,
34,805,430 bytes. Package SHA-256:
18200be5e5e7715aaaf160f438648d98f861bdd063e96584cc1c24765e279dfa.
Archive entry comparison against perf258 confirmed only game-a.self and
boot-game.txt changed. All jobs are terminal. Perf260 is ready as a hardware
candidate but remains uninstalled and unverified on Vita.

### Explicit fallback coverage (2026-09-26)

The differential fixture now counts `xv_x87reg_miss` callbacks and requires
both fallback and non-fallback cases, including fallback from all eight entry
TOP positions. The first stronger assertion exposed a fixture correlation:
`scenario & 7` selected both entry TOP and effect paths, so all 62 fallback
cases started at TOP 0. Selecting TOP from independent scenario bits removes
that gap. This was a test coverage defect, not an observed lowering mismatch.

Host O1 ASan/UBSan and Pi Cortex-A9 O2 Thumb each passed 1,000 cases with
7,875 synthetic callee observations, 62 fallback cases, 938 non-fallback cases,
and entry-TOP mask `ff`. The observed fallback call site was `00112983`; this
does not establish coverage of all 12 static guards or actual game callees.
Full guest context, arena memory, and callee observations still match.
Private build receipts are under `effect-physical-slots/guard-host` and
`effect-physical-slots/guard-arm`. No runtime/package changes were made.
Perf260 remains ready but uninstalled: the Vita control endpoint still refuses
connections. These tests establish no new physical-Vita FPS result.

## Hardware deployment, 2026-09-26

After the authenticated endpoint returned on perf258, installed the prepared
`xita-perf260c.vpk` through the code-only updater. `vita-candidate/deploy.log`
confirms all 34,805,430 bytes, matching SHA-256, restart requested and boot
confirmed in slot 1. A separate status request reports `0.2.0-perf.260`,
revision `97c49789`; perf258 remains slot 0 rollback. Session 3663 completed
successfully. Renewed a one-hour awake lease after restart.

Started the existing a30 launch sequence adapted for version/path and a
45-second initial wait. It retains isolated `XV_TEST_SAVE=a30-perf211`,
360p and the existing qualified optimization stack, with phase instrumentation
disabled. Private artifacts are `vita-candidate/launch-a30.py`, `.log`, and
`a30-env.json`. Deployment success is not performance or correctness proof;
settled scene identification, frame intervals, firing, and regression checks
are still required.

Launch session 87563 and follow-up collection 36589 completed. Save reads in
`a30-first.log` confirm the isolated namespace and `a30.map`; however both
`a30-after-sequence.png` and `a30-loaded-check.png` show loading/black output,
not the playable scene. Follow-up reports timing frame 4417 and continued
two-draw frames. The 4200+ interval summary (41.93 ms mean) is therefore
explicitly excluded from gameplay evidence. Next inspect loading progress
and scene readiness; do not report this number as a performance gain.
