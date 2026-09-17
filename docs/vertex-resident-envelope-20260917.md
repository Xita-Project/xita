# Exact resident vertex ranges in asynchronous uploads

This default-OFF residency experiment now avoids real GPU writes for clean
leading/trailing ranges and completely clean batches. Previously a resident hit
skipped the guest-to-mirror snapshot, but the asynchronous worker still copied
the entire batch to GPU storage. Residency remains opt-in through the environment
or a process-start build default; this change has not been integrated, deployed
or measured on hardware.

The dispatcher retains one dirty envelope per slot between its existing 64 KiB
batch triggers and final seal. Nonresident snapshots extend that envelope;
newly initialized alignment bytes are included. A dispatch copies from the first
dirty range through the last dirty range, including resident holes between them.
No per-draw queue or allocation is added. Each original dispatch opportunity
produces at most one copy, and a shortened range below 4,096 bytes retains the
existing caller-copy fallback. When residency is OFF the copied ranges and batch
triggers are the original ones, with additional bookkeeping/counters.

An all-clean batch advances the dispatched prefix without submitting a job or
clearing the last real pending ticket. This includes ticket 0 after wrap. The
pump must still wait for an earlier copy even when the frame ends in clean bytes.
Mirror reset seals and joins CPU copies, then clears dirty/current-frame state;
it preserves the initialized prefix. Final GPU completion remains the prerequisite
for acquiring the slot. Early query completion, display release and CPU upload
completion do not release geometry. No frame ownership, worker, scheduler,
shader, comparator, vertex-reference or draw-order policy changes.

The existing `[vertex-resident]` report now labels hit bytes `avoided-snapshot`,
meaning avoided guest-to-mirror bytes. New `[vertex-transfer]` counters separate
producer-queued GPU bytes, caller GPU bytes (including synchronous uploads and
padding stores), async caller-fallback/tail counts, clean dispatch windows and
bytes omitted, and resident holes/old padding recopied inside envelopes. These
producer counts are not worker-completion totals or GPU timing. Existing
`[vertex-worker]` completed-job measurements can cross report boundaries.
Requested comparison bytes also do not count early-exit bus reads.

## Qualification

`make -C recomp/host test-vertex-resident-worker` exercises the actual production
uploader and worker with pthread-backed Vita calls. It journals the real memcpy
destinations/lengths, independently of the new counters. It covers:

- All-clean batches, dirty prefixes/suffixes/middles, clean internal gaps,
  residency-OFF full-range identity, exact counter distinctions and unchanged
  dispatch count.
- Initial padding, newly exposed padding, shorter frames retaining an older
  valid prefix, subsequent reuse of those old bytes, and same-frame mutations
  appending a new snapshot.
- Three concurrent slots, delayed worker completion, repeated seal, both pump
  wait and reset wait, wrapped ticket 0, source mutation after capture and clean
  tails that retain the last pending ticket.
- Three worker startup failures, failed signal and full queue, with the exact
  shortened envelope copied on the caller and no lost bytes.

Normal, ASan/UBSan and ThreadSanitizer runs pass. Four private production-source
negative controls are rejected: restoring full-batch writes, erasing the pending
ticket in a clean batch, omitting padding, and failing to extend through a later
dirty range. Existing upload, sparse-reference, asynchronous-worker and joined
preparation-worker suites also pass with grouped comparisons enabled under
ASan/UBSan. Host equality uses the portable implementation; the unchanged grouped
NEON helper retains its separate prior ARM qualification.

The actual Vita uploader object compiles through the production Makefile with
`XV_VERTEX_BLOCK_LOADS_DEFAULT=1`. Compared with the same base source and flags,
object text/rodata grows 5,499→6,147 bytes and BSS 101,672→101,764 bytes; pool metadata
grows 36 bytes across three slots. Those are object sizes, not runtime costs.
Default-OFF uploads now maintain a dirty envelope and traffic counters; no claim
of zero disabled-path overhead is made. No new hot-path clocks are added.

Private receipts and test binaries are under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/vertex-residency-dirty-envelope/`.
Only `runtime/xv_vertex_upload.c` changes production execution. A retained-stage
integration would rebuild that runtime object and relink; it needs no guest-code
regeneration, shader rebuild or new selector. Review comes before any integration
or enablement.

The September 7 scalar residency result remains negative. Current grouped
comparison and upload-worker behavior differ, but byte savings do not prove a
frame-time improvement. Internal resident holes remain copied deliberately to
bound job/metadata cost. Fresh-launch cumulative gameplay validation remains a
separate root-owned gate after code review; graphics and the retained worker
should stay unchanged.

## Fresh-launch selection

`XV_VERTEX_RESIDENT_DEFAULT=0` is the repository build default. A cumulative
fresh-launch candidate can append `XV_VERTEX_RESIDENT_DEFAULT=1` to its retained
Make arguments, leaving the other feature flags and graphics unchanged. The
uploader uses this default only when `XV_VERTEX_RESIDENT` is absent. Explicit
environment strings retain the original `atoi` behavior: `0`, empty and invalid
strings disable residency; `1`, other nonzero numbers and negative numbers enable
it. Numeric prefixes remain accepted. Configuration is cached once; subsequent
environment edits do not change it. The existing override still normalizes
nonnegative values to OFF/ON and restores the cached selection for negative values.

On first use, the existing uploader log path reports
`[vertex-resident] startup mode N (build default D)`. It reports the effective
selection, including a prior override, and runs once. The periodic
`[vertex-resident]` report continues to show the current mode and actual checks/
hits; a startup ON line alone does not prove residency was applicable or faster.
No startup setter, runtime selector, new counter or hot-path clock is added.

Both C and Make reject nonboolean numeric defaults. The separate
`build/vertex-resident-startup.config` stamp changes only when the selected
default changes; its only object dependency is `runtime/xv_vertex_upload.o`.
Changing 0→1→0 rebuilds that object and causes a normal final relink, without
invalidating UI, D3D, shader, main or generated guest objects. Grouped comparison
and RGBA layout startup flags remain independent.

Run the focused startup checks with:

```sh
python3 tools/test_vertex_resident_startup.py \
  --vitasdk /home/birchwoodgod/vitasdk \
  --output-dir /tmp/vertex-resident-startup-check
```

The output directory must not already exist. All 66 separate-process cases pass:
macro absent/0/1, ten absent/explicit environment choices, synchronous and real
worker uploads, and preexisting override selection. Actual first uploads and
retained slot reuse prove the mode; exact bytes/padding, queued versus caller
writes, cached environment selection, override restoration and both mode logs
are checked. Six real Make builds with small C inputs verify uploader-only flag
scope and incremental invalidation; invalid C and Make defaults are rejected.
The fixture uses the existing production uploader/worker with host Vita stubs.
It does not repeat the already-qualified envelope/worker concurrency suite or
claim fresh hardware validation. Startup receipts are under the private evidence
directory's `startup/` subdirectory.
