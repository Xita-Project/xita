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

## Cold shader timing follow-up

Parsed 82 `[shader-load-us]` records into private `shader-times.json`.
Largest total was 830 us; independent maxima: load 812 us, registration 11 us,
link 134 us, metadata 99 us. All recorded sources were embedded. Consolidation
removed synchronous intermediate log writes from the metadata interval; the
final log write remains outside the timer. Thus the prior 40–49 ms records were
not evidence of intrinsically expensive shader parameter lookup/compilation.
This is not a controlled estimate of logging's total frame cost.

Slow frame 10683 was 267481 us, with 267290 us before Present. The remaining
movement hitch cannot be explained by the measured shader operations in this
capture. Follow pre-Present game/preparation work and logging; do not spend the
next optimization on shader precompilation based on the old metadata timings.
