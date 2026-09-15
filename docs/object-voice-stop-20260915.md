# Object sound cleanup: voice-stop handoff

A physical Blood Gulch run on September 15 ended with an explicit worker STOP
at `0019C5FF`, returning to `00028745`, on lane 1. This is `DSoundVoiceStop`,
called by object sound cleanup `28710`; the indirect chain is `C3A00 -> 28710`.
The worker stack canary is intact. The terminal log establishes an unsupported
owner-service call in this run, not a GPU-driver failure.

The confirmed installed runtime was `b64e2b984f0d328d5a0ec7f44a94a442f54aba7022e3e43e955b090c30ac22e9`.
All four available game logs and the launcher log were preserved before updating.
The user's crash screenshot has not yet been retrieved. Private evidence is
`physical-crash-20260915T233409Z/game-1.log` under the engine-restructure validation
directory. The log is 2,835,027 bytes and includes the terminal registers and stack.

## Fix

Only this audited voice-stop caller is admitted to the existing owner-service
handoff. Every object lane parks before the original handler runs on the guest
owner. The handler resolves direct or wrapped sound objects, updates the existing
voice reporting state and stream reporting deadlines, and returns with the
original one-argument stack cleanup. It dispatches no callbacks. Its existing
audio behavior is unchanged; the worker bridge does not substitute a success
stub or execute sound code concurrently on workers.

Unknown call sites, missing handlers and unsupported nested audio services still
stop. A separate `quiescent owner voice stops` counter reports completed services
and resets after the joined frame window. The zero-worker diagnostic path also
executes the original handler. Both experimental workers and the earlier
lightweight-lock and logging improvements remain enabled.

## Validation

The production worker suite passes normally, with ASan/UBSan and ThreadSanitizer.
Each configuration executes 600 stops with two, one or zero workers, profiling
on/off and bounded waits on/off. Calls run inside and outside shared transactions.
The test compiles the real lookup and stop bodies and checks the servicing thread,
parked-worker publication, direct buffers/streams, wrapped objects, null/unknown
objects, preserved packet completion data, no callback execution, registers,
arguments, stack cleanup and counter reset. Unrelated callers and null handlers
are rejected before invocation.

Native compilation passes without warnings. Package verification confirms all
1,588 members and the existing updater contract; only the runtime and its digest
file change. Runtime SHA-256:
`3753961f842872504f3922d459d6a5d2b27f56708f8b68e9df5bb3e88895c051`.
Vita3K reaches Blood Gulch through the normal menus and completes a charged
plasma shot (100 to 89 battery) and a grenade throw. Its initial captured log
contains no worker STOP, but reports zero voice-stop handoffs: this smoke test
has not reproduced the exact cleanup path. The physical updater confirms the
new runtime running in slot B; the preserved slot A remains unchanged. Blood
Gulch is running on hardware and its first captured log has no worker STOP.
Neither smoke test reaches the new voice-stop handoff yet, so a longer gameplay
retest remains necessary. No performance gain or general crash resolution is
claimed for this handoff.

## Periodic logging observation

The recovered run contains 273 batched report samples: median 10.723 ms,
maximum 151.500 ms, with two above 100 ms. The previous reduction in typical
report cost persists through this longer session, but occasional stalls remain.
These timings measure report wall time, not whole-frame FPS or isolated disk
latency. The pending replacement-blend experiment is excluded from this build.
