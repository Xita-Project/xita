# Per-frame elapsed-time partition

Perf268's movement frame 7147 lasted 281.487 ms. The old present logger requires
more than 300 ms inside present, and the scene logger does not identify this
interval. Their silence cannot attribute the interval to the game, renderer or
GPU. Empty deferred drains already exit after a queue check; the relevant
60-frame report has about 0.97 ms/frame of actual deferred wait. Removing those
ownership barriers is not justified by the high drain-call count.

`XV_FRAME_SLOW_MS=100` enables a diagnostic (accepted range 50–10000 ms;
unset/invalid disables it). The present wrapper takes one additional timestamp
before deferred recorder synchronization. Existing timestamps bracket the rest.
Qualifying frames retain twelve values in a fixed 60-frame buffer, then emit
`[frame-slow-us]` at the existing joined-owner report boundary. This also retains
helper-side presents without trying to print from the helper.

Values are total interval, then eleven adjacent elapsed segments:

1. Before present entry, including guest/HLE, scheduling and previous reporting.
2. Deferred recorder drain and inline-state synchronization.
3. Vertex capture drain and histogram transition checks.
4. Settings/benchmark transitions.
5. EndFrame.
6. UI flip.
7. Pending GPU flush.
8. Publish/acquire (`xv_present`).
9. BeginFrame.
10. UI frame begin.
11. Texture purge.

These are wall times, not exclusive CPU or GPU service times. No new fences,
queue waits, draw sorting or rendering changes are introduced. Frame IDs are
stored at collection, not inferred from report arrival order. All eleven
segments must sum exactly to the full interval. First intervals without a prior
present are omitted. Reporting overhead belongs to the following interval.

Decode with `python3 tools/frame_times.py <log> --slow-only`, optionally using
`--from-frame` and `--to-frame`. The decoder rejects truncated, malformed or
nonpartitioning records. A synthetic sample with total 281487 us and known
75400-us publish/acquire segment passed; altered totals and truncations failed.
This synthetic sample is a parser test, not a measurement of the real stall.

The changed UI unit compiled successfully using the retained perf268 Vita
build flags in `../frame-slow-candidate/build-x87`; command/result are saved
outside Git. No new build has been deployed yet. Hardware remains perf268.
Next use this diagnostic during ordinary a30 firing/movement to identify the
slow interval's actual segment. Continue targeting typical firing and scene
cost as well; eliminating one cold stall alone does not meet sustained 20 FPS.
