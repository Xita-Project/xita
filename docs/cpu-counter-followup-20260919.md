# CPU cache-counter follow-up

Elapsed vertex-capture samples cannot distinguish cache misses, instruction cost
and preemption. The installed VitaSDK's `psp2/perf.h` and public
[VitaSDK reference](https://docs.vitasdk.org/perf_8h.html) expose thread-ID-based
PMON reset/select/start/stop/read APIs, with events for data-cache accesses/misses,
data-cache stalls and cycles. `libScePerf_stub.a` exists in the local SDK. No
current runtime/tool implementation uses these functions.

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

The current hardware sequence stays unchanged. Complete perf.35 and the planned
same-camera native/360p/native comparison first; those results determine whether
CPU cache work is the next priority. No PMU code is installed or exercised yet.

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
