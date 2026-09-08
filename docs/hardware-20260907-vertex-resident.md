# Vertex residency hardware result — September 7, 2026

The exact retired-slot reuse experiment does **not** improve performance in the
tested Blood Gulch view. The next candidate disables it by default and compares
the native byte-equality routine separately.

Evidence: `/home/birchwoodgod/xita-backups/2026-09-07-184059-vertex-resident-hardware/`.
The collected executable matches the installed candidate (`b997294e…`). Standard
settings match SHA-256 `8faaa4d045f6a0d834adfcd44d3ac92cc509d0bd04af705d611a6595cf95711d`.
The user confirmed this test did **not** include Warthog driving. There is no new
crash dump in the collection; this does not clear the earlier driving crash.

## Controlled comparison

Each phase settles 60 frames and measures 120. Camera position and direction
match in all phases, with `view-ok 1` throughout and successful restoration.

| Measurement | Reuse off | Reuse on | Reuse off again |
| --- | ---: | ---: | ---: |
| Measured FPS | 7.404 | 7.306 | 7.569 |
| Measured duration, microseconds | 16,207,672 | 16,424,280 | 15,854,266 |
| Stream preparation, ms/frame | 7.104 | 9.881 | 7.021 |
| GPU copies per 60-frame window | 7,140 | 60 | 7,140 |
| Copied KiB per 60-frame window | 31,426 | 39 | 31,426 |
| Compared KiB per 60-frame window | 31,420 | 62,847 | 31,420 |

Preparation values average the two full profiling windows inside each measured
phase. Pooled off throughput is 7.486 FPS; enabled throughput is 2.39% lower.
About 99.9% of upload bytes disappear, but scalar comparisons nearly double and
stream preparation rises by approximately 2.82 ms/frame against pooled controls.
Avoiding writes alone was insufficient.

The same windows retain 226 actual GXM draws/frame, with no draw-count variation.
Submission stays around 5.4–5.5 ms/frame, display queuing about 0.034 ms/frame,
and normal Finish calls remain zero. Notification latency stays around 63–64 ms;
it overlaps CPU/submission work and is not exclusive GPU execution time.

Texture preparation drifts from about 4.18 to 2.59 to 1.96 ms/frame across phases.
Do not attribute every change in total FPS to upload residency or treat this as
a perfectly settled cache experiment. The repeatable increase in stream cost
and lack of throughput gain support disabling the current scalar reuse path.

A separate 544/360/544 run measures **7.422 / 8.926 / 7.404 FPS**, with matching
views and restoration to 544p. It confirms some resolution sensitivity, but is
still far below the physical-Vita 20 FPS goal. These views differ from earlier
benchmark sessions; raw FPS across those sessions is not a controlled comparison.

## Next action

The native ELF's current `memcmp` uses a four-byte scalar loop, or a byte loop
for unaligned pointers. Test a bounded NEON equality routine for cached spans,
with residency disabled in every phase. Preserve all immutable snapshots,
single-flight submission, shaders and graphics settings. Compare actual Vita
stream cost and throughput; fewer executed ARM instructions alone do not prove
a speedup. Then check driving separately, preserving any new crash evidence.
