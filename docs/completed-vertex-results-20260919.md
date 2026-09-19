# Reusing completed vertex preparation without another queue job

The capture FIFO already reuses exact snapshots and prepared GPU results within
its existing lifetime. Nevertheless, each repeated draw publishes another job,
wakes the worker and reports completion, even when that result has finished.
`XV_VERTEX_CAPTURE_READY=1` lets the recording owner publish completed results
without repeating that queue round trip. It defaults Off and requires capture
reuse. It does not extend any snapshot, GPU allocation or frame-slot lifetime.

After acquiring and collecting completed jobs, the shortcut requires that no
previous job remains outstanding. Every stream must pass the existing exact
source comparison and have a non-null prepared result. All pointers are staged
before any target or callback is published. A pending predecessor retains FIFO
callback order by forcing the normal path. Sparse streams, changed sources,
missing results, failures and disabled capture reuse retain normal preparation.

Mixed hit/miss batches reuse preflight IDs rather than comparing successful
hits twice. Pressure-driven drain/reclaim discards those IDs before rebuilding
the job. A completely ready draw needs no staging allocation and can bypass even
when the CPU arena is full. Joined drains still invalidate ordinary GPU results;
retained CPU snapshots alone do not qualify. The existing upload-copy tickets,
frame seal/wait and GPU slot retirement remain required. No GPU wait was added.

`[vertex-capture-ready]` counts draws completed by this shortcut. The existing
job count continues to count actual FIFO jobs. Capture timing includes exact
preflight checks and inline publication but excludes pressure-driven joining,
as before; worker timing and worker-reuse counts cover only queued work. These
counts do not measure an FPS improvement.

## Qualification

The production FIFO/uploader/copy-worker harness covers both enabled and disabled
builds, raw/packed/compact captures and the independent persistent-cache option.
Focused cases exercise two-stream all-ready publication; mixed hit/miss without
duplicate exact comparisons; outstanding predecessors; source mutation;
post-drain invalidation; full-arena ready hits; full-arena mixed misses forcing
reclaim; failed mixed jobs with no partial target publication; packed prefix/tail
changes; and reuse while the separate upload-copy worker is deliberately paused.
The latter still requires seal/wait before reading GPU bytes.

The actual production callback and synchronization contract were reviewed.
The success callback is a no-op and results are published before the containing
command list is submitted. The worker's completion release, acquired by collect,
protects the result reads; an empty FIFO prevents subsequent worker writes until
the owner publishes another job. Host tests do not execute GXM or prove hardware
performance. Physical campaign validation remains pending.

All twelve harness configurations pass ASan/UBSan and ThreadSanitizer. Device
fences themselves are not instrumented by GCC; host queue/event/semaphore
synchronization remains instrumented. Vita cross-compilation and default/Off/On,
no-op and restoration build transitions pass. Invalid selectors and missing
reuse support fail before compilation. The Off object's instructions match
perf13 except for the changed source-line literal supplied to its assertion.
