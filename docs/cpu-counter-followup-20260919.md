# CPU cache-counter follow-up

Elapsed vertex-capture samples cannot distinguish cache misses, instruction cost
and preemption. The installed VitaSDK's `psp2/perf.h` and public
[VitaSDK reference](https://docs.vitasdk.org/perf_8h.html) expose thread-ID-based
PMON reset/select/start/stop/read APIs, with events for data-cache accesses/misses,
data-cache stalls and cycles. `libScePerf_stub.a` exists in the local SDK. Before this opt-in probe, no runtime/tool implementation used these functions.

This establishes an API surface, not hardware availability or validated counter
semantics on the user's firmware. Do not claim measured cache pressure yet.
Before adding frame instrumentation:

1. Run an opt-in probe on a dedicated thread using only public user-mode APIs.
   Check every result and report unsupported/permission errors without treating
   zero counters as successful measurements. Do not reset another thread's PMU.
2. Validate a selected software-increment counter against a known increment
   count, then check cycle progress and counter reset/read behavior. Stop the
   probe's counters on every exit after a successful start.
3. Check data-cache event response to bounded known memory workloads. Calibrate
   measurement overhead and counter wrap; document what is counted across
   scheduling before attributing events to a gameplay scope.
4. Only then sample the recording thread/worker separately, with counters
   reserved for that diagnostic. Retain elapsed timing and matched gameplay
   captures; events alone are not FPS, GPU utilization or exclusive stall time.

The perf.35 native/360p/native comparison is complete. GPU completion latency
fell at 360p, but total frame time did not improve in the crowded campaign view.
This supports investigating resolution-independent work; it does not establish
cache pressure. See `packed-compare-groups-20260919.md`.

## Opt-in response probe implemented

`XV_CPU_PMON_PROBE=1` adds a dedicated core-2 startup thread before the dashboard
and guest execution. It touches only its own PMU context. The ordinary build
omits its object/imports; a validated 0/1 build stamp rebuilds the startup hook
when changed. The launcher and graphics assets are unchanged.

The probe checks reset, event selection, zeroing, reading, start and stop results.
It requires exactly 32 software increments, then a positive cycle-event response
to a bounded integer loop. A failed call or unexpected value prevents success.
A failed stop receives one cleanup attempt and is reported. The startup join
has a one-second timeout; a potentially live thread is never deleted on timeout.
No guest pointer or caller-owned stack data is passed to that thread. It makes
no gameplay instrumentation calls and does not claim calibrated cycle/cache
measurements or scheduling attribution.

The production core passes ASan/UBSan tests covering all 46 individual API-call
failure positions, nonzero reset response, wrong software count, zero cycle
response and repeated stop failure. The fixture checks that no such case reports
qualification. Vita SDK compilation passes with `-Wall -Wextra -Werror`.
These mocked failures test control flow, not physical PMU availability. Device
qualification remains pending. Fixture: `tools/tests/pmon_probe.c`.

The same fixture also passes startup lifecycle cases: requested dedicated-thread
priority/affinity/stack, bounded join, creation failure, start failure cleanup,
API failure followed by joined deletion, and no deletion after a failed wait.
These tests mock thread services and do not prove firmware scheduling behavior.

## Perf.36 deployment did not confirm boot

The opt-in diagnostic package (source `eaf3173`) built and passed package checks,
but the updater terminated with `No confirmed boot within the timeout`.
Subsequent status requests returned `ConnectionRefusedError`. This is not a
successful installation receipt and provides no PMU response measurements.
The last confirmed runtime remains perf.35 (`fbcb29d`); the currently running
process and on-device slot metadata have not been recovered yet.

The probe is called after graphics initialization but before `xv_remote_start`
and dashboard confirmation. A loader/import failure or a failure within that
startup path can therefore prevent remote access; no cause has been established.
Do not redeploy this diagnostic until device logs are recovered. Moving the call
later would not address a loader failure and is not an evidence-based fix yet.

Recovery inspection confirms that `xv_update_boot` marks a pending candidate
ATTEMPTED before selecting it. A later launch skips an unconfirmed ATTEMPTED
candidate and selects the verified CONFIRMED slot. Both `tools/test_update.py`
and its `MOUNT_TEST=1` variant pass against the current production updater,
including failed-boot fallback. These synthetic storage tests establish control
flow, not the actual device's storage state. Close/reopen the Xita bubble once,
then inspect status and startup/launcher logs before any further deployment.

Preserved local artifact: `../cpu-counter-hardware/build/xita.vpk`.
Runtime SHA-256:
`6b20f65d5db31b53e00a9e825b21c8b697c17c7bf8f5bf33ea2ac9acc117d2e9`.
The package changed only `game-a.self` and `boot-game.txt`; launcher and assets
were unchanged. Deployment output is in `../cpu-counter-hardware/deploy.log`.

## Recovery confirmed on hardware

After the user reopened Xita, `/status` returned perf.35 / `fbcb29d` and
`/update` confirmed slot 0 with runtime SHA
`0e147decd401ac688ce8300a55bd6bfff9e94175a3327a8c0497d69357dbe73f`.
The current log records successful dashboard confirmation. The launcher log's
last selections are slot 1 then slot 0, consistent with failed-candidate fallback.

The preserved previous log identifies perf.36 / `eaf3173`, reaches graphics
initialization completion (1956 ms), then ends before any probe result, remote
startup or dashboard confirmation. This rules out failure to enter the new
executable, and narrows the observed stop to the interval between graphics
completion and remote startup, containing the probe. It does not identify the
exact failing call; logging loss and faults inside imported calls remain possible.
Do not characterize this as a gameplay crash or claim PMU qualification.

Receipts: `../cpu-counter-hardware/recovery/1789856245097402289/`
(`previous-1.log`, `launcher.log`, `update.json`), plus the current startup capture
in the parent recovery directory. Perf.36 remains excluded from further deployment;
perf.35 is restored without reinstalling the VPK.
