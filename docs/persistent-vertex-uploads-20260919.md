# Retaining immutable vertex uploads across frames

The perf10 campaign capture still spent about 6 ms per frame capturing vertex
inputs, although the later uploader usually found resident copies. The capture
producer copied live guest bytes into staging before the worker validated them
against its own mirror. The new optional cache checks current bytes once on the
recording owner and can reuse an immutable GPU upload directly.

`XV_VERTEX_PERSISTENT=1` selects this path at build time. It defaults Off in the
repository; a selected build can disable it at startup with the same environment
setting set to `0`. The existing capture worker must also be enabled. Its strict
0/1 build stamp changes only the capture object. Other preparation paths remain
available when the cache cannot accept an input.

## Ownership and retirement

The cache owns a 2 MiB cached CPU mirror and a 2 MiB GPU-readable uncached pool,
with 4 KiB pages and 256 entries. It accepts dense raw streams from 256 bytes to
256 KiB. Packed and sparse streams use their existing preparation paths. Every
hit checks source identity, byte count, stride and all current source bytes;
guest memory is never assumed static. A changed input gets a new immutable
version. Capacity or allocation failures fall back to ordinary preparation.

The recording owner alone changes allocation metadata and slot-reference bits.
The FIFO worker copies new mirrors to GPU memory and never reads live guest
memory. Pending hits are safe because their creating job precedes them in the
FIFO. Each job uploads all promised new entries before processing ordinary
streams, so failure of an earlier ordinary stream cannot leave a later cache
entry uninitialized for an already queued hit. The existing write barrier and
completion publication make those writes visible before drawing.

An entry remains pinned until **every referencing GPU frame slot retires**.
`xv_d3d_BeginFrame` releases only the slot already acquired by the presentation
path, after draining CPU preparation. A CPU drain by itself never releases GPU
references. Shutdown retains the upload pools' existing GPU-drained precondition.
There is no in-flight eviction and no new full-GPU wait.

This differs from the earlier retained CPU-staging experiment: a hit here also
bypasses the worker's second resident comparison and upload preparation. Its
benefit still needs hardware measurement; byte comparisons, metadata scans and
4 MiB of additional pool storage have costs.

## Evidence and measurement

`python3 tools/test_vertex_capture.py` runs the production FIFO, uploader and
copy worker under host Vita service mocks. `SANITIZE=1` enables ASan/UBSan;
`THREAD_SANITIZE=1` enables TSan. Tests cover immutable pending hits, source
mutation/unmapping, all three GPU slots, allocation failures, queue wrap and
partial-job failure. Host checks validate ownership and bytes; they do not
replace hardware GPU retirement or long-session testing.

All eight build configurations passed normal, ASan/UBSan and TSan runs, including
persistent uploads with packed layout and capture reuse both disabled. Enabled
tests also cover sparse/packed bypass, separate page and metadata exhaustion,
fragmented multi-page allocation and preservation of other slots' bytes. The
Vita cross-compile checks rebuilt on default/On transitions, stayed unchanged on
explicit Off and repeated On, and rejected invalid configuration values.

The joined `[vertex-persistent]` log row records hits, creations, capacity
fallbacks, compared bytes, avoided capture bytes and uploaded bytes. Compare
these with capture/worker times and total frame time in ordinary gameplay.
Reduced copying alone is not an FPS result.

## Physical Vita result: keep disabled

`0.2.0-perf.11 / d76aa4f+` booted successfully on hardware and loaded the same
Normal Pillar of Autumn checkpoint with unchanged graphics settings. Its runtime
SHA256 was `dca02f1cb99a864c7c603647b14b3c26291ead7f51e2d8ab76de77e2a28bc77c`.
Only the game executable and its boot manifest changed from perf10. No emulator
or diagnostic benchmark was used.

In the last twelve complete 60-frame ordinary-play windows, the cache recorded
a median 5,874 hits, six creations and zero capacity fallbacks per window. It
avoided 44,726.5 KiB of capture writes and uploaded just 34 KiB per window.
Worker preparation fell from 4.781 to 0.678 ms/frame, but recording-owner capture
rose from 5.978 to 10.716 ms/frame; streams preparation rose from 6.824 to
11.535 ms/frame. Exact comparisons on the recording owner displaced useful worker
overlap. Eliminating copies did not eliminate the serial validation cost.

Median total frame time was 79.9 ms / 12.5 FPS versus perf10's 78.2 ms / 12.8 FPS.
The draw counts were similar (149 versus 152 per frame), but live NPC movement
means this is not a deterministic scene comparison. The earlier perf11 sample
was also no clear win (78.95 ms / 12.7 FPS). The consistent preparation-cost
increase and absence of a total-frame improvement do not justify enabling this
option in the cumulative build. The hardware was selected for rollback to
perf10; previous accepted optimizations remain intact. This cache remains
available behind its default-Off selector for future work on validation cost or
guest write ownership. No long-session/combat stability claim follows from this
short test.

Private evidence: `ce-perf11/gameplay/{initial,checkpoint}.log`, corresponding
summary JSON and screenshots, plus package, configuration and host-test records
in the unified-games workspace.
