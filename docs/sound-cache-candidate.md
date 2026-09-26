# Sound-obstruction cache candidate

September 26, 2026. Verification-only runtime integration; no fast-path deployment.
See [the firing census](impact-phase-diagnostic.md) for the supporting evidence.

The existing direct-mapped cache loses many recent queries during firing. A Pi
shadow census estimated roughly 670–790 additional retained queries per
60-frame firing window using four-way retention. That census did not substitute
answers and its shared counters were not a serialized replay. Sound queries
also appear on the scene helper, so extending the original unsynchronized table
without addressing view ownership would be unsafe.

## Implemented boundary

`xk_sound_cache.h` provides bounded 1/2/4-way retention, preserving endpoint
anchors and the existing age/distance approximation, with memory-view and
world-epoch keys. `xk_sound_cache_access.h` serializes access with a single
nonblocking atomic attempt. Contention declines reuse; it never spins or holds
a guard across guest code. A hit is copied before unlocking. A miss returns a
ticket containing the original inputs and guarded generation. Deferred
invalidation clears entries on the next admitted lookup and rejects old
in-flight tickets, including after a newer lookup has consumed the reset.

World-transition code must request invalidation before publishing a new world.
That hook is **not wired yet**. The access layer offers the mechanism; it does
not establish that runtime world transitions are covered. Memory-view keys
separate page-table identities, but do not independently identify map changes.

`XV_SOUND_CACHE_VERIFY_WAYS=1/2/4`, with `XV_SOUND_OBSTRUCTION` enabled, selects
the candidate observer in `xk_sound_obstruction.c`. It always calls the original
collision routine and retains its full guest result/state. Candidate hits are
compared with that result, never returned in its place. The real cast is outside
the cache guard. Different page tables use different domains. Nonfinite or
out-of-range hash inputs bypass the candidate. The mode reports atomic
queries/hits/same/different/unavailable/other-view counters; reporting boundaries
are asynchronous and individual counters need not describe identical intervals.
On a candidate hit, its anchor is not refreshed. The legacy cache remains the
default; no candidate fast mode exists.

First-use configuration is now published with acquire/release atomics. A
simultaneous caller casts normally while initialization is in progress, rather
than reading partial configuration or blocking. This does not make the legacy
cache table itself thread-safe; only the candidate uses the new access layer.

## Validation

- Policy tests passed host ASan/UBSan and Cortex-A9-targeted Pi ARM execution.
- Access tests passed host ASan/UBSan, ThreadSanitizer, and Pi ARM on cores 0/1.
  They exercise forced contention, input snapshot ownership, invalidation during
  a simulated cast, rejection of old tickets after reset, and 80,000 concurrent
  lookup/commit attempts in two views with 10,000 invalidation requests.
- The runtime unit compiled with the Vita SDK. The ARM game harness linked.
- The 180-second real-query comparison completed its planned timeout (124),
  with 66 host reports. See the failed semantic acceptance gate below.

Private receipts and build inputs live in `sound-cache-access-candidate/`:
`tests.log`, `commands.json`, `tsan.log`, `tsan-result.json`, `vita-compile.log`,
`run.log`. Generated game code and assets are not committed.

Before enabling reuse: inspect actual-query mismatch data, wire and test world
invalidation, verify map transitions and concurrent ownership, then run normal
hardware gameplay with retained rollback. These tests neither establish a Vita
FPS gain nor prove long-session audio/gameplay correctness. Perf258 remains the
last confirmed installed Vita build; perf259 is built but not deployed, and
Xita's HTTP endpoint is still unavailable. The 20 FPS objective remains open.

## Actual-query result: acceptance gate failed

Run 5193 is terminal. Of 205,532 reported queries, 174,143 were candidate hits:
174,125 agreed with real casts and **18 differed**. There were 25,087 queries
through a non-root memory view, and zero reported unavailable lookups. Counters
are asynchronous; these totals are not a lossless per-call trace. The summary
script deliberately failed its zero-mismatch assertion. Do not call this a
passed correctness run or enable candidate reuse on its basis.

Differences appeared near host reports 540 (2), 600 (8), 1860 (3) and 2100 (5).
The latter two are around firing. Clean later windows did not establish a clean
whole run. Verification always retained actual guest results, so no incorrect
cached answer was returned to the game. We have not established whether these
differences stem from the inherited endpoint/age approximation, selection among
multiple retained anchors, or another issue; do not assign a cause yet.

Private receipts: `verify-summary.json`, `verify-firing-windows.json`, and the
full `codex-sound-cache-verify-20260926.log` in `d3d-record2-work/pi-runs/`.
A bounded diagnostic now copies the matched entry under the guard and records
up to 32 mismatches with query frame, anchor age, memory view, predicted/actual
AL, listener/sound distances and endpoints. There is no pointer into the cache
after unlocking. The access tests were rerun with this capture and passed host
ASan/UBSan, ThreadSanitizer and Pi ARM (`tests-with-capture.log`). The diagnostic
has not yet been built into another whole-game run; the previous runtime unit
build receipt predates this logging addition.

Next action: rebuild the verification-only ARM unit with the bounded mismatch
capture, rerun the two-burst sequence, inspect anchor age/distance and memory-view
patterns, and then correct or constrain reuse. World invalidation integration
and hardware verification remain required. No Pi job is currently active.
