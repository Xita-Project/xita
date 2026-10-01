# Proven opaque material specialization — September 7

Implemented, locally validated and hardware-tested. The first
[hardware comparison](hardware-20260907-opaque-material.md) records
**6.730 / 7.079 / 6.877 FPS off/on/off**, a 4.06% gain against pooled off
throughput in that view. The graphics wait falls by 5.71 ms while on; this is
one promising run, not an established all-scene speedup. The previous
[CPU comparison](hardware-20260907-cpu-comparison.md)
found no measurable gain for its three-switch bundle; this candidate tests a
different opportunity while preserving the user's standard graphics settings.

## Implementation

The common `154066FD` material family uses tex0 alpha as its output alpha.
When the actual uploaded texture is entirely opaque and the captured alpha
function/reference accepts alpha 1, recorded draws now select the existing
alpha-disabled fragment program. Color math, blend/depth state, draw order,
resolution and effect quality remain unchanged. No shader assets were changed.

`xv_texture_alpha.h` classifies BC1/BC2/BC3 blocks and decoded RGBA uploads,
including all uploaded mip levels. Unchanged texture binds reuse the result.
Cutouts, unknown resources, cubes, render-target aliases, previous-frame
substitutions and unsupported border addressing retain alpha testing. Shader
overrides and disabled alpha specialization disable this optimization too.

A draw that relies on opacity pins its upload. If the source changes, a new
upload preserves the pixels referenced by earlier recorded commands. Existing
drained texture-pool purges reclaim retained versions. Allocation failure
preserves the old upload and follows the existing purge/fallback path.
Frequently changing opaque textures can therefore consume more pool space;
monitor that tradeoff on hardware. The initial family restriction is deliberate:
all six program identities and their six alpha-disabled source variants have
verified tex0 alpha dependence. Other families require separate proofs.

## Comparison controls

**L + R + Square** now starts a **GPU** panel and logs `[material-test]`.
It compares only this optimization: **off / on / off**, at the current
resolution, with 60 settling and 120 measured frames per phase. The earlier
clipping, palette-cache and unused-texture-preparation defaults stay enabled.
Completion, cancellation and loss of the gameplay view restore the configured
default. `XV_OPAQUE_MATERIAL=0` disables the normal default for development;
no saved graphics setting was added.

**L + R + Select** still runs the separate resolution comparison. Earlier
`[cpu-test]` results do not measure this candidate.

## Validation

- 626,336 BC block cases against independently decoded alpha, all 4,096
  captured alpha predicates, mip tails and RGBA row padding.
- 131,072 production draw recordings in each of three configurations. Command
  contents match the original path except the opacity flag. Cases include
  cutouts, missing/cube/feedback textures, borders and shader overrides.
- Upload-cache tests cover changed content, preserved recorded descriptors,
  unchanged binds, cube rejection and allocation failure, across compressed
  format/mip configurations. Existing texture, shader, input, completion and
  render-profile checks pass.
- Ten ASan/UBSan runs pass with leak detection. Global instrumentation is
  disabled for the isolated-source section-linking harness.
- Native `make RECOMP=1 -j6` passes in the isolated staging tree. All 5,110
  archived source hashes match; generated recompilation units and the special
  runtime-header timestamp are preserved. Native, compressed and padded
  executables have identical decoded segments.

The isolated software-OpenGL Vita3K run used the exact hardware configuration:
544p, textures 256, automatic filtering, mip smoothing on, High effects,
20 FPS cap, CPU 444 MHz and rear touch disabled. Solo multiplayer starts through
the normal lobby. Blood Gulch movement, firing, flashlight input and menu exit
were exercised. Campaign's opening ship/sky render; skipping returns to the
player camera, right-stick input changes its direction, and the cryo room,
NPC and observation windows remain visible. This is not a full campaign or
active-camo pickup playthrough; armed campaign flashlight behavior was not
retested in this pass.

In the fixed Blood Gulch view, each complete 60-frame report shows:

| Counter | Off | On | Off again |
| --- | ---: | ---: | ---: |
| Eligible material draws | 4,560 | 4,560 | 4,560 |
| Draws with captured opacity proof | 0 | 3,240 | 0 |
| Material draws | 4,620 | 4,620 | 4,620 |
| Material indices | 1,050,360 | 1,050,360 | 1,050,360 |
| Material draws using alpha-disabled program | 60 | 3,300 | 60 |

That is **54 additional draws/frame without alpha testing** in this emulator
view. Earlier CPU counters remain identical across phases. The camera is
unchanged, and completion/cancellation restore defaults. Screenshots show no
visible regression; a 323×151-pixel static wall region is pixel-identical in
all three captures. Other animated regions are not claimed pixel-identical.

Nine geometry samples report zero changes before GPU completion. All 322
complete render-stage reports have consistent elapsed-time sums, required final
completion/queue calls and no intermediate target Finish calls. No automatic
draw-shortage or app shader/target failures were logged. Optional capacity
headroom sampling was disabled. Vita3K still emits varying-link errors also
present in the previous validated build. The private emulator required SIGKILL
after its normal shutdown timed out; gameplay checks completed beforehand.
The private emulator/Xvfb are stopped and its original configuration restored.

Emulator off/on/off throughput was 19.954 / 19.990 / 19.984 FPS under a 20 FPS
cap, with geometry diagnostics inside every phase. **This does not establish a
Vita performance benefit.** Removing alpha tests might not save enough GPU time
to improve FPS.

## Build and hardware follow-up

Archive: `/home/birchwoodgod/xita-backups/2026-09-07-134016-opaque-material`.
Padded executable: 32,918,474 bytes, SHA-256
`e2f70270c1bf472149dda554f91b33963737d59bca392cbc2bf22d0ecc398258`.
Installed over USB at 14:14 CDT. The existing executable allocation was
overwritten at exactly the same size, then verified with direct I/O and a fresh
read-only remount. All 657 other tracked files, including settings and saves,
are unchanged. USB is safely unmounted; no VPK reinstall is required.
The standard configuration remains byte-identical, SHA-256
`8faaa4d045f6a0d834adfcd44d3ac92cc509d0bd04af705d611a6595cf95711d`.
The archive contains the source snapshot, validation evidence, prior executable,
backup manifest and `deployment.json` verification record.

The first hardware test is complete. It removes alpha testing from 32 additional
draws/frame with fixed camera, settings and draw totals. The measured pool stays
at 132 textures / 8,489 KB with no reuploads or purges. Defaults restore correctly;
no device writes were made during collection and USB is safely unmounted.

Keep standard settings fixed; no repeat of the same test is needed now. Next
inspect the remaining 26 alpha-tested material draws for a conservative
uploaded-alpha-range proof against the captured cutoff. Preserve cutouts and
all existing mip, sampler and upload-lifetime guarantees. The linked hardware
report records limitations and the next shader family's different alpha
dependency. The stable 20 FPS target remains open.
