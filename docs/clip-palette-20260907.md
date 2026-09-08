# Clipping, palette preparation and CPU comparison — September 7

The [hardware follow-up](hardware-20260907-overnight-performance.md) still measures
101 ms/frame at 360p. This pass reduces repeated CPU bookkeeping in the clipper
and texture preparation, and adds a controlled comparison of these two changes.
[Hardware follow-up](hardware-20260907-clip-palette.md) measures 6.315 / 7.559 /
6.327 FPS at 544p / 360p / 544p. Both new paths execute, but that session ran
the resolution test; the CPU off/on/off comparison and its speedup remain
unmeasured. Its camera and draw counts differ from the preceding build's test.

## Changes

- The exact Halo 3925 polygon clipper now keeps its eight integer registers in
  native locals alongside the existing floating-point locals. It publishes and
  reloads the guest context around the stack probe, string operations and
  cooperative preemption, and publishes the result at return. Integer flags,
  guest memory access order, floating-point rounding and aliases are preserved.
  The generator rejects unclassified context-taking helpers. The preceding
  FP-only implementation remains available for comparison and rollback.
- P8 palette bindings reuse the FNV hash only after comparing every one of the
  palette's 1,024 bytes. This replaces a dependent multiply chain with a full
  content comparison. Four slots occupy approximately 4 KiB. CPU writes within
  the same frame, different addresses, physical aliases and reused storage are
  still checked; the decoder and texture cache continue receiving the same hash.
- A new CPU benchmark switches only these two changes **off → on → off**, at the
  current resolution. It uses the existing 60 settling / 120 measured frames per
  phase, neutral input and camera checks. Completion, cancellation or loss of a
  valid gameplay view removes temporary overrides and restores configured
  defaults. No configuration or save files are written by the benchmark.

The switches default on. `XV_CLIP_REGISTERS=0` selects the preceding FP-only
clipper; `XV_PALETTE_HASH_CACHE=0` recomputes each palette hash. The CPU comparison
temporarily overrides these preferences and restores them afterward. The older
`XV_NATIVE_CLIP=0` still disables the native clipper altogether.

`[clip-registers]` and `[palette-cache]` logs show how often each path runs. These
are work counts, not saved milliseconds or GPU timings. AI, physics, GPU passes
and CPU affinities are unchanged by this pass.

## Hardware comparison

Launch Blood Gulch solo, stand still in a normal first-person view with menus
closed, and press **L + R + Square**. Leave controls alone until the **CPU** panel
disappears; allow up to two minutes. The same chord cancels. The existing
**L + R + Select** resolution test is unchanged, and **L + R + Triangle** still
provides the multiplayer start shortcut.

This compares the combined effect of the two new CPU changes. It does not
attribute savings to either one individually or freeze scene simulation. A
result at the frame cap cannot measure performance above that cap.

## Validation

- Normal and ASan/UBSan runs each pass four sets of 12,000 differential cases,
  covering both register defaults, disabled native clipping, runtime overrides,
  all eight x87 TOP values, four host rounding modes, full CPU context and 2 MiB
  of guest memory. Each set observes 3,843 preemptions, including changes to
  integer output addresses and floating-point state at the preemption boundary.
  NaN payload variation is permitted only in floating-point output words.
- Texture fixtures verify every palette word, colliding cache slots, unchanged
  uploads, same-frame color changes, restored palette contents and physical
  aliases. Both cache preferences and runtime overrides pass. Existing texture
  worker, mip, cube and compression checks pass; the palette fixture also passes
  ASan/UBSan.
- Production input and benchmark fixtures check fixed-resolution off/on/off
  timing, warmup exclusion, held chords, cancellation, invalid views and default
  restoration. Existing resolution tests and the multiplayer shortcut pass.
- The isolated native build succeeds. Its generated game code and profiling
  markers are preserved. Native, compressed and same-size USB executables have
  matching decoded SELF segments.
- The exact USB executable starts Blood Gulch through the normal solo lobby.
  Movement, firing and camera turns render in captured views. The CPU comparison
  reports 19.998 / 20.011 / 20.013 FPS and the resolution comparison reports
  20.005 / 19.996 / 20.003 FPS. These are capped software-emulator results and
  establish behavior, not Vita FPS gains.
- CPU comparison logs confirm zero register-local calls and palette reuse in
  the off phases. Each supporting on-phase window records 34,800 register-local
  clip calls and 1,020 reused palette hashes. Both benchmark cancellations restore
  544p. Initial harness checks used a fixed one-second delay before reading
  buffered logs; subsequent reads verify the actual cancellation/restoration
  markers despite interleaved thread logs.
- The a10 opening renders and Cross skips to the first-person cryo bay, with
  camera control 0 and the first-person director. Nine requested geometry
  samples across Blood Gulch, the cinematic and the cryo bay record zero
  changes before GPU completion. All 251 capacity reports show zero drops;
  250 complete render reports have consistent stage sums and preserve final
  completion and display-queue calls. The private emulator and Xvfb are stopped,
  and the private configuration is restored. Existing emulator diagnostics remain;
  these are bounded regression checks, not a complete campaign validation.

The host clip microbenchmarks do not establish a clear material gain from the
integer-local change over the prior FP-only version. The hardware A/B is the
acceptance check for performance. Common color-writing GPU passes and the rest
of the translated engine still require optimization.

## Candidate

Archive:
`/home/birchwoodgod/xita-backups/2026-09-07-080723-clip-registers`.

The installed 32,918,474-byte USB executable has SHA-256
`bad95dd9dbd3bc92de3db59e7a7edecbc677fc9acab19fb229b36fcebde0b591`.
The archive retains 5,105 source files, executable variants, host tests,
emulator captures and the previous device executable/settings/saves backup.
Installed at 08:34 CDT using the executable's existing allocation. Direct reads
and a fresh read-only mount verify the new executable and 657 other files
unchanged, including settings and saves. USB storage was safely unmounted at
08:36 CDT. `deployment.json` and `validation/final-result.json` record the checks.
