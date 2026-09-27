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

## Perf269 ordinary gameplay findings

Hardware capture artifacts are in `../frame-slow-candidate/`: `idle.log`,
`gameplay.log`, matching marks and summaries. No automated on/off loop was used.
These are CPU Present intervals at 360p, not scanout or GPU service measurements.

| Segment | Samples | Mean ms / FPS | p95 ms | Maximum ms | Frames >100 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Lifepod | 720 | 55.810 / 17.92 | 74.045 | 141.638 | 1 |
| AR firing | 64 | 84.624 / 11.82 | 107.520 | 131.210 | 6 |
| Movement | 82 | 64.681 / 15.46 | 78.732 | 257.300 | 1 |
| Outdoor, changed camera | 925 | 48.917 / 20.44 | 64.034 | 385.863 | 4 |

The outdoor camera changed, with extra stick input observed; its average is not
an equivalent stationary-scene comparison. 309 outdoor samples exceeded 50 ms.
The sustained-20-FPS objective remains unmet. This diagnostic is not a speedup.

All decoded slow-frame partitions sum exactly to their respective intervals.
Firing frames 6872, 6874, 6896, 6903 and 6904 spent over 99% before present.
Frame 6900 spent 93.040 of 104.703 ms there, plus 11.479 ms publish/acquire.
Movement frame 7209 spent 257.120 of 257.300 ms before present. This bucket
includes guest/HLE work, scheduling, internal scene waits and prior reporting;
it is not exclusive game CPU time. Nevertheless, the final recorder drain and
final display-slot wait do not explain these firing/movement stalls.

The overlapping 60-frame report after firing has FA920 elapsed 4,099,185 us
(68.32 ms/frame, inclusive), deferred drain 4.05 ms/frame, and owner join parks
2.86 ms/frame. These overlap and cannot be added or subtracted as CPU self time.
The next recurring-cost investigation should follow the owner game-update path
and its nested collision/effects work, using existing native implementations
and prior impact-phase findings rather than repeating rejected experiments.

A separate lifepod outlier, frame 5704, spent 85.600 of 141.638 ms in
publish/acquire. Its nearby report records an 85.585-ms busy-slot wait and
85.379 ms of packet delay before first inspection. This is window correlation,
not an exact packet-to-frame causal link or proof of GPU execution duration.
Submission-pump contention remains a separate investigation.

Outdoor frame 7600 spent 385.708 of 385.863 ms before present. A nearby scene
4051 lasted 382 ms and new fragment-program link messages appeared in that
region. This does not measure shader-link duration. `runtime/xv_shader.c`
already searches embedded precompiled shader tables before opening files when
device overrides are disabled. First-use GXM registration/linking still occurs
in `xv_fshader_load`. Verify the particular program's source and time the actual
load/register/create stages before claiming storage or linking caused the hitch.

Reproduce firing partition extraction with:

```sh
python3 tools/frame_times.py ../frame-slow-candidate/gameplay.log \
  --slow-only --from-frame 6869 --to-frame 6932
```

Hardware still reports perf269/a2f3a0e0. The remote endpoint was reachable after
the user's LiveArea/sleep report; a fresh 3600-second awake lease succeeded.
No restart or settings change was performed during this follow-up inspection.

## Cold fragment load attribution prepared

Both fragment names adjacent to the outdoor hitch are present in perf269's
embedded shader table, and the captured launch reports `packaged` preference.
For these successful loads, `load_gxp` therefore uses embedded bytes without
opening a shader file. Reading those two GXP files from the SD card is not the
explanation. Allocation, copying/checking, patcher registration/linking, and
preemption still require separation.

`runtime/xv_shader.c` now replaces the existing successful-link log with a
`[shader-load-us]` record when `XV_FRAME_SLOW_MS` is valid (50–10000). It records
process-time endpoint, actual embedded/file provenance, total elapsed and four
adjacent stages: load, register, link, metadata. The metadata interval includes
existing alpha/constant diagnostic formatting. Five clock reads occur only on
successful cold loads; cache-hit draws take none. Disabled diagnostics preserve
the old successful-link log and take no clocks. Failed loads keep existing
failure handling/logging and do not produce a completed timing record.

Normal helper/recorder log suppression remains intentional: forcing synchronous
logging there previously caused stalls. Thus missing records cannot disprove a
shader hitch; visible records can attribute observed successful pump-side loads.
No shader bytes, linking keys, blend policy or resource lifetimes changed.

The changed unit compiled with retained perf269 Vita flags after correcting the
time declaration include to `psp2/kernel/processmgr.h`. Compile command and log
are private in `../shader-load-diagnostic/`. The existing material-link cache
fixture passed; it validates cache/fallback policy with a mock loader, not real
GXM timing. This source change is not yet packaged or installed. Perf269 remains
on hardware. Include this small diagnostic with the next qualified candidate;
it is not a performance optimization itself.
