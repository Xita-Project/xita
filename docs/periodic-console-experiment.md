# Periodic console-output experiment

`XV_LOG_PERIODIC_CONSOLE=0` omits duplicate console output only for the native
asynchronous periodic report queue. Default and invalid values retain both
sinks. The choice is read before the worker starts and requires a fresh process;
remote environment changes do not alter an active worker.

All periodic bytes still go to the append-only file. File retry offsets, queue
ownership, completed-report counters, flush barriers and shutdown are unchanged.
Immediate ordinary messages, critical messages and writer failure notifications
still reach the console. Synchronous fallback behavior is unchanged.

Motivation: perf250 pod logs put 14 of 17 intervals over 100 ms at the same
position in a 60-frame cycle. That phase averages 104.317 ms, versus 55.820 ms
for other phases. This is correlation, not proof of console causality. The
observed excess is about 0.81 ms per frame when amortized; eliminating it alone
would not meet the 50 ms target. Report producer time averages only 1.444 ms
per report and excludes asynchronous console/file work.

Validation: the production real-thread mock suite passes ASan/UBSan, including
file-only progress while console output is blocked, exact file retry after a
partial failure, immediate errors/ordinary console output, and invalid-policy
fallback. Pi Cortex-A9-targeted Thumb/NEON fixtures pass those new cases plus
critical, partial, console-error, owner-critical and barrier cases. The exit
fixture was updated for the current aggregate update-request helper.

Perf251 is built from perf250, changing only the logging include and version.
Hardware results are pending. Keep the setting experimental. The worker starts
in the dashboard before remote launch overrides, so a `/env` acknowledgment alone
does not activate this startup option. The first perf251 attempt still had
increasing console counters and is not a valid experiment. A temporary
`xita.cfg` override was then backed up, applied and read back before cold launch.
Nineteen live worker reports confirmed zero console time and the startup log
confirmed the override. The exact original configuration was then restored and
read back while the worker retained its startup policy. The private receipt and
guarded restoration script live in the candidate directory. Check cumulative console time stays zero while file
bytes progress, then compare complete raw frame distributions and phase cadence.
Do not remove periodic outliers from the acceptance results.


## Qualified perf251 pod result

After startup activation was verified, 1,200 intervals ending at report frames
6300..7440 averaged **56.083 ms / 17.83 FPS**, p95 72.666 ms, p99 108.853 ms,
maximum 183.038 ms. Eighteen exceeded 100 ms; none exceeded 200 ms. The prior
perf250 sample was 56.629 ms / 17.66 FPS. This small difference does not establish
a meaningful speedup.

The recurring phase-3 interval still averages **101.993 ms** (11/20 over 100 ms),
versus 104.317 ms (14/20 over 100 ms) before. Disabling periodic console output
does not remove the cadence or explain the whole stall. Leave the option
experimental; do not make it a default optimization on this evidence.

Reproduce the cadence analysis without excluding any slow frames:

```sh
python3 tools/frame_times.py capture.log --from-frame 6300 --to-frame 7440 --cadence-period 60
```

The capture must first be qualified for build, settings and scene. The pod photo
was at frame 6369, inside this window; screenshot I/O can affect isolated samples.
The opening photo at frame 5057 was a black transition, not evidence of canyon
performance. Outdoor qualification is still pending.
