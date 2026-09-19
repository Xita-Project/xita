# Vector comparison for repeated vertex snapshots

The [campaign capture](shadow-trace-20260918.md) measured vertex-stream
preparation at 17.312 ms per displayed frame in one slow combat window.
Existing snapshot reuse recorded 6,645 exact hits and avoided 45,114 KiB of
staging writes over 60 frames. Those totals include raw and compact snapshots;
they do not establish how much time belongs to raw comparisons alone.

Raw snapshot validation still called `memcmp`. It now uses the existing
`xv_bytes_equal_blocks` helper already used by resident vertex uploads. On ARM,
this compares bounded 64-byte groups with NEON and retains bounded vector and
scalar tails. It tests exact equality; no hash, approximate comparison or
assumption of immutable guest geometry replaces the original validation.
Compact snapshots keep their existing prefix comparator. Keys, mutation
invalidation, sparse exclusions, copies, queue ordering, worker ownership and
GPU retirement are unchanged. No new thread or graphics setting is added.

## Validation

The actual capture FIFO, uploader and copy worker pass normal, ASan/UBSan and
ThreadSanitizer tests in all six raw/packed/compact and reuse-off/on build
combinations. Existing fixtures cover source rewrites, restored old versions,
unmapped input after capture, different strides/slots, sparse masks, queue wrap,
capacity pressure and delayed GPU-copy ownership.

The Vita-compiled comparator also passes 21,997 fixtures / 66,043 calls against
the libc `memcmp` extracted from the installed diag.2 ELF. These include every
input alignment pair, boundary lengths, every mismatch position through 129
bytes, longer spans, and guard-page ends. There are no out-of-range reads or
writes in the ARM model.

| Equal span | Current libc instructions | Block comparator instructions |
| --- | ---: | ---: |
| 16 bytes | 58 | 29 |
| 64 bytes | 190 | 40 |
| 256 bytes | 718 | 109 |
| 4 KiB | 11,278 | 1,489 |
| 64 KiB | 180,238 | 23,569 |

These are dynamic instruction counts in a Cortex-A9 instruction model, **not
cycles, hardware timings or FPS gains**. Both methods still read the matching
bytes; memory stalls may dominate. An early mismatch can favor the scalar
routine. The private receipts are in `2026-09-18-unified-games/capture-neon/`
and the `capture-neon-*.log` files in the sibling `shadow-followup/` directory.

Hardware acceptance requires ordinary comparable gameplay after a fresh boot,
checking stream/capture elapsed time, full frame time, effects and geometry.
The earlier optimizations remain stacked. Stable 20 FPS in heavy gameplay is
not established by this change.
