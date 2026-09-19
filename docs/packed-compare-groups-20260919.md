# Grouping exact packed-vertex comparisons

The owner-copy comparison in perf.34 did not establish a whole-frame benefit and
increased worker/copy workload. Retain the snapshot/result reuse path and test
reducing the cost of its exact packed-prefix comparison instead.

With the existing `XV_VERTEX_WIDE_COMPARE=1`, `xv_packed_equal` now checks the
first four vertices promptly, then accumulates differences across groups of
sixteen before reducing to an ARM scalar condition. Smaller spans and remaining
tails retain four-record/single-record checks. Only the first 16 bytes of each
32-byte source record are read, matching the existing shader-input contract.
It compares every relevant bit; there is no hash, approximation, source-lifetime
extension, relaxed publication rule or changed draw ordering. The ordinary flag
default remains zero. Copy code is unchanged.

`tools/test_arm_packed_compare.py` executes both flag values on a Cortex-A9 ARM
instruction emulator and checks exact outcomes, read ranges and unchanged inputs.
It covers counts 0–20 and larger boundary sizes through 1,024 vertices, three
alignments, equal data, first/middle/last prefix differences and unused-tail
changes. All 852 cases pass. A 1,024-vertex equal comparison falls from 6,663 to
5,596 executed instructions; the selected aggregate long equal cases fall from
34,425 to 29,364. These are instruction counts, not measured cycles, cache costs
or FPS. Grouping may spend longer before detecting a difference inside a later
group; real source distributions and hardware scheduling still matter.

The production ARM uploader fixture accepts `--wide-compare` to exercise the
same helper within complete upload entry points, including compact and raw
layouts, resident/cached data and changed inputs. Its result and hardware
qualification must be recorded before claiming a performance improvement.
Private direct-comparison results: `packed-compare-arm/result.json` in the
unified workspace. This work uses instruction emulation for correctness; it is
not a Vita3K performance test.

Production-entry validation completed: all 840 ARM uploader cases pass with
`--wide-compare`, including range checks and exact CPU snapshots. Results and ELF
hashes are preserved in `packed-compare-uploader-arm/receipt.json`. GPU-copy
execution and real hardware timing remain outside that fixture's scope.
