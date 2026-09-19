# Retaining exact CPU vertex snapshots

This source candidate retains the capture arena's immutable CPU snapshots across
joined drains. Every reuse still compares the current guest inputs exactly.
It does not assume map/model memory is immutable, retain old GPU addresses,
or bypass upload preparation and slot retirement. The first hardware run
showed higher recording-side cost, so retention is now disabled by default.

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

Retention requires the existing opt-in `XV_VERTEX_CAPTURE_REUSE=1`
build and explicit process-start `XV_VERTEX_CAPTURE_RETAIN=1`. By default,
drains discard CPU snapshots as before.
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
## Hardware result and default

The combined `0.2.0-perf.6 / d8a3b61+` boot was verified on the physical Vita,
runtime SHA-256 `a074ca30dfb4adbbedfc5b69add9428f0e08ff8f89eaa7b0477167760e2bfcdb`.
It adds this retention and exact model fog reuse to the cumulative perf.5 stack.
Ordinary menus restore the same New001 checkpoint; no built-in benchmark runs.

The last twelve full-tick 60-frame windows show median 4,293 retained hits and
26,610.5 KiB of staging writes avoided per window, with zero arena reclaims.
However, recording-side capture rises from roughly 5.92 to 9.95 ms/frame;
stream preparation rises from 6.72 to 10.81 ms/frame. Overall median frame time
is 80.20 ms (12.45 displayed FPS), versus perf.5's 78.30 ms (12.8 FPS).
Draw counts differ slightly (152 versus 148.5), and live NPCs are not a
deterministic replay. These samples nevertheless show that removing copies
alone is not a win: the extra exact comparisons add substantial owner work.
Worker preparation remains necessary to validate retired GPU storage.

Keep the implementation and lifetime checks for further work, but require an
explicit opt-in. Preserve all earlier optimizations and test the model fog
change separately in the next cumulative build. Private captures and the
selection summary are in `ce-perf6/`.

The follow-up perf.7 hardware run confirms the default change: capture returns
to 5.98 ms/frame and total frame time to 78.20 ms (median of twelve settled
windows). The updated six-configuration fixture passes with explicit retention
coverage plus a no-environment default-off check. No queue/lifetime algorithm
changed in that follow-up.
