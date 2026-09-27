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

## Perf269 deployed

Full retained-stage build completed successfully. Package comparison against
perf268 changed only `game-a.self` and `boot-game.txt`; updater contract stayed
`775a18633b824a8ed092a7883713a88fff592bda190db0ffbc8e01614d7d4897`.

- Runtime size: 34,818,070 bytes.
- Runtime SHA-256:
  `a44a99534763ec0df560dba7d89d119d9c145345991a874fbcb943768fde1030`.
- Package SHA-256:
  `86bbe55a6eccc843a0782361913d38db1aa24d59840c0c1c995f25aae79b5814`.
- Private package: `../frame-slow-candidate/xita-perf269c.vpk`.

The deployment receipt reports verified=true, boot_confirmed=true, slot 0.
Live dashboard status confirmed perf269/a2f3a0e0 with timing_frame=0. Perf268
remains in slot 1. The one-hour awake lease was renewed after restart.
The ordinary launch sequence has started; no gameplay result is claimed yet.

Launch keeps the perf268 optimizations, including the experimental root pair,
and the protected a30-perf211 save. It replaces `XV_FRAME_DRAWS=1` with
`XV_FRAME_SLOW_MS=100` to fit the 32 environment-key limit. Aggregate draw counts
and frame-time samples remain. This build is for attribution, not a claimed
performance improvement or an automated A/B comparison.
