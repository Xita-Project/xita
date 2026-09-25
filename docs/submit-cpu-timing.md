# Submission CPU-time diagnostic

`XV_SUBMIT_CPU=1` samples the pump thread's kernel `runClocks` around
`xv_gfx_render_frame`. It is disabled by default. Disabled calls return before
reading clocks or querying the kernel. No thread, draw, wait or fence policy changes.

Every 60 submissions, `[submit-cpu]` reports valid samples, submission failures,
and summed elapsed time, CPU time and their difference. Divide the sums by the
**valid sample count**, not necessarily 60. Empty thread information, decreasing
clocks and CPU deltas exceeding the enclosing wall interval are rejected.
The CPU interval includes sampling overhead. The remainder combines preemption,
driver waits and sampling boundaries; it is not GPU service time. Failed
submissions are counted separately but included in valid totals when their clocks
are valid, so discard windows with failures when interpreting normal gameplay.

Interpretation:

- CPU time near elapsed time supports optimizing command setup/submission work.
- A large remainder means elapsed submission time cannot be attributed entirely
  to CPU execution. Thread-state or fence evidence must distinguish preemption
  from driver waits before changing scheduling.
- Existing packet notification bounds still measure queued completion latency.
  Do not add them to CPU frame phases as independent costs.

The private fixture checks disabled-call behavior, known elapsed/CPU totals,
invalid samples and report-window reset. The Vita build must additionally be
qualified against real kernel counters. Diagnostic timings are not a clean
performance baseline.
