# Existing-scene completion census

This is a compile-only diagnostic, default OFF. It adds no scene, draw, wait,
Finish, publication, resource release or live comparison selector. No hardware
result or speedup is claimed. Its timing describes CPU observations of queued
fragment completion, including submission, execution and observation delay.

## Build and capacity gate

`RECOMP=1 XV_SCENE_CENSUS=1 XV_SCENE_CENSUS_NOTIFICATION_WORDS=0` enables the
startup mapping-metadata log and decline/structural counts without adding any
notification writes. Both settings default to zero. The feature accepts only
0/1 and requires RECOMP. Runtime/default selectors for the existing optimizations
are unchanged.

The observer needs a separately established capacity of **at least 56 unsigned
words** from `sceGxmGetNotificationRegion()`: the existing eight words remain
reserved, followed by twelve words for each of four ticket slots. This is a
capacity assertion, not an allocation request. The public SDK inspected here
does not establish that hardware capacity. Do not set 56 solely because an
emulator allocates more storage or a kernel mapping is large enough.

Startup calls `sceKernelGetMemBlockInfoByAddr` with an initialized size and logs
the return status, region, mapped base/size, memory type, type and access. It
never derives capacity from those fields. Capacity zero does not dereference
any extra word or perform diagnostic notification polling.

Feature transitions rebuild main and D3D only; capacity changes while compiled
ON rebuild main only. Capacity changes while OFF rebuild nothing. Header
dependencies use the runtime's actual `-MMD` output. No generated guest unit,
native math helper, query planner or shader asset is modified.

## Ownership and observation contract

The pump examines a sealed packet before its first BeginScene. The planner
matches RTT UI-before-command transitions and the mandatory final backbuffer,
plus the existing upscale when present. It validates counts, targets, command
kinds and UI ordering before initializing words. Twelve is a diagnostic budget,
not an engine limit: the current storage permits 3,073 world scenes, or 3,074
with upscale. An over-budget packet declines in its entirety; it never samples
only an apparently convenient prefix. Network-dialog and non-HLE packets are
untracked. Malformed metadata is not repaired or authorized by the diagnostic.

Every existing query/world/final notification expression is evaluated once.
The exact existing pointer is passed to EndScene unchanged. Only a previously
NULL fragment-notification argument receives a new diagnostic descriptor.
Actual successful BeginScene target/depth load/store policy and EndScene
cursors must match the plan; mismatches invalidate the diagnostic. The final
native or upscale notification retains all original resource ownership.

Each slot's new words initialize to `~ticket`, including uint32 ticket wrap.
Descriptor/word/bracket storage remains retained until the original final
notification or existing failed-packet Finish permits retirement. Younger
packets can be observed without being published or retired. Missing, invalid,
failed and unsupported observations never delay retirement. An illegal live
slot reuse records an orphan and preserves the earlier slot. Shutdown does not
introduce diagnostic cleanup waits; an unretired diagnostic slot remains live.

A sweep has one timestamp before its bounded loads and another after them.
For a scene, the last false sweep's *before* time is its lower bound; the first
true sweep's *after* time is its upper bound. Without a negative observation,
packet submission begin is the lower bound. Completion during submission is
therefore not incorrectly placed after submission. Adjacent-scene difference
bounds are `[max(0,L_i-U_previous), U_i-L_previous]`, never GPU active durations.
The first interval starts at packet submission. Signals first seen together
remain unresolved; a false earlier load followed by a true later load can be
progress during one sweep. Backward/discontinuous intervals and sum overflow
are excluded. Normal uint64 timer wrap is handled by bounded unsigned deltas.

## Report and samples

Reporting uses the existing 60-retirement cadence. One summary and at most
twelve rows are emitted, outside draw loops. Only complete valid packets with
the first valid packet's exact shape **and depth policy** contribute timing rows;
other shapes count as mixed. This deliberately avoids averaging unlike passes
under one scene ordinal. Submission counts/attachments, poll counts and retired
packet counts may straddle a report boundary; they are not interchangeable
denominators. Resetting totals leaves live packet brackets intact.

Row completion and gap bounds are **sums in microseconds**, with `n` as their
denominator; maximum bracket and maximum poll gap show observation precision.
Summary attachments distinguish new/query/scaled/final notifications and failed
EndScenes. Failed packets, missing signals, invalid intervals, orphans, cap and
other declines are explicit. Sweep time measures only the poll sweep, excluding
planning, sample extraction, aggregation and logging; it is not total observer
cost. With capacity zero, no timing rows or samples are admitted.

For each admitted scene, `(ticket + scene ordinal) % recorded draws` selects one
mesh command. Only that command's successful actual draw records a sample;
skipped/rejected/failed selected draws remain unsampled. The tuple contains the
command, requested PS key/table entry, VS function hash, actual fragment-program
pointer and patcher ID, selected route (heuristic/linked/depth), actual alpha
variant, blend, and recorded material kind. Program pointers/IDs are exact
within that process; table entries require the matching generated shader table.
A requested PS key does not imply that the requested shader survived fallback.

The read mask is collected after active sampler resolution, previous-frame
substitution and cube/fallback selection. Bits 0–7 indicate matching RTT bases,
8 scene-backbuffer, 9 previous-frame, 10 other/unknown and 11 bind error. Exact
base comparison does not prove absence of aliasing. UI, overlays, settings,
nonselected draws and hidden dependencies are explicitly incomplete. Upscale
has structural/timing data but no mesh shader sample. These samples cannot
authorize reordering or parallel execution.

## Validation and measured storage

`tools/test_scene_census.py --output-dir <new-private-dir> --shader-dir <owned-stage>/shaders`
runs production replay/planner and pump extractions with ASan/UBSan, texture
resolution, the actual post-draw sample tuple, metadata-query success/failure,
and actual ARM main/D3D OFF/ON object compilation. The owned shader directory
is needed because checkout placeholder PS tables do not match current D3D.
Without it, host tests run and the optional ARM check is omitted.

Coverage includes 500 randomized replay cases; exact 12/13/3,074-scene bounds;
multiple targets, UI boundaries, repeated query IDs and later query writers;
original pointer identity; delayed, coalesced, missing and younger signals;
ticket/timer wrap; slot-pressure/illegal reuse; retained cancellation state;
Begin/End failure and checked sum overflow. Production
replay traces match OFF/ON apart from notification arguments. The production
pump fixture covers 23 native/scaled/query/error/wrap/submission-time cases;
its GPU and draw execution are mocks, not hardware qualification.

`tools/test_scene_census_build.py --output-dir <new-private-dir>` runs the real
Makefile with real small host objects through ten transitions and two header
touches, checking exact compiler flags and no guest recompilation. Existing
query-boundary, scene-fence, visibility-placement, draw-texture and acquisition
tests also passed. No device, executable launch or full package build was used.

ARM `nm -S` gives 6,368 packet bytes, 2,072 totals bytes and two four-byte
pointers: **8,448 bytes** of private static state, plus the proposed 192-byte
notification reservation. This is actual symbol storage, not net padding or a
one-KiB estimate. Feature-OFF objects contain no census symbols. Bounded stack
temporaries and diagnostic text/code are additional. The observer does not
allocate per packet or keep unbounded histograms.

Private evidence is under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/scene-census-implementation/`.
The hardware gate remains notification capacity. After that gate, a physical
capture must establish actual attachment coverage, scene counts/declines,
bracket widths and observer cost before interpreting completion differences.
Maximum pending one does not establish GPU idle time; different camera views
do not establish equal GPU service time.
