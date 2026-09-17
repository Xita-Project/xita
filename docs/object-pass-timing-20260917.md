# Passive accepted-object-pass accounting

`XV_OBJECT_PASS_TIMING=1` adds one elapsed interval around each accepted object
pass and two raw worker run-clock samples at the existing quiescent frame report.
The repository default is `0`; there is no environment setting, live selector,
benchmark, new guest hook or changed work policy. This is diagnostic evidence,
not an optimization or an FPS claim.

Build with the existing retained flags plus:

```
RECOMP=1 XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OWNER_PHASE=1 \
XV_OWNER_PHASE_DEFAULT=1 XV_OBJECT_PASS_TIMING=1
```

The first three prerequisites and Boolean timing flag are checked by Make. The
owner observer must provide `int xv_owner_phase_active(void *, unsigned,
uint32_t *)`: a zero token binds only after native-thread/live-context admission;
a nonzero token must match the current owner generation. Return values are -1
unknown, 0 inactive, 1 active. This change weakly consumes that API and uses
`XV_OWNER_TICK`, without modifying observer files. An older/missing API or an
explicitly disabled owner observer leaves accepted passes **untimed**, counted as
`unknown`; it does not disable jobs. Startup logs identify the compiled observer.
The observer's ordinary pre-Present warmup remains untimed.

Only `kernel/xk_object_jobs.o` receives the new define and depends on
`build/recomp/object-pass-timing.config`. A reused build directory switching
0→1→0 rebuilds that object and its game archive, with no generated guest rebuild.
No holder, motion, general XV_PHASE, solver or query setting is enabled.

## Interval and reporting contract

The timer begins after successful `xv_object_jobs_begin` admission, at the
existing 900E0 pass boundary. Finish measures after its existing final join,
before clearing `owner`; the guest hook is before the 90314 tail to 8ECA0.
The accepted pass interval excludes that tail. It includes owner scanning,
callbacks, queued batches, any cooperative yields and scheduling delays.
It is **elapsed**, not CPU self time.

The unmodified `batch_us` value is captured at each admitted begin and finish.
The matching difference includes capacity flushes and explicit/yield-driven
joins in that pass. No batch timer, queue, worker budget, lock, service or yield
was moved. Ancestry must be admitted with the same generation and FA920 state at
both boundaries. Generation/live-context/state loss or backwards/inconsistent
clock/batch values discards the span and increments `interrupted` and `invalid`.
A later valid begin abandons any stale open diagnostic interval. Shutdown joins
as before, records an open span as interrupted, and invalidates clock baselines;
it never fabricates a normal finish. After the terminal unreported-window row,
the entire diagnostic window and snapshot records are cleared, so a future
lifecycle cannot report them again. Process-aborting STOP remains a STOP, with
no guaranteed final diagnostic report. Existing shutdown forbids reinitialization
(`initialized == -1`); this observer does not introduce a restart path.

`[object-pass]` reports the same frame window as `[object-jobs]`:

- `begun`: timed accepted starts, excluding `unknown` admissions.
- `completed`, `interrupted`, `open`: disposition of those starts. Normal reports
  are already quiescent, so `open` is zero. A report attempted while a pass is
  open still returns through the original gate without resetting counters.
- `unknown`: accepted starts lacking owner-observer admission; these have no
  elapsed estimate. `invalid` is the invalid-finish subset of interruptions.
- `clocks`: actual pass-timer reads, excluding existing batch/work clocks and
  the two report sampling timestamps. `owner-valid` qualifies that report's
  permission to read worker snapshots, not all previous passes.

The two `[object-pass-scope]` rows separate admitted FA920 inactive/active passes,
with completed counts, elapsed microseconds and **same-pass** batch microseconds.
Their difference describes elapsed owner-side residual within these accepted
passes, not exclusive CPU work. Other object passes and unaccepted serial work
remain outside these counters. Nested function calls do not open an additional
pass: the original `owner` admission gate prevents it.

The UI resets its frame window even if the original object report gate skips
an open or running pass. Object counters then carry into the next report.
Compare consecutive matching valid reports; after a skipped report, the printed
`60 frames` alone does not establish the span of the accumulated object totals.
Do not subtract those carried totals from a single owner window. Raw worker
samples separately report their actual wall interval.

These functions retain the existing guest-owner caller contract. They are not
new arbitrary-thread registration/control APIs. Workers never update the new
records, and no new checks are inserted into their loops or math locks.
Shutdown is the existing exception: the main coordinator clears the observer
after joining and quiescing workers; it does not perform owner-ancestry admission.

## Worker clock observations

At each original quiescent report, after verifying the saved native owner,
current live context and generation, each of the two **existing** worker UIDs is
queried with a zeroed, correctly sized `SceKernelThreadInfo`. Both threads exist
for 0/1/2 active-worker modes; the `active` field distinguishes idle lanes. There
is no extra join, wait or new reporting cadence. A timestamp is taken after each
independent query; the samples are sequential CPU observations.

`[object-worker-clock]` prints UID, API return code, post-query monotonic timestamp,
raw cumulative `runClocks`, raw delta, elapsed wall interval, validity, reason and
cumulative read-error count. Reason codes: 0 valid delta; 1 failed/empty/invalid
record; 2 new baseline; 3 UID changed; 4 raw clock or timestamp regressed (including
wrap). Failed records invalidate the baseline; the next successful read cannot
bridge an unknown identity/failure. Baselines persist across successful reports,
so skipped reporting extends the explicitly printed wall interval. A zero raw
clock is valid if the SDK record is populated. Host runs report platform
unavailability instead of inventing a CPU-clock equivalent.

Do not convert these raw deltas to cycles, microseconds or CPU percentages without
an independent platform unit contract. The checked SDK describes `runClocks` as
clock cycles but supplies no conversion in this path. Even with a known unit,
these are whole-thread samples over the reported wall interval, including work
outside any one pass; they do not measure lock-holder CPU time or exclusive tick
CPU time. A lane's elapsed lock waits overlap pass/batch work and must not be
added to these intervals.

## Validation and observer cost

`tools/test_object_pass_timing.py --output-dir <new-private-directory>` runs the
actual production worker pool and runtime preemption STOP under ASan/UBSan. It
covers 0/1/2 workers; OFF, ON and absent ancestry API; initial allocation failure
and unsupported reinit; rejected/nested admissions; capacity and explicit joins;
wrong-context finish; open-report preservation; empty passes; generation/live
state loss; inconsistent arithmetic; shutdown with pending work; and raw clock
UID/failure/empty/wrap transitions. It executes the exact Vita query loop with a
mock kernel API to verify size initialization, IDs, sequencing and failure
handling. Three negative controls execute the real budget STOP. A real Make
cc/ar fixture verifies five transitions and rejected flags/prerequisites.

Optional `--owner-source <path-to-final-xk_owner_phase.c>` links the actual owner
observer and worker pool, testing ancestry and a context rebind through another
owner and back. The separate observer implementation supplies its own native
foreign/worker/live-fiber admission qualification.

The actual Vita compiler qualifies OFF and explicit-zero instructions,
relocations and section sizes against base 3bb0654; saved private evidence also
compares all dumped section bytes. ON compiled with current census/holder/pose
feature definitions adds 1088 bytes text and 832 bytes strings. The new records occupy **176 bytes**
(112 pass state + 64 worker samples); net `.bss` growth is 192 bytes because of
alignment. The measured `xv_object_jobs_report` native frame is 264 bytes, versus
120 bytes OFF; successful finish helper frame is 56 bytes. These sizes are
compiler/configuration-specific, not physical timing measurements.

Normal added frequency is two pass clock reads, two ancestry queries and native
identity checks per accepted measured pass; no per-job or per-guest-call timer.
At the prior checkpoint's approximately two passes/frame this is about four new
pass clock reads/frame, **not a guarantee for driving**. Quiescent reports add
one ancestry query, two SDK thread-info queries and two timestamps. Five bounded
report lines are added per report. Actual overhead requires ordinary gameplay
comparison of frame distributions or independent measurement; the implementation
does not subtract a guessed observer cost. Source-body identity checks preserve
the existing join, queue, execute, worker, mutex, service, override and HLE paths.
