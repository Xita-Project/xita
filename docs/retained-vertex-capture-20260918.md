# Retaining exact CPU vertex snapshots

This source candidate retains the capture arena's immutable CPU snapshots across
joined drains. Every reuse still compares the current guest inputs exactly.
It does not assume map/model memory is immutable, retain old GPU addresses,
or bypass upload preparation and slot retirement. Hardware performance is
unverified.

The preceding campaign captures spend about 6 ms per frame on recording-side
vertex capture. Representative 60-frame windows stage roughly 25–27 MiB even
with within-drain reuse enabled. This identifies a larger repeated-copy target
than the previously omitted reference masks; it does not establish how much of
that elapsed interval this change can remove.

## Ownership and bounds

Previously, every drain discarded all capture keys and reset the arena. The
next frame copied each source again before the uploader could discover that
its retired GPU storage still matched. Now a drain joins preparation, collects
callbacks, and clears every cached GPU result. Immutable CPU bytes and their
source/size/stride/layout/slot keys remain available for exact comparisons.
The first hit after a drain must run uploader preparation again. Later hits
within that drain interval can share its newly prepared GPU result.

Only a joined capacity boundary discards the CPU arena. Queue pressure still
joins outstanding jobs; it does not need to discard payloads when space remains.
The existing 2 MiB arena and 512-entry metadata limit remain. Changed source
versions append, sparse masks retain independent preparation, and unsupported
packing follows the original path. Shutdown clears all identities. Pending GPU
copies use the separate uploader mirror; its copy tickets and GPU retirement
remain unchanged.

Retention is enabled inside the existing opt-in `XV_VERTEX_CAPTURE_REUSE=1`
build. `XV_VERTEX_CAPTURE_RETAIN=0` at process startup restores discard-on-drain.
Builds without capture reuse retain the original behavior. The periodic
`[vertex-capture-retain]` report counts first exact hits after drains, avoided
staging writes, and arena reclaims. These counts are not GPU time or FPS.

## Validation

The production capture/uploader/worker fixture passes all six raw, packed and
compact configurations, with reuse disabled/enabled, normally and under
ASan/UBSan and ThreadSanitizer. New tests make the retained CPU payload page
read-only during a hit and remove access to guest input before the parked worker
runs. The next GPU generation deliberately puts different geometry at the old
GPU address; the retained CPU hit must obtain correctly prepared storage and
leave the unrelated geometry intact.

Checks also cover source mutation, all three retired slots, repeated empty
drains, shutdown and startup disable. Existing queue/arena pressure, counter
wrap, partial failure, allocation/notification failure, sparse-to-full and
delayed GPU-copy lifetime cases remain. A private negative control which
retains GPU results across drain fails the observable returned-geometry test.

Private receipts are `retain-{normal,asan,tsan}.log` and
`retained-capture-negative/result.json` under the unified-games workspace.
Next: build the cumulative executable, verify actual boot identity, and measure
retained-hit traffic, arena pressure and complete campaign frame times. Added
comparisons and longer hash chains can offset avoided copies; no FPS gain is
claimed before that run.
