# Compact vertex staging

`XV_VERTEX_CAPTURE_PACKED=1` moves the existing exact prefix packing into the
capture step for already-qualified packed draws. A 32-byte source record now
occupies 16 bytes in the private staging arena. The preparation worker compares
and copies that contiguous representation directly. It previously received all
32 bytes and gathered the same 16-byte prefix again.

The flag defaults off and requires `XV_PACKED_VERTEX_LAYOUT=1`. An enabled build
can retain the previous capture path with startup setting
`XV_VERTEX_CAPTURE_PACKED=0`. Graphics settings and shader selection are
unchanged. This is a cumulative hardware candidate; hardware performance and
visual acceptance remain unverified while the remote service is unavailable.

## Why the bytes can be omitted

The existing shader admission checks limit packing to VS06/29/58 with verified
program bytes and attribute declarations. Their stream-0 attributes fit exactly
within bytes `[0,16)` of a 32-byte record. The retained alternate vertex program
already uses stride 16. Immediate, sparse, diagnostic and unsupported layouts
continue through their existing paths. This change does not broaden admission.

Every draw still captures its current source before guest execution resumes.
The worker owns the compact copy and never follows a guest pointer. Raw-input
and compact-input uploads share the same packed GPU cache key and compare the
same bytes; raw GPU layouts remain distinct. Source rewrites append new versions.
Queue publication, failed-batch handling, GPU copy tickets and slot retirement
retain their existing contracts.

Only admitted packed streams use half the staging payload. Unchanged raw streams
still copy all requested bytes. Existing `[vertex-capture]` volume now reflects
actual staging allocation; `[vertex-capture-packed]` separately records compact
stream count and avoided staging writes. The source cache-line traffic may stay
the same, and packing moves some CPU instructions onto the recording thread.
Half the payload does not imply half the time.

## Evidence and limits

In a repeated 60-frame view from the last installed build's retained log, packed
requests represented 4,145 KiB of 32,924 KiB captured. This suggests about
2,072 KiB (6.3%) fewer staging writes if the same requests take this path.
These older counters do not establish a new hardware saving or FPS change.

Production capture/uploader/copy-worker tests pass with packing disabled,
packing enabled with old capture, and compact capture enabled. Normal,
ASan/UBSan and ThreadSanitizer runs cover queue pressure, source rewrites and
unmapping, copied masks, wrap, notification/allocation failures and fallback.
New cases check nonuniform unaligned prefixes, ignored-tail changes, changed
prefixes, shorter reuse, raw/packed interoperability, doubled arena capacity for
packed payloads, nine retired slot generations and startup disabling. TSan
instruments the host queue synchronization; it cannot prove GXM device fences.

The Vita-compiled uploader passes 840 ARM cases across raw, gathered packed and
already-compact inputs, including cache hits, retired reuse and first/last-byte
misses. Memory bounds and final bytes match. Larger contiguous comparisons use
fewer modeled instructions; small comparisons can cost more. Counts omit capture
and hardware costs, with firmware copy volume accounted separately.

Five real VitaSDK flag transitions and five invalid setting/dependency checks
pass. Disabled uploader text matches the preceding build. The disabled capture
unit has compiler-output differences after equivalent capacity-check refactoring;
its raw behavior is covered by the production tests. Other runtime/guest objects
and package members are checked during cumulative packaging.

Private receipts: `direct-cluster-query/compact-capture-20260918`. Next install
the cumulative candidate with the visibility pass, verify the boot hash and a
fresh process, then inspect ordinary gameplay, staging counters, queue pressure
and frame-time variation at standard/native settings. The larger unresolved
work remains proving BSP/model writers and lifetimes before removing their
repeated validation; this capture change assumes no map-data immutability.
