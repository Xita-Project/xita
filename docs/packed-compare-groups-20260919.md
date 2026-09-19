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

## perf.35 installed for hardware qualification

The candidate builds from perf.33 with the packed comparison header change;
owner reuse and completed-result bypass remain enabled. All other build options
are retained, including sampled capture detail. Package checks confirm only
`game-a.self` and `boot-game.txt` changed and the update contract is identical.
The remote updater verified runtime SHA256
`0e147decd401ac688ce8300a55bd6bfff9e94175a3327a8c0497d69357dbe73f`
(32,199,606 bytes), restarted into slot 0 and confirmed boot. Status reports
`0.2.0-perf.35 / fbcb29d`; perf.33 remains in slot 1 for rollback.
The guarded ordinary campaign/checkpoint/corridor sequence is running under
`packed-compare-hardware/run-gameplay.py`. No hardware timing improvement is
established by the installation. Build, package and deployment receipts remain
in that private directory.

## First settled checkpoint

The ordinary saved campaign checkpoint completed at the expected camera. Final
six 60-frame windows average 78.25 ms (about 12.78 FPS), compared with perf.33's
78.10 ms. Owner capture is 5.096 versus 5.274 ms/frame and worker preparation
4.739 versus 4.778 ms/frame, with slightly less captured data/model work. No
whole-frame improvement is established. The packed-comparison candidate remains
under investigation while its heavy-corridor capture completes. Private evidence:
`packed-compare-hardware/checkpoint-settled.log`, generated summaries and
`checkpoint-comparison.json`.

## Corridor capture and resolution attribution

The perf.35 corridor capture completed at 186.13 ms/frame, versus perf.33's
171.53 ms. Endpoint camera and model workload differ (28.74 versus 22.57 selected
model calls/frame), so this does not establish a causal regression. Owner capture
is 20.63 versus 18.41 ms with 1,383 versus 1,218 KiB/frame staged. There is no
confirmed hardware speedup. Private evidence: `packed-compare-hardware/corridor/`
and `corridor-comparison.json`.

To avoid another restart/route mismatch for attribution, the same perf.35 view
is now undergoing an ordinary settings-only native/360p/native comparison.
The graphics overlay was inspected with Render Resolution selected at Native.
One right press wrapped it to 360p; the runtime confirmed `640x360` and
`XV_RENDER_HEIGHT=360 applied` without fallback. No camera/locomotion input was
sent. `resolution/collect-and-restore.py` collects 90 seconds at 360p, restores
native in its cleanup path with an application-log check, then collects another
90 seconds. Timing and restoration remain pending completion; the initial
native log and all subsequent evidence are kept in `packed-compare-hardware/`
`resolution/`. This uses ordinary gameplay and the live graphics control, not
the built-in benchmark controller.

## Fixed-camera resolution result

All three ordinary-gameplay captures completed. Native resolution was restored
and confirmed by both `render resolution: 960x544 (native)` and
`XV_RENDER_HEIGHT=544 applied`; the final screenshot shows gameplay on perf.35.
The last six logged camera observations in each capture are identical:
(-26.89, 37.28, 0.62), direction (-0.96, -0.25, -0.15). Each timing summary uses
six complete 60-frame windows. Live NPC state is not deterministic.

| Metric | Native before | 360p | Native after |
| --- | ---: | ---: | ---: |
| Frame interval | 182.03 ms | 180.15 ms | 172.60 ms |
| Derived FPS | 5.49 | 5.55 | 5.79 |
| Selected model calls/frame | 31.54 | 33.13 | 33.73 |
| Owner capture | 21.81 ms | 22.13 ms | 22.07 ms |
| Worker preparation | 10.17 ms | 10.79 ms | 11.15 ms |
| Completion bound after submit | 109.61–112.97 ms | 69.67–71.45 ms | 116.47–119.33 ms |

Reducing pixel resolution substantially reduces outstanding completion latency
but does not produce a corresponding FPS improvement; the restored native arm
is faster than the 360p arm. This supports prioritizing CPU preparation and other
resolution-independent critical-path work for this scene. It does not prove all
GPU work is irrelevant, identify a cache miss, or negate prior resolution gains
in other views. Packet bounds include pipeline latency, not exclusive GPU time.

Keep native settings and the cumulative perf.35 candidate while investigating
the next CPU bottleneck. No whole-frame gain is claimed for packed comparison.
Next qualify the optional CPU-counter probe described in
[cpu-counter-followup-20260919.md](cpu-counter-followup-20260919.md) before using
cache events to choose a larger refactor. Private evidence includes the three
resolution logs/screenshots, `comparison.json`, `view-check.json`,
`apply-360.log`, `restore-native.log` and completed `trial.log`.
