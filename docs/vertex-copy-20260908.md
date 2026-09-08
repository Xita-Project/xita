# Fused vertex snapshot copies — September 8

This separate opt-in experiment keeps the existing cached comparison mirror,
uncached GPU allocation and frame-slot retirement. It changes only how new
snapshot bytes are copied. **It is not installed on the Vita.**

## Implementation

The baseline copies guest bytes to the CPU mirror, then copies the mirror to
GPU memory. `xv_snapshot_copy.h` instead loads a chunk once with ARMv7 NEON and
writes both destinations from those values. Short tails stay inside the requested
ranges. Host/non-NEON builds retain the two-copy implementation.

For an N-byte new snapshot, this removes N bytes of explicit CPU mirror reads;
both N-byte destination writes remain. It does not remove either allocation,
perform vertex math, reuse stale data or grant the GPU access to live guest memory.
Actual cache/bus traffic and time savings are unmeasured on Vita.

The caller still acquires a retired slot, validates all same-frame reuse with
an exact comparison, appends a new version when source bytes change, initializes
padding, and runs the existing publication barrier. The source, mirror and GPU
ranges are disjoint. No GXM calls or new worker threads are introduced.

`XV_VERTEX_COPY_NEON=1` enables the new copy path. Default: **off** until a
hardware comparison. `vertex-copy` log rows count actual fused copies/bytes.

`XV_BENCHMARK_VERTEX_COPY=1` selects two-copy/fused/two-copy on **L + R + Square**.
It takes precedence over `XV_BENCHMARK_DRAW_SCAN` if both are set. The experiment
holds scans, visibility scheduling, residency, vertex comparison, resolution,
queue mode and shaders fixed. The configured copy policy is restored on
completion, cancellation or loss of the gameplay view. This selector is for
development configuration; it is not a new dashboard graphics control.

## Validation

- Native Vita build passes in an isolated stage based on the installed September
  8 source plus the opt-in draw-scan implementation. Scans remain off during
  the copy comparison.
- The actual allocator/uploader passes host and ASan/UBSan tests for same-frame
  source mutations, retained older frames, padding/size changes, mapping/allocation
  failures and **2,000 mixed frame-slot generations**, alternating copy modes.
- **1,612 Vita-compiled snapshot cases** pass Cortex-A9 emulation with exact
  read/write bounds, distinct initial destination bytes, all source alignments,
  unaligned destinations, vector tails, large ranges and unmapped end pages.
  Both destinations must equal the original bytes; reads from either destination
  are rejected for this vector routine. No game data is required.
- Existing index/constant ARM checks also pass. `sceClibMemcpy` is modeled for
  their baseline path; this is not a firmware or GPU benchmark.
- Frame acquisition/completion and benchmark tests pass for default visibility,
  scan selection, copy selection, selector precedence, restoration and ticket wrap.

The private Blood Gulch comparison completes at the emulator's 20 FPS cap:
**19.966 / 19.971 / 19.961 FPS**, with 60 settling and 120 measured frames per
phase and a matching camera. This validates switching; it establishes no
physical-Vita speed gain. The campaign cryo comparison completes at
**19.664 / 19.543 / 19.250 FPS**, also with matching camera checks; that variation
does not establish a benefit.

Gameplay checks cover ordinary solo launch, camera turns, firing, movement,
flashlight input, grenade self-death and respawn, Pause / Leave Game, and the
campaign opening followed by skip into cryo gameplay. Eight ownership samples
cover **1,081 draws** with zero geometry-byte changes before GPU completion.
The game log records no upload failures, constant rejections, fence errors,
draw-storage drops or normal Finish calls. Peak upload use is 1,983/8,192 KiB
per slot; maximum pending GPU packets is one. The opening cinematic reaches
1,291 GXM draws in a frame. The emulator is stopped and its previous executable
and configuration are restored.

The Vita3K backend console does report fragment/vertex varying-link errors.
These also occur in the earlier baseline, independent of these CPU candidates.
Game-side GXM success checks do not establish that every emulator pass rendered;
this [separate shader issue](shader-varyings-20260908.md) limits visual and
timing conclusions from these runs.

## Next hardware decision

Collect the installed visibility comparison first. Test this copy path separately
at unchanged graphics settings, leaving `XV_VERTEX_RESIDENT` and the scan policy
fixed. Compare stream-preparation cost, total frame duration and crash behavior.
The whole stream-preparation interval was about 3.93 ms/frame in the previous
hardware session; the copying addressed here is only part of it. Do not infer
a 20 FPS outcome from the reduction in reads.

Private archive: `xita-backups/2026-09-08-065547-skate3-vita-optimization/`.
Source: `vertex-copy-stage/`; ARM evidence: `arm-vertex-copy/`;
emulator evidence: `vertex-copy-run/`. This stage and its source manifest remain
separate from the installed candidate.

Native SELF: 34,873,222 bytes, SHA-256:

```
32dc974a29183974eba85cb8669154ea653d6b1221b1be9eaad1c189090e53f2
```

No Vita write, new VPK, commit or GitHub push was made for this experiment.
