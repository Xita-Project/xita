# GPU packet submission and observation timing

`make RECOMP=1 XV_GPU_PACKET_TIMING=1` enables a pump-owned diagnostic around
packet submission and final fragment notification reads. Ordinary builds omit
its state, extra clock calls, extra notification reads and reports. The build
marker rebuilds `main.o` when this flag changes in either direction. There is
no saved-setting change, runtime toggle, new thread or per-draw timer.

## Why this measurement

The second physical async-report comparison has about 99.5 ms median frame
intervals. Across 29 complete retirement windows in each OFF/ON/OFF arm, existing
packet completion latency averages 60.936 / 61.042 / 61.127 ms. Pump submission
averages about 4.2 ms. Maximum pending is one and recorded slot waits are zero.
These measurements do not establish that the GPU executes for 61 ms or that
61 ms is its minimum sustained frame interval.

The existing timer starts before pump submission and ends when the pump notices
the final notification. Pending work is polled independently of guest Present,
with a requested 100 us sleep, but actual scheduling gaps were unmeasured.
The diagnostic preserves that loop, queue policy, fences and ownership release.

## Captured boundaries

Each packet retains the following on the pump only:

- `begin`: the existing submission-region start, after display-slot/frame-cap
  admission and before CPU accounting, upload joins and GXM submission.
- `end`: immediately after `xv_gfx_render_frame` returns, before exceptional
  failure cleanup. This CPU elapsed interval includes API stalls, logging and
  preemption; it is not CPU active time. It includes the display queue call.
- The clock immediately **before** the last unsuccessful notification read.
- The clock immediately **after** the first successful notification read.
- Poll count through first success, and largest observation gap. The first
  gap starts at `begin`; subsequent gaps run from the preceding poll's leading
  clock to the current poll's trailing clock. This conservative gap includes
  polling overhead and submission periods in which no read was made.

The final notification is passed to `sceGxmEndScene` as its fragment
notification. It follows the final upscale when scaling is active. Intermediate
world visibility notifications and display callbacks do not complete this
diagnostic record or release packet storage.

For a successful packet with a previously unsuccessful read, completion lies
between that read's **leading** clock and the successful read's trailing clock.
Using the failed read's trailing clock would be incorrect: the GPU could finish
between the load and that clock. If the first read succeeds, the lower bound is
`begin`, not `end`; completion may occur while CPU submission is still running.
The bound concerns notification visibility to the CPU, including driver/queue
dependencies. It is not a pair of GPU execution timestamps.

Only the oldest packet is retired. Younger submitted packets also receive
bounded observations on each pump iteration, without changing their readiness
or releasing them. Their first-ready time is retained through later ordered
retirement. A missing observation remains a wide bound, never an invented
completion timestamp. Timing data is folded before publishing frame completion;
neither the recording thread nor the display/GPU callback accesses it.

## Reading the report

`[gpu-packet]` accompanies the existing report after every 60 retirements.
It identifies the first/last ticket and reports retired, valid, failed, invalid
and `no-negative` counts. All sums/maxima use **valid successful packets only**.
Divide `sum-us` fields by `valid` for per-packet means:

| Field | Interpretation |
| --- | --- |
| `submit` | CPU submission-region elapsed time |
| `completion-lo/hi` | Bounds from `begin` to observed completion |
| `after-submit-lo/hi` | Each bound minus CPU submit duration, clamped at zero |
| `bracket` | Upper minus lower bound; uncertainty in completion time |
| `polls total/max` | Reads counted through first success |
| `max-us observation-gap` | Worst conservative gap, including initial submission |

`no-negative` is a valid sample whose first read already succeeded; its broad
bound must not be interpreted as completion at CPU return. Failed submissions
are excluded even if cleanup's `sceGxmFinish` makes their notification visible.
Stale matching words before submission, mismatched diagnostic ticket generations,
missing first-success observations, reversed clocks and accumulator overflow
are invalid samples. Reports expose their counts instead of silently inventing
zero durations. Unsigned 64-bit differences accept clock wrap for spans shorter
than 2^63 us; packet iteration also preserves uint32 ticket wrap.

The diagnostic can distinguish wide observation uncertainty from a notification
that remains incomplete well after CPU submission. It cannot separate active
GPU execution from internal dependencies, memory contention, scheduling or
driver queues. Even a tight 61 ms latency bound does not establish 61 ms GPU
throughput: cross-frame pipeline overlap at a faster feed remains untested.
Completion latency overlaps CPU work and must not be added to guest/pump time.

Extra clock reads and a periodic report perturb scheduling. Use this build to
attribute latency; return to an ordinary build for final performance comparisons.
The current view's 13.4 ms total draw-HLE budget alone cannot explain the roughly
49.5 ms reduction needed for 20 FPS. No GPU or FPS improvement is claimed here.

## Validation

`python3 tools/test_gpu_packet_timing.py` runs production polling/retirement
fixtures normally, under ASan/UBSan and under ThreadSanitizer. A separate thread
publishes 500 notifications during the failed read's trailing clock, including
ticket wrap. Tests also cover timestamp wrap/zero, out-of-order visibility,
retained first success, missing observations, stale generations, failures,
report/reset and invalid bounds. The production pump fixture runs with the flag
ON/OFF across display release, pacing, failure cleanup, missing intermediate
world notifications and ticket wrap. Native ARM compilation is checked in both
modes. No emulator or physical-device result is implied by these host checks.
