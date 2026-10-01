# Unused texture preparation hardware follow-up — September 7

Update: the [13:05 CPU off/on/off collection](hardware-20260907-cpu-comparison.md)
completes the missing comparison with 7.079 / 7.065 / 7.204 FPS at 544p. All
paths switch correctly; no FPS benefit is established. The resolution runs
below remain historical measurements with all three paths enabled.

The installed build completes Blood Gulch's **resolution** benchmark at
**9.604 / 13.166 / 9.711 FPS** for 544p / 360p / 544p. It skips **31.6% of
bound texture-stage preparations** in the measured view. All three recent CPU
optimizations execute throughout; **there is no CPU off/on/off comparison in
the collected logs**. Their combined FPS benefit remains unmeasured, and the
sustained 20 FPS target remains open.

## Collection and baseline

Read-only USB collection at 12:05–12:06 CDT copied and hash-verified 63 files,
487,219,483 bytes, into:
`/home/birchwoodgod/xita-backups/2026-09-07-120534-unused-texture-hardware`.

The 32,918,474-byte executable has SHA-256
`d97bffe770677b02c7436edb48f89ceabf1d8302ee239e81ab9be384d3b23b3e`,
matching the [11:33 installed candidate](unused-texture-preparation-20260907.md).
The configuration is byte-identical to its predeployment backup, SHA-256
`8faaa4d045f6a0d834adfcd44d3ac92cc509d0bd04af705d611a6595cf95711d`:
544p, texture limit 256, automatic filtering, mip smoothing On, High material,
glow and particles, a 20 FPS cap and CPU 444 MHz. Rear touch remains disabled.
Preserve these settings for the user's requested standard-settings baseline;
the benchmark's temporary resolution change restores 544p on completion.

No device files were changed. USB storage was safely unmounted at 12:07 CDT.
No September 7 screenshots were present. The current `xita.log` has one
completed resolution test and no CPU-test markers; no rotated log contains
a CPU comparison either. Historical shader errors in the older `xboxvita*`
logs are byte-identical to the predeployment backup, not new session errors.

## Resolution result

Each phase settles for 60 frames and measures 120 frames.

| Phase | Resolution | FPS | Mean frame time |
| --- | --- | ---: | ---: |
| Before | 960×544 | 9.604 | 104.12 ms |
| Reduced | 640×360 | 13.166 | 75.95 ms |
| After | 960×544 | 9.711 | 102.98 ms |

The pooled 544p result is 9.657 FPS. Reduced resolution improves throughput
by 36.3% and saves 27.60 ms/frame. The two 544p phases differ by 1.11%.
Position `51.1849 -80.6098 0.7514` and forward
`-0.55948 -0.82887 0.00160` match throughout; supporting reports show
123 draws/frame in every phase. The test completes and restores 544p.

The [preceding hardware test](hardware-20260907-clip-palette.md) used a different
camera and produced 316 draws/frame. The higher FPS here cannot be attributed
to the new build. Matching camera within this test supports its resolution
comparison, although simulation continues to run.

Supporting 60-frame reports and CPU samples have different boundaries from
the exact 120-frame FPS intervals:

| Supporting metric | 544p before | 360p | 544p after |
| --- | ---: | ---: | ---: |
| Draw preparation | 5.58 ms | 5.56 ms | 5.13 ms |
| Render submission | 3.57 ms | 3.65 ms | 3.56 ms |
| Final graphics-completion wait | 68.41 ms | 39.33 ms | 68.42 ms |
| Query wait per guest frame | 40.64 ms | 20.43 ms | 40.49 ms |
| Query completion-to-resume | 100 µs | 95 µs | 96 µs |
| Nearby C0 / C1 / C2 median busy | 4 / 10 / 51.5% | 5 / 14 / 63% | 3 / 7 / 55% |

Engine, render and query intervals overlap; do not add them. Final Finish
measures an elapsed CPU-side wait, not exclusive GPU execution. Its strong
response to resolution, alongside nearly unchanged submission/preparation,
supports a substantial graphics workload contribution in this view. Lower
core-2 utilization here is consistent with more waiting; it does not prove
better work distribution. Even at 360p, 75.95 ms exceeds the 50 ms budget.

## Path coverage and checks

- Every measured 60-frame report prepares 19,740 texture stages and skips
  9,120 unused stages: 329 prepared and 152 skipped per frame, a 31.6% work-count
  reduction. Across 38 world reports, 343,940 of 1,072,283 bound preparations
  are skipped (32.1%). These counts do not establish elapsed-time savings.
- Every measured report records 7,020 native clipping calls, all using integer
  locals, and 720 palette hash reuses with zero rehashes. World totals are
  248,740 clip calls and 26,968 palette reuses with one hash. None of the three
  paths is switched off in this run.
- Native matrix and quaternion paths cover 98.3% and 99.1% of their respective
  world calls. No texture decodes occur during the measured phases.
- All 2,177 world-bracketed visibility waits have ready timestamps. Average
  completion-to-resume is 108 µs, maximum 13.409 ms, with 159 fallback sleeps.
  Average notification latency alone does not explain the frame-budget gap.
- All 57 complete render reports have matching frame counts and stage sums,
  60 final Finish/display-queue calls and no intermediate target Finish calls.
  No explicit render-target/shader failure or workload-table overflow appears
  in the current log. Geometry sampling was off and no screenshots were
  collected, so this run does not establish visual correctness.

## Next measurement and work

The installed input handler distinguishes **L + R + Square** (three CPU
optimizations off/on/off at one resolution) from **L + R + Select** (resolution).
Square takes precedence if both chords are pressed together. There is no
dashboard benchmark entry. The later 12:35 collection and user clarification
below resolve the repeated resolution-test selection.

The CPU comparison should display **CPU** and name clipping, palette hash
cache and unused texture stages in its start log. It restores all configured
defaults on completion or cancellation. Use that same-build comparison before
accepting or reverting the bundle on performance grounds. Keep the saved
standard graphics settings fixed.

Continue investigating common color-writing GPU passes alongside CPU draw
preparation. Draw/index counts identify candidates, not GPU timing. This view
has more graphics waiting and fewer draws than the previous CPU-heavy view;
both workloads matter. Extra texture-decoding workers cannot remove decoding
work that is absent from the measured intervals.

Raw files, hashes, collection metadata and the general, benchmark and
optimization analysis scripts/results are retained in the archive. No code,
executable or device settings changed during this follow-up.

## 12:35 follow-up and confirmed controls

The next read-only collection is archived at
`/home/birchwoodgod/xita-backups/2026-09-07-123508-unused-texture-hardware`.
All 63 copied files (487,103,494 bytes) were hash-verified. The executable and
configuration match the identities above. No pad script, replay, recording or
`env.txt` is present in `data/xita`. USB was safely unmounted without writes.

This is another completed resolution test: **8.261 / 10.507 / 8.465 FPS** at
544p / 360p / 544p. Position `92.4877 -149.3792 1.1385` and forward
`-0.69289 0.71871 0.05836` match across phases, with 193 draws/frame in each
supporting report. The pooled 544p result is 8.362 FPS; 360p saves 24.42 ms/frame
and improves throughput by 25.7%. The two 544p phases drift by 2.47%. This camera
differs from the earlier 123-draw view, so FPS cannot be compared between runs
as an optimization result.

All three CPU paths remain enabled. Each measured 60-frame report records
33,660 integer-local clipping calls, 1,140 palette reuses with zero hashes, and
33,960 texture stages prepared / 11,940 skipped (26.0%). There are no measured
texture decodes. Supporting graphics-completion waiting is 56.86 / 34.21 /
56.96 ms; draw preparation is 10.46 / 9.63 / 8.26 ms. These elapsed intervals
overlap other work and have different boundaries from the exact FPS phases.
All 32 complete render reports have consistent sums, 60 final Finish/queue
calls and zero intermediate target Finish calls. No explicit render-target or
shader failure is found; geometry diagnostics remain off.

The user confirmed using **L + R + Select**, then acknowledged missing Square
and is repeating with **L + R + Square**. This explains the recorded test type;
there is no evidence here of a benchmark-dispatch bug. No control or executable
change is needed. The next test should show **CPU**, hold resolution fixed,
switch the three CPU optimizations off/on/off and restore defaults afterward.

After collection, report the measured CPU frame-time difference and choose
the next change from that result. If the gain is small, prioritize investigation
of the common rendering passes behind the graphics waits. End each hardware
report with the next development step and whether another user test is needed,
as explicitly requested by the user.
