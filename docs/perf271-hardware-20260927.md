# Perf271 physical Vita capture — 2026-09-27

Build perf271 / 7b9b849d retains the polygon register candidate and consolidates
cold shader logging. Object-worker readiness changes are NOT included.
360p, protected a30-perf211 save, same cumulative 32 launch settings.
Receipts and screenshots: private ../shader-log-hardware/.

| Window | Samples | Mean ms / FPS | p95 ms | Maximum ms | >50 ms | >100 ms |
|---|---:|---:|---:|---:|---:|---:|
| Lifepod idle | 780 | 57.269 / 17.46 | 74.099 | 88.582 | 572 | 0 |
| AR held five seconds | 69 | 76.256 / 13.11 | 95.131 | 116.766 | 68 | 2 |
| Forward movement | 82 | 65.819 / 15.19 | 80.344 | 267.481 | 82 | 2 |
| Outdoor stationary | 916 | 49.180 / 20.33 | 58.592 | 92.562 | 245 | 0 |

These are CPU Present intervals, not GPU execution or scanout timings. Screenshots
confirm lifepod before/after idle, reload pose after firing, and outdoors after
movement. Controls released on completion. No crash in this short sequence.
No new rendering correctness, AI combat, audio, cutscene or 15-minute stability
claim follows. Existing visible HUD/tree issues remain.

The outdoor mean meets 20 FPS, but 27% of its frames exceed 50 ms; firing and pod
remain below target, with a 267 ms movement hitch. Different run timing/view
prevents attributing small differences versus perf270 to shader logging alone.
The goal remains unmet. Next investigate safe diagnostic ownership for the
newly admitted object workers before hardware promotion, and inspect cold shader
and slow-frame records for the remaining movement stall.
