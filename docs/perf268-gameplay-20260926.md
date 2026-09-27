# Perf268 ordinary gameplay result

Hardware: perf268/d681f21f, verified slot 1; perf267 retained in slot 0.
Configuration: the perf267 32-key launch set, replacing explicit disabled
record-worker timing with `XV_ROOT_PAIR=1`; 360p, protected a30-perf211 save.
The game reached loaded=1, active=1 and director=1 before measurement.
Screenshots confirm the lifepod and the subsequent outdoor location.
No automated on/off benchmark was used.

| Segment | Samples | Mean ms / FPS | p95 ms | Maximum ms | >100 ms |
|---|---:|---:|---:|---:|---:|
| Settled lifepod | 720 | 59.795 / 16.72 | 77.013 | 89.382 | 0 |
| AR trigger held 5 s | 67 | 81.557 / 12.26 | 102.707 | 137.415 | 5 |
| Forward movement 5 s | 84 | 64.735 / 15.45 | 82.944 | 281.487 | 2 |
| Settled outdoors | 910 | 49.597 / 20.16 | 59.043 | 102.066 | 1 |

These are CPU Present intervals, not physical scanout or isolated GPU times.
The lifepod contained 599/720 intervals over 50 ms; outdoors 264/910 exceeded
50 ms. The outdoor average does not establish sustained 20 FPS. AR triggering
includes the clip/reload behavior and is not a per-shot microbenchmark.

Earlier perf267 means were 57.364, 78.676, 64.716 and 49.176 ms respectively.
This run demonstrates no useful whole-frame improvement from root composition;
the small Pi operation speedup did not translate into a demonstrated game gain.
The outdoor camera positions differ slightly (268: 30.65,-92.91,58.74;
267: 30.68,-93.29,58.76), so these separate gameplay samples do not prove a
causal regression or an exact performance delta.

The optimization is active: late lifepod reports accepted about 18,666–20,130
pairs per 60 frames, with 102–110 declines. Outdoors late reports accepted
14,964–15,138, with 86–87 declines. Deferred recording reported zero session
object-slot/matrix or rewritten-tag errors. No crash occurred during this short
sequence. This is not the required 15-minute active gameplay qualification,
AI-combat/audio verification, or canyon-cutscene measurement.

## Remaining stall evidence and next target

The 281.487 ms movement interval is frame 7147. Its report window (7141–7200)
has no busy render-slot wait and no full deferred queue wait. One early
slot-stall was ticket 1864, during loading; it is not evidence for frame 7147.
The movement window includes nine texture decodes (35.4 ms in the report),
13,732 deferred drain requests (105 waited, 0.97 ms/frame), and more draws and
constant rows than the previous stationary window. These are window aggregates,
not exact attribution of the 281 ms interval. GPU completion reports are
CPU-observed bounds and must not be presented as GPU service time.

The priority remains reducing typical firing and scene-preparation time, while
separately locating the cold movement stall. Inspect whether repeated empty
drain/setup calls and first-use texture/shader preparation account for those
costs before changing synchronization. Retain the qualified root-pair code as
an opt-in experiment; do not advertise it as a verified speedup or enable it by
default based only on its Pi microbenchmark.

Private evidence: `../root-pair-hardware/{idle,gameplay}-summary.json`, timing
marks, logs, launch environment and screenshots. The Vita is left in a30 outside
the lifepod, with all remote buttons released. The 20-FPS goal remains unmet.
