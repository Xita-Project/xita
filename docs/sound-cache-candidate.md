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

## Captured mismatch follow-up

The updated diagnostic unit compiled for Vita and ARM, and the rebuilt ARM
harness completed `codex-sound-cache-mismatch-20260926` (180-second planned
timeout, exit 124; 68 host reports). The 60 cache-counter reports contain
210,406 queries, 178,332 proposed hits, and 14 disagreements. All 14 were captured;
none was returned instead of the real collision result.

Ten disagreements occurred during listener movement at frames 639–650, with
anchors one to three frames old. Four occurred around firing at frames
1882–1883 and 2113–2114, with a stationary listener and moving sound endpoint.
Every captured discrepancy involved endpoint movement. This implicates the
inherited movement tolerance, but does not rule out simultaneous world changes.
A one-frame-old answer can already disagree, so merely reducing six-frame
retention to two frames is insufficient for these examples.

Private evidence: `sound-cache-access-candidate/mismatch-capture-summary.json`,
`vita-capture-compile.log`, and the full run log in `d3d-record2-work/pi-runs/`.
The next verification-only run, `codex-sound-cache-exact-20260926`, uses zero
listener and sound tolerances with the same two-burst input sequence. It is
running on Pi cores 0/1; local exec session 68175 is its observation handle.
Inspect that handle and the complete log before concluding anything. Zero
movement tolerance still does not prove the world stayed unchanged; invalidation
and hardware qualification remain required. No new Vita deployment or FPS gain
is claimed.

## Zero-tolerance result and exact-key hardening

Run `codex-sound-cache-exact-20260926` is terminal: planned timeout 124,
66 host reports, 58 cache reports. It used zero listener/sound tolerances,
six-frame retention, and four ways. The complete counters report 204,574
queries, 168,451 proposed hits (82.34%), 168,451 agreements and zero differences;
24,302 queries used a non-root view. Every query still performed its real cast.
Private receipt: `sound-cache-access-candidate/exact-summary.json`.
This is a single headless ARM comparison, not hardware performance proof or
world-transition coverage. No further Pi job is running.

The zero-tolerance policy now compares coordinate bits rather than squared
floating-point distance. This prevents underflow from treating distinct tiny
coordinates as identical; differing signed zeros conservatively miss. Tests
cover both listener and sound underflow and signed zero. Policy tests passed
host ASan/UBSan and Cortex-A9-targeted Pi ARM; access/concurrency tests passed
host ASan/UBSan and Pi ARM after the change. Receipt: `exact-key-tests.log`.
The whole-game run above predates this additional policy hardening.

Remaining key audit: the current runtime reconstructs the sound endpoint as
`start + vector`. Identical reconstructed endpoints do not necessarily imply
identical original vectors because float addition rounds. Exact reuse must key
the original ray inputs as well, not rely on the reconstructed endpoint alone.
Furthermore, epoch is still zero: unchanged positions do not prove unchanged
collision objects/BSP. The scene helper exposes a context generation, but that
is not yet established as a collision-world revision and must not be used as
one without checking mutation boundaries. Actual cached reuse remains disabled.
Next: preserve original ray inputs, audit world/view invalidation, rerun real
queries, then qualify on hardware. Perf258 remains last confirmed installed.
