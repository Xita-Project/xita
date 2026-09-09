# Native visibility helper: first hardware result, September 8

The primary Vita's installed application now matches
`xita-native-bounds-20260908.vpk`: all **1,585** package payloads compare
byte-for-byte, including shaders. The two additional app files are VitaShell
installation metadata. Executable SHA-256:
`5e0ad8f8eb54187c5fe763897edcd179f7efa7204eacde1ad92ef689bac400cf`.
This confirms the earlier pending VitaShell installation completed.

Collection was read-only. The application, logs, settings, saves excluding
derived map caches, crash files, and selected screenshots were backed up and
verified before unmounting. A follow-up at the user's request copied all 320
screenshots, verified every copy, and unmounted again. No Vita file was changed.
The standalone ad hoc diagnostic bubble is not installed on this device yet;
its VPK remains staged. Two-console multiplayer remains unverified.

## Controlled result

The benchmark ran in Blood Gulch with a stationary first-person view. Its
original/native/original phases each settle for 60 frames, then measure 120.
All phases report the same camera position and direction, `view-ok 1`, and the
final result reports `comparable-view 1` followed by successful restoration.

| Phase | Measured frames | Elapsed microseconds | FPS |
| --- | ---: | ---: | ---: |
| Original before | 120 | 12,642,225 | 9.492 |
| Native helper | 120 | 12,832,796 | 9.351 |
| Original after | 120 | 12,569,631 | 9.547 |

Combining the two original windows by elapsed time gives 9.519 FPS. The native
phase is 1.77% below that value. One short sequence is insufficient to establish
a repeatable small regression; it provides **no evidence of a speedup**. Keep
`XV_NATIVE_BOUNDS=0`. Do not use the earlier synthetic ARM instruction reduction
as a hardware performance claim.

The measured sequence used 640×360, texture cap 128, linear filtering, mip
smoothing on, reduced glow/materials, particles off, a 15-second/32-decal cap,
single-flight submission, and a 20 FPS cap. Requested CPU 500 MHz was rejected;
the effective CPU clock was 444 MHz. No graphics setting changed during the
three benchmark phases. Changes through the graphics overlay occurred afterward.
The final saved configuration therefore does not describe the benchmark: it
contains texture cap 256, triple buffering on and original effect settings.

## Is the benchmark operating?

- The switch reaches the native helper, not just its label. During each of the
  two reported 60-frame windows in the enabled measurement, the native helper
  records **8,940 calls**. Both original measurement phases report zero calls.
- `xv_benchmark_present` advances once through the renderer's Present hook.
  `sceKernelGetProcessTimeWide` supplies elapsed microseconds, and FPS is
  `120 * 1,000,000 / elapsed_us`. Phase switching drains happen before settling;
  the measured interval starts after 60 settling frames.
- The regular frame-time logger follows a separate accumulation path, using the
  same underlying monotonic clock. Its windows report 9.4–9.5 FPS before,
  9.4 FPS during, and 9.6 FPS after. Their frame boundaries are not identical
  to the benchmark boundaries; the agreement is a useful cross-check, not an
  independent clock calibration.
- Existing synthetic benchmark tests exercise known timings, changed frame
  rates, switch order, movement rejection, cancellation and restoration. No
  benchmark implementation was changed for this result.

This measures game Present throughput in a live scene, not the isolated helper's
CPU cycles or display scanout. The camera guard does not freeze animation,
particles or other game simulation. Small differences need repeated controlled
sequences and more than one view. The 20 FPS cap does not conceal a gain at the
observed ~9.5 FPS, but would limit interpretation of phases reaching the cap.

## Remaining work

The later free-play log includes live filter, resolution, frame-cap and triple
buffering changes, so it must not be aggregated as a controlled optimization
comparison. One representative heavy preparation window (mesh 2940–2999)
records 17.334 ms of draw preparation, including 6.343 ms of stream preparation,
against about 169.9 ms per whole frame. Pump submission is 5.156 ms and no normal
full-GPU finish calls occur in that window. These overlapping measurements are
not additive and do not identify all of the remaining frame cost.

The next local work is reducing redundant vertex snapshot comparisons in costly
draws, then testing against the original path. It must retain every vertex the
GPU actually reads and preserve ownership across in-flight frames. The earlier
campaign logs expose much larger stream-preparation windows than this Blood
Gulch run; the candidate still needs correctness checks before hardware delivery.
This is one portion of the work needed for stable 20 FPS, not a predicted cure.

## Evidence

Private collection: `2026-09-08-212601-native-bounds-hardware` under the external
Xita backup directory. It contains `installed.json`, `manifest.json`,
`analysis.json`, `benchmark-crosscheck.json`, and
`all-screenshots-manifest.json`. Raw logs, saves and crash dumps stay outside Git.
New gameplay log SHA-256:
`14449c6deada01ba444831530105430ae62d84e502d47a3521cca7763ebe8ef8`.
