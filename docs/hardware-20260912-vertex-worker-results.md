# September 12 vertex worker hardware result

The connected Vita contains the tested vertex worker executable from source
`21fe9ca`, SHA-256
`b4eeb211b06641ed53ddae8d6c9e5078b73bcbf4d30a33710e0e50645f169106`.
A completed Blood Gulch off/on/off comparison favors the worker by **2.49% FPS**
against the pooled off phases. This is a small improvement in one test, not a
repeatability result or achievement of the stable 20 FPS target.

The executable, configuration, rotated logs and 30 recent screenshots were copied
and hash checked: 41 files, 53,540,835 bytes. The local archive is
`/home/birchwoodgod/xita-backups/2026-09-12-211734-vertex-worker-results`.
No device executable or settings changed during collection. USB is safely
unmounted. Raw logs and images remain outside tracked source.

## Controlled comparison

The result is in `xita.1.log`, lines 5424–5812. All phases report `view-ok 1`
at position `82.2239 -90.7322 4.9681`, forward
`0.11940 -0.99283 -0.00894`. Each phase settles for 60 frames and measures 120.
The run uses 360p, 128-pixel texture limit, Low model detail, a 20 FPS cap,
500 MHz CPU request (the API reports a fallback to 444 MHz) and triple buffering
off. These are the run's logged
settings; the latest saved configuration has several different graphics options.

| Phase | FPS | Mean frame time |
| --- | ---: | ---: |
| Worker off before | 11.294 | 88.540 ms |
| Worker on | 11.627 | 86.007 ms |
| Worker off after | 11.395 | 87.759 ms |

Pooling the equal-length off intervals gives **88.149 ms/frame**. Worker-on
saves **2.142 ms/frame**, a 2.43% time reduction or 2.49% throughput increase.
The two off phases differ by 0.781 ms. Camera stability and bracketing help the
comparison, but live animation and simulation still vary: nearby reports show
226–227 draws/frame and slightly different math-call counts. Repeat trials are
needed before assigning the entire difference to the worker.

Two nearby 60-frame reports in each measured phase support the path change.
Their boundaries are not exactly aligned to the benchmark intervals:

| Supporting metric | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| Stream preparation | 4.764 ms/frame | 3.551 ms/frame | 4.754 ms/frame |
| Sum of draw-preparation categories | 14.561 ms/frame | 13.226 ms/frame | 14.392 ms/frame |
| Median C0 / C1 / C2 busy | 4 / 7 / 98% | 3.5 / 7 / 98.5% | 3 / 7 / 98.5% |

The two on reports each complete 420 worker batches and roughly 24 MiB per
60 frames, with zero caller fallbacks. Their averages are **0.686 ms/frame of
worker copy time** and **0.279 ms/frame of upload completion waiting**. The
thread startup record confirms `xv_vertex_upload` running on core 0 with affinity
`00010000`. It is doing useful recurring work, but this is too little work to
raise whole-core utilization substantially.

Both new sessions report zero caller fallbacks and zero vertex-upload allocation
failures. This only checks those reported conditions; it does not certify general
rendering correctness or crash freedom. Some normal-play windows contain larger
completion waits than the benchmark, which remain worth investigating.

## What to tackle next

The on phase still needs about **36 ms/frame** removed to reach 50 ms / 20 FPS
in this view. Further tuning a sub-millisecond copy cannot supply that reduction.
Keep the worker available for testing, with the project default still off, and
shift effort to larger blocks of work on the game thread.

1. Add bounded timing around major guest update and scene-preparation phases in
   a diagnostic candidate. Rank animation/transform preparation, visibility,
   simulation and HLE work without restoring function-entry markers throughout
   the gameplay build. Current math counters count calls, not their time; the
   ordinary build has guest function tracing and sampling disabled.
2. Audit inputs and outputs of the largest measured batch for independent object
   work. Prefer owned transform/visibility preparation with a single publication
   point. Preserve dependencies within a skeleton or shared scene structure.
3. Use the existing persistent workers for sufficiently large independent ranges;
   measure completion waits and end-to-end frame time in a matched comparison.
   Keep guest AI/physics ordering until shared state and callbacks are understood.

`recomp/kernel/xk_os_vita.c` still implements guest fibers with a semaphore baton
and one active guest at a time. Releasing every guest fiber concurrently would
change that scheduler's ownership assumptions; the new upload worker does not
make the original game state thread safe. Render submission remains on its own
pump and GXM already performs rendering on the GPU.

No additional repeat benchmark is required before this investigation. The latest
session includes campaign play with changed material, effect and queue settings;
it cannot be compared directly to the older benchmark. Recent `a30` screenshots
also show dark sky/object regions, which remain rendering follow-ups and are not
evidence of a worker speedup or regression.

## HL2 post supplied by the user

The screenshot describes distributing physics/entity work across three cores and
a rise in peak test FPS from 4 to 60. The exact code, configuration and benchmark
behind that post were not located during this review. Do not use its peak numbers
as a prediction for Halo.

An earlier [comment from the HL2 developer](https://www.reddit.com/r/vitahacks/comments/1w9ojfa/comment/p8f8r7q/)
describes a main core plus a worker, with stability preceding broader threading.
That comment predates the supplied screenshot and does not establish the state
of a later build. No verified implementation of the screenshot's changes was
imported or used to justify a scheduler change here.

The applicable design idea is to find independent batches, keep workers alive,
and publish completed results at explicit dependency boundaries. A jump from
4 to 60 is fifteenfold; spreading unchanged, compute-bound work over three
equivalent cores alone cannot explain it. Other fixes, removed stalls, reduced
work or different test conditions would have to contribute. The COD4 project
has not yet been identified from a developer link.
