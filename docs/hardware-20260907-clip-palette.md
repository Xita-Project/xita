# Clipping and palette build hardware follow-up — September 7

The latest installed build completes the Blood Gulch **resolution** benchmark
at **6.315 / 7.559 / 6.327 FPS** for 544p / 360p / 544p. Camera and reported draw
counts match across its phases. The new clip-register and palette-cache paths
execute throughout. **The CPU off/on/off benchmark did not run**, so their
combined performance benefit remains unmeasured. Sustained 20 FPS remains unmet.

## Collection and build identity

Read-only USB collection at 09:49 CDT copied and hash-verified 63 files totaling
487,170,739 bytes, including logs, configuration, executable and saves, into:
`/home/birchwoodgod/xita-backups/2026-09-07-094923-clip-palette-hardware`.

The executable is 32,918,474 bytes with SHA-256
`bad95dd9dbd3bc92de3db59e7a7edecbc677fc9acab19fb229b36fcebde0b591`,
matching the [validated and installed candidate](clip-palette-20260907.md).
Configuration matches the predeployment backup byte for byte: 544p, textures
256, automatic filtering, mip smoothing On, High material/glow/particles and
a 20 FPS cap. Effective clocks are CPU 444, bus/GPU 222 and XBAR 166 MHz.
No device files were changed. USB storage is safely unmounted.

No September 7 screenshots were present. No CPU-test markers occur in any
collected log. The current `xita.log` contains one completed resolution test;
the earlier `xita.1.log` session contains neither benchmark. Old `xboxvita*`
logs contain historical shader errors, but their hashes are unchanged since
the predeployment backup. They are not errors from this new session.

## Resolution result

Each phase settles for 60 frames and measures the following 120 frames.

| Phase | Resolution | FPS | Mean frame time |
| --- | --- | ---: | ---: |
| Before | 960×544 | 6.315 | 158.35 ms |
| Reduced | 640×360 | 7.559 | 132.29 ms |
| After | 960×544 | 6.327 | 158.04 ms |

The pooled 544p result is 6.321 FPS. Reducing resolution improves throughput by
19.6% and saves 25.91 ms/frame; the two 544p phases differ by only 0.20%.
Position `37.4348 -68.1313 0.8808` and forward `0.24213 -0.97027 0.00000`
match throughout. The benchmark completes and restores 544p.

This view produces 316 draws/frame in each supporting report, versus 199 in
the [preceding build's test](hardware-20260907-overnight-performance.md).
The camera is also different. The lower FPS does not establish a code regression
or an optimization speedup between builds. Simulation is not frozen even
within a benchmark.

Supporting 60-frame reports and CPU samples have different boundaries from
the exact 120-frame benchmark intervals:

| Supporting metric | 544p before | 360p | 544p after |
| --- | ---: | ---: | ---: |
| Render submission | 8.43 ms | 8.48 ms | 8.41 ms |
| Final graphics-completion wait | 67.05 ms | 40.36 ms | 67.17 ms |
| Draw preparation | 14.43 ms | 13.33 ms | 13.33 ms |
| Query wait per guest frame | 27.93 ms | 9.54 ms | 28.54 ms |
| Query completion-to-resume | 197 µs | 119 µs | 96 µs |
| Nearby C0 / C1 / C2 median busy | 4 / 9 / 78% | 4 / 10 / 89% | 4 / 8 / 78% |

Engine, render and query intervals overlap; do not add them. Graphics-completion
waiting is an elapsed CPU-side interval, not exclusive GPU execution time.
The resolution response supports a material GPU workload contribution. The
remaining 132 ms/frame at 360p, compared with the 50 ms budget for 20 FPS, and
the high core-2 utilization support continued CPU preparation/translation work.
The 360p elapsed profile places indexed drawing at 10.0% and clipping at 9.7%;
these sampled shares do not establish exclusive CPU time or attainable savings.

## Path coverage and checks

- Each supporting measured-phase report records 49,800 native clip calls, all
  on the new integer-local path: 830 calls/frame. The 16 world reports total
  769,460 calls. There is no disabled phase for comparison.
- Each measured-phase report reuses 1,500 palette hashes with zero rehashes,
  after checking all 1,024 bytes per binding: 25 bindings/frame. World reports
  total 23,174 reuses and one hash. These counts establish execution, not savings.
- Sampler preparation reuses 52,260 of 75,120 lookups per measured report
  (69.6%). No textures are decoded during measured phases; about 8.7 MiB is
  resident in the texture cache. More decode workers or cache capacity cannot
  remove work that is absent from these intervals.
- The depth-only replacement handles three draws/frame, less than 1% of the
  reported draws. Common color-writing shader passes remain the wider GPU
  investigation target; index or draw counts alone do not measure GPU cost.
- All 78 complete render reports have matching stage sums and frame counts,
  60 final Finish/display-queue calls and zero intermediate target Finish calls.
  No explicit render-target/shader failure or workload-table overflow appears
  in the current session. Geometry diagnostics were off, so this does not
  certify geometry correctness.
- All 860 world-bracketed visibility waits have ready timestamps. Mean
  completion-to-resume is 123 µs, with an 11.39 ms maximum and 35 fallback sleeps
  across those reports. Notification latency does not explain the frame-budget
  gap by itself.

## Next measurement

The installed build already has the controlled CPU comparison. In Blood Gulch,
stand still in first person with menus closed and press **L + R + Square**.
Wait until the **CPU** panel disappears, allowing about two minutes. This
compares the two changes **off → on → off** at one resolution and restores
preferences afterward. **L + R + Select** runs the separate resolution test.

Use that comparison before accepting or reverting these two changes on
performance grounds. Continue profiling indexed-draw preparation, translated
clipping and common color-writing GPU passes; the present evidence does not
support RAM expansion or moving more texture decoding as the next remedy.

Raw logs, file hashes, collection metadata and general/benchmark/optimization
analysis scripts and JSON results are retained in the archive. No executable
or settings changes were made during this follow-up.
