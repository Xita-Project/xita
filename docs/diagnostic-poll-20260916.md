# Separating diagnostic filesystem polls from gameplay stalls

The current physical view is CPU-heavy, but its 60-frame counters cannot
attribute individual drops. Six complete windows in the initial logger trial
contain 1,050 object passes over approximately 34.986 seconds: about 30 passes
per second, despite about 10.3 rendered frames per second. Joined object batches
total 12.851 seconds, or 35.70 ms/frame and 12.24 ms/pass. Worker elapsed lock
waits overlap those batches. Draw HLE is about 12.8 ms/frame; notification
retirement near 62 ms overlaps guest/submission, with no recorded slot or exact
visibility waits in these windows. These scopes are not additive. Effective
clocks are CPU 444, bus/GPU 222, xbar 166 MHz; requested CPU 500 is not effective.

Real-time vblank catch-up can increase object passes after a slow frame. The
counts support that workload relationship, not a proven per-frame oscillation
or permission to skip simulation ticks. Existing detailed guest-phase capture
disables object-job admission and would change this workload. A later broader
timeline should snapshot the existing joined object/pass/lock counters and draw
timers at a common Present boundary, retain report-reset epochs, and join pump
submission/retirement timestamps by ticket. It should not time every translated
call or add overlapping CPU/GPU scopes.

The first corrected-priority logger trial has 30 report scopes per measured arm.
Median OFF/ON/OFF producer costs are 11.1935/1.002/11.1195 ms, maximum costs
30.122/1.102/18.227 ms, and totals 371.517/30.249/353.987 ms. The last captured
enabled worker row has no error or backpressure. The parent's physical trial
also shows improved frame tails. This supports a report-related hitch in that
view, not stable 20 FPS or proof that every remaining stall comes from logging.
The measured UI scope excludes the subsequent flare/camera/director report in
`xd3d.c`; OFF writes that group as separate synchronous calls and ON groups it.
Therefore the roughly 11 ms UI scope is not the entire periodic stall or an
explanation of the full observed tail reduction.
The following two polls remain synchronous and separate from the report queue.

## What the two polls do

- `xk_os_pad_poll()` invokes the screenshot watcher before each controller peek.
  Its only production caller is guest `XInputGetState`, including virtual-player
  and invalid-handle queries. The 180-call threshold counts guest calls, not
  distinct physical controller samples or Presents. At three calls/frame it
  scans every 60 frames; the actual ratio was previously unmeasured. The current
  physical configuration has `XV_SHOT_DUMP=1` although the missing-env default
  is off. Each scan opens the screenshot root and all immediate child directories
  and counts files, then optionally arms a diagnostic trace. It does not create
  a screenshot. Directory size can increase its work.
- `hist_level_track()` checks `hist.now` every 16 Presents regardless of any
  environment trace setting. A miss still performs a synchronous path lookup.
  A successfully consumed request arms a trace. This is unconditional debug
  polling in ordinary play, not necessary game asset I/O.

Both run with the serialized guest baton held. Native filesystem calls can block
guest progress while the render pump and native workers continue. Neither has a
measured causal cost before this candidate. Remote `/screen` independently copies
a completed display buffer and streams PPM; it never scans the screenshot tree
or arms these traces. Its existing benchmark exclusion stays unchanged. Native
screenshot controls and all controller peeks/mapping remain in place.

## Isolated same-session comparisons

Benchmark family **38** exposes two request names:

```
python3 tools/vita_remote.py --config PRIVATE_CLIENT benchmark OUTPUT --kind diagnostic-shot --runs 3
python3 tools/vita_remote.py --config PRIVATE_CLIENT benchmark OUTPUT --kind diagnostic-hist --runs 3
```

Each runs baseline / selected poll suppressed / baseline at the same graphics,
logger and worker policy. Each arm settles for 60 frames and measures 1,800 FPS
intervals; pacing statistics retain 1,799 intervals after excluding the marker
interval, as in the logger experiment. `on` in the legacy result line means
suppression enabled. The client receipt uses `baseline_before_fps`,
`suppressed_fps`, and `baseline_after_fps` to make that meaning explicit. The
first interval remains explicitly labelled in event attribution.

The network publishes the kind and selector in one atomic request word: 38 for
shot, `38 | 256` for hist. Only the serialized guest consumes it and changes the
gate after the existing pump drain. IDs 36/37 are reserved for independent
clip/census work. An environment-disabled screenshot watcher rejects the shot
comparison; baseline never forces it on. The other poll remains enabled according
to its original configuration. Cancellation, lost gameplay control and normal
completion all restore the gate.

Screenshot polling ticks continue advancing while suppressed. Resumption
invalidates the old count; the next complete successful scan establishes a new
baseline without arming a stale trace for photos taken during suppression. Later
new photos again arm normally. Partial scans, failed child opens/reads/closes,
and truncated paths never replace a valid count. `hist.now` stays untouched
during suppression and is consumed once afterward. Successful close and removal
are required before arming; failures retain a retryable request and do not
repeatedly trace it. These error-handling corrections also apply outside tests.
Other environment/manual histogram triggers are unchanged. Do not deliberately
trigger unrelated detailed traces during a performance comparison.

## Bounded attribution and validation

Only actual scans/probes take two extra clock readings, only during measured
arms. Counters include caller polls, due opportunities, skipped opportunities,
actual runs, elapsed/maximum time and frame, visited directory entries, errors
and trace triggers for both paths. A fixed 512-entry array records the measured
interval and both poll durations for intervals containing I/O. No allocation,
new worker, filesystem write or log line occurs per poll. All attribution is
printed after each measured arm and before the next settling interval. The
array is bounded (about 20 KiB of fixed storage); overflow invalidates the trial. Trace triggers during settling
also invalidate later results. Errors/triggers, missing counters, empty selected
exposure or malformed suppression counts prevent a valid client receipt.

Elapsed poll time includes scheduling and excludes the trace output it may arm;
it is not isolated SD latency. Match event intervals to pacing before inferring
causation. Keep logger mode fixed for these trials; start with the shot comparison
because its directory work scales with saved screenshots. A benefit still needs
repeated physical baseline/suppressed/baseline results. No default suppression
or physical speedup is claimed by this source candidate.

Validation uses the production poll implementation and benchmark state machine:
normal and ASan/UBSan tests cover both selectors, default-off admission, exact
180-call/16-Present gates, independent triggers, failed/partial directory scans,
checked file consumption, stale-count rebasing, timing disabled outside capture,
1,800-frame arms, cancellation/view loss, restoration and invalid attribution.
The actual input fixture retains front/rear touch and physical/remote controller
behavior. HTTP tests verify both encoded selectors, capture exclusion and client
rejection of missing/error counters. Frame-dispatch tests verify the diagnostic
branch drains without modifying other optimizations. Existing logger benchmark
tests still pass. Six deliberate regressions are rejected: incorrect cadence,
stale screenshot count, partial-scan baseline replacement, cross-path suppression,
duplicate file consumption, and losing a settling-period trace marker. The six
modified/new runtime/kernel translation units compile
with the installed VitaSDK. These checks do not establish firmware I/O cost,
screenshot behavior on hardware, or frame-rate improvement. No emulator or
hardware was accessed for this candidate.

Private evidence: `engine-restructure-20260914T2300Z/physical-async-reports/initial-trial-tail.log`
and `physical-async-priority/log-compare/trial-1.log`. No game assets or generated
guest code are included.
