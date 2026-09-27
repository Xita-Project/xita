# Perf267: scene-helper portal clipping on hardware

Installed build: `0.2.0-perf.267 / c1a2d36a`, verified updater slot 0.
Perf266 remains the rollback slot. Existing settings are retained, including
360p and `XV_TEST_SAVE=a30-perf211`; `XV_SCENE_PORTAL=1` replaces the disabled
scene-wait sample key. No automated A/B loop was used.

The ordinary launch initially showed the loading screen. The collector waited
for loaded/active/director telemetry; a screenshot then confirmed the a30
lifepod before sampling. Loading/menu frames are excluded.

| Segment | Samples | Mean ms | FPS | p95 ms | Max ms | >100 ms |
|---|---:|---:|---:|---:|---:|---:|
| Lifepod | 720 | 57.364 | 17.43 | 75.074 | 95.994 | 0 |
| AR trigger held | 68 | 78.676 | 12.71 | 106.101 | 131.987 | 7 |
| Walk outside | 82 | 64.716 | 15.45 | 80.344 | 257.636 | 2 |
| Outdoor stationary | 918 | 49.176 | 20.33 | 58.970 | 113.159 | 1 |

These are CPU Present intervals, not physical scanout measurements. Trigger and
movement brackets include asynchronous input/status latency. The post-trigger
image shows a reload; this is not a pure isolated firing-kernel measurement.
One movement interval exceeded 200 ms (frame 7072, 257.636 ms); none of the other
sampled segments did. The only slot-stall receipt was earlier during loading
(ticket 2703); no busy-slot wait was reported in the sampled movement window.
That does not by itself assign the movement stall to a specific CPU routine.

The scene portal route is active: late lifepod windows admitted roughly
923–943 clips per 60 frames with 81–93 budget declines. Outdoor examples admit
956–981, with 31–64 budget declines; other decline categories remain zero in
those windows. Budget failures retain the original execution path.

## Interpretation

Perf266's earlier captures were lifepod 55.799 ms / 17.92 FPS, trigger 73.188 ms /
13.66 FPS, movement 70.048 ms / 14.28 FPS, outdoor 53.444 ms / 18.71 FPS.
Thus perf267 does not establish a universal gain: the pod and trigger averages
are worse, while movement and outdoor averages are better.

Outdoor screenshots show the same general view but different position and
pitch. Perf266 ends near (30.04,-96.25,58.89), forward (-0.03,0.99,0.12);
perf267 near (30.68,-93.29,58.76), forward (-0.08,1.00,0.00). Ammo/reload state
also differs. Do not attribute the full outdoor difference to clipping.

Nearby asynchronous helper reports show pod helper CPU around 45.92 -> 44.79
ms, with completion-to-owner-notice around 7.39 -> 9.23 ms. Outdoors, helper CPU
is around 48.64 -> 45.13 ms. These reports are not exact synchronized partitions
of the frame. They support investigating remaining owner/tick overlap, not
adding component averages or claiming a causal FPS gain.

The candidate remains enabled for further ordinary testing, with rollback
available. No crash occurred during these short samples. No claim is made of
15 minutes active crash-free play, corrected AI/effects, checkpoint verification,
or the opening canyon cutscene target. The outdoor mean alone does not satisfy
sustained 20 FPS: 240 of its 918 intervals exceed 50 ms.

## Next work

Retain the qualified scene path while investigating the remaining simulation
and effects costs. The recent phase capture places 8DDF0 transform preparation
at about 12 ms inclusive, with a substantial untimed remainder; its native
basis and hierarchy hooks are already active. A source audit confirms basis
acceptance and hierarchy batches in this run, so do not reimplement those
existing optimizations or mistake intentional final-node bounds declines for
a new whole-path failure. Identify remaining descendants/native work before
selecting another replacement. Firing and movement spikes remain separate
requirements from the now faster outdoor stationary segment.

Private evidence: `../portal-helper-candidate/idle-summary.json`,
`gameplay-summary.json`, `helper-comparison.json`, input marks, full logs and
screenshots. Source branch remains private; no push was performed.
