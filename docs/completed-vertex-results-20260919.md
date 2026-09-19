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
performance. The first physical campaign observation is recorded below;
longer stability and movement checks remain pending.

All twelve harness configurations pass ASan/UBSan and ThreadSanitizer. Device
fences themselves are not instrumented by GCC; host queue/event/semaphore
synchronization remains instrumented. Vita cross-compilation and default/Off/On,
no-op and restoration build transitions pass. Invalid selectors and missing
reuse support fail before compilation. The Off object's instructions match
perf13 except for the changed source-line literal supplied to its assertion.

## Physical Vita first capture

`0.2.0-perf.14 / b115727+` was hash-verified and boot-confirmed in slot 1.
Runtime SHA256 is
`967359ee37a1df9f5413242558012abb0bab4af2cce6d86b862d6c4da2116e9c`.
Only the runtime and boot manifest differ from perf13. Earlier cumulative
options and graphics settings remain unchanged; persistent vertex caching is Off.
The same Normal New001 campaign checkpoint loaded through the ordinary menus.

In twelve complete 60-frame ordinary-play windows, the median shortcut count
was 3,167.5 draws/window, or about 52.8/frame. Median queued work was 3,640.5
jobs/window. Capture elapsed fell from perf13's 6.131 ms to 5.242 ms/frame;
stream preparation was 6.066 ms versus 7.053 ms. Worker preparation was
4.676 ms versus 4.782 ms. These nested/overlapping timings cannot be summed.
This confirms substantial queue bypass and lower measured capture overhead.

Total median frame time remained about 78.3 ms (logged FPS median 12.75), versus
perf13's 78.4 ms / 12.8 FPS. Draw count was 149 versus 149.5/frame. There is no
clear whole-frame FPS improvement; keep the qualified shortcut in the cumulative
research build without claiming that the isolated timing saving became an FPS
gain. This is a first same-checkpoint observation, not a paired restart study.

The initial capture contains no searched crash marker and no reported failed
capture job. During the later log pull, the host reported network unreachable,
then no route to the Vita; its neighbor entry was FAILED. The second log is
empty. No additional controller input, restart or rollback was sent after that
loss. This does not establish a game crash or its cause. Movement and longer
stability checks remain pending reconnection. Private evidence is under
`ce-perf14/gameplay/` in the unified-games workspace.
