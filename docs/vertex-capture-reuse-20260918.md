# Reuse exact vertex captures within a recording interval

`XV_VERTEX_CAPTURE_REUSE=1` lets repeated draws share an already-owned vertex
snapshot and its prepared upload result. The recording owner compares the
current guest bytes against its private snapshot before every reuse. On an
exact match it avoids another staging copy; the ordered worker also avoids
repeating preparation for that same snapshot. Changed data takes the existing
capture/upload path.

This complements the cumulative visibility and compact-capture changes. It does
not rely on BSP/model immutability or incomplete lock tracking. Hardware frame
times remain unverified while the Vita remote service is unavailable.

## Lifetime and eligibility

- Keys include the source identity, exact byte count, stride, packed encoding,
  compact-input encoding and render slot. Identity alone never proves a hit.
- Only the newest version of an exact key is compared. Frequently rewritten
  effects cannot trigger repeated full comparisons against every older version.
- Raw captures compare all bytes. Already-qualified compact captures compare
  the same 16-byte prefix per 32-byte source record that their shader consumes.
- Sparse referenced uploads retain independent preparation. Their GPU storage
  can contain older unfetched records, so identical input bytes alone would not
  justify reusing the result for a different reference mask.
- Cache entries and staging bytes remain immutable until the queue drains.
  Result pointers belong to the FIFO worker and survive queue-job reuse. The
  owner does not read a result before the existing completion publication.
- Every drain clears the cache, including synchronous fallback, publication,
  slot reset and shutdown boundaries. GPU copy tickets, sealing and final GPU
  retirement still govern the prepared allocations.
- A failed preparation leaves its result empty. A later matching job can retry;
  failed multi-stream draws still publish no partial streams.

The cache has 512 entries and 256 lookup buckets. Exhausting metadata capacity
retains the ordinary path; it does not drop draws. Arena pressure retains the
existing conservative reservation check and may drain before attempting a hit.
The Vita object adds 19,496 bytes of static storage and 1,484 bytes of text over
the preceding packed-capture object. Queue metadata grows by 2 KiB before
allocation rounding. There are no new threads or per-draw allocations.

The build flag defaults off. In an enabled build, startup setting
`XV_VERTEX_CAPTURE_REUSE=0` restores independent capture. Existing flags,
resolution and graphics settings remain unchanged. `[vertex-capture-reuse]`
reports exact checks/hits, staging bytes avoided and reused worker preparations.
The main capture byte counter measures bytes actually staged. These counters
do not imply an equal saving in frame time or GPU traffic.

## Validation and next hardware check

The production FIFO, uploader and asynchronous copy worker pass normal,
ASan/UBSan and ThreadSanitizer tests in six combinations: raw, packed and compact
capture, each with reuse disabled and enabled. Checks include source rewrites,
returning to an older version, source unmapping, slot/stride separation, sparse
mask changes followed by full validation, a full metadata cache, queue wrap,
startup disabling, allocation retry, nine slot generations and duplicate results
while GPU copies are delayed. TSan instruments host queue synchronization; it
cannot validate the device fences themselves.

Five actual VitaSDK flag transitions reproduce their respective objects;
unchanged mode does not rebuild. Five invalid flag values are rejected. The
disabled path passes production behavior checks; its object is not assumed to
be byte-identical after accounting was changed to actual staging writes.

On hardware, verify a fresh boot of the cumulative package and use ordinary
native-resolution gameplay. Compare frame-time variation, owner capture time,
worker time, drains and actual reuse counts. The extra equality check can cost
time on changing data, and owner comparisons replace copying rather than remove
all owner work. An FPS improvement must be measured, not inferred from tests or
saved bytes. Continue the separate BSP/model writer audit before considering
cross-frame immutable geometry storage.

Private build and validation receipts: `direct-cluster-query/capture-reuse-20260918`.
