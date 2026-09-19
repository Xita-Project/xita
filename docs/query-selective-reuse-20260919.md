# Selective world-query reuse

Follow-up: [perf.21 retention changes](query-retention-20260919.md) separate
candidate history from completed transactions after the trial documented here.

`XV_QUERY_REUSE=1` connects the qualified CPU/memory recorder to the selected
`172C95 -> 171F94` world query. It is an experimental, default-off build option.
The dynamic-object continuation and ordinary query implementation remain intact.
See [CPU replay qualification](query-cpu-replay-20260919.md) for the dependency
and floating-point contract.

## Admission and lifetime

The adapter owns 16 fixed entries (349,312 bytes in the host fixture). It uses
the existing world-query admission and actor guard; it does not unlock shared
game state or add a blocking mutex. A busy flag makes nested/concurrent calls
take the original path if a callback temporarily releases that guard.

A coarse key includes registers, guest input bytes, FP control/status and helper
configuration. It is only a lookup key: a hit still requires complete recorded
CPU dependencies, guest mappings and read-before-write bytes to match before
any replay store. Root changes discard entries. Lost callback admission requests
a deferred reset without touching borrowed entry storage.

Eight observations in distinct joined simulation passes and at least 32 budget
units of prior query work are required before capture. There is at most one
capture per 32 passes; rejected records cool down for 64 passes. Short searches
continue through the original translation. These are initial heuristics, not
evidence that real gameplay repeats sufficiently often.

## Validation

Host and ASan/UBSan tests cover promotion, replay, atomic rejection, throttling,
cooldown, roots/configuration, aliases, callback reentry, lost admission, epoch
wrap and joined reporting. Generation tests verify default source identity,
one selected wrapper change, qualified capture output, no-op regeneration,
restoration when disabled and invalid-option rejection.

The actual ARM adapter fixture compares the entire guest arena, context and
native FPSCR after each of 12 repeated calls. Three expensive fixtures each
execute seven original calls, one capture and four successful replays. Changed
geometry, CPU dependencies and short budgets correctly fall back to the original
query. The short negative fixture never captures.

| Fixture | Original ARM instructions | Adapter miss | Capture | Warm hit |
|---|---:|---:|---:|---:|
| Split traversal |103,835|104,553|1,689,880|10,741|
| Winding |40,161|40,879|612,557|7,689|
| Edge |38,338|39,056|570,700|7,719|
| Short negative |1,215|1,933|—|—|

These counts exclude firmware copy cost and real hardware scheduling/cache
effects. Capture is expensive and needs many future hits to pay back. A useful
instruction reduction on synthetic warm queries does not establish an FPS gain.

Private fixture sources, ELF, commands, results and source hashes are retained
under `../collision-query-reuse/qualification/`; owned game bodies are not
included in this repository.

## Hardware trial

Perf.20 retains the prior cumulative options and enables this adapter. The
remote updater verified runtime SHA256
`431a03ef08c9ccf3238845781c1d7ca137da3b55d4a54db4a4cf84c840690c77`
and confirmed boot. The displayed build is `0.2.0-perf.20 / 8ac1594+`.
The Normal Pillar of Autumn save loaded at camera `(-28.66, 32.52, 0.62)`,
forward `(0.56, 0.82, -0.15)`. Twelve settled 60-frame windows averaged
**78.25 ms / 12.78 FPS / 151 draws**, essentially unchanged from perf.19's
78.20 ms / 12.8 FPS at the same camera. Live actor/draw variation prevents
treating the tiny difference as a measured regression or gain.

Across 22 reported windows (including transition), 15,294 calls produced
5,966 coarse repeats, seven capture attempts, four completed records, three
abandoned records and only **three replay hits**. There were 9,282 evictions.
The captured log contained no searched fatal/stop/GPU-fault/data-abort markers;
this short observation does not establish long-session stability.

This implementation has not earned an optimization claim. The next step is to
separate lightweight repeat history from expensive completed records and inspect
capture-abandon reasons. The current ring lets ordinary misses displace both
history and successful records. Preserve all replay dependency checks while
changing retention; do not merely lower capture thresholds and pay more cold
recording cost. Keep the option default-off until ordinary hardware play shows
sufficient surviving hits and a frame-time improvement.

Private captures and summary are under
`../ce-perf20/reconnect-20260919-084213/`.

`[query-reuse]` reports calls, repeats, promotion, completed/abandoned captures,
CPU/memory rejections, hits, saved budget units, evictions and fallbacks. Budget
units are execution counters, not milliseconds. Compare ordinary campaign runs
at the same checkpoint/settings and inspect surviving hits before retaining
this option in a tester build.
