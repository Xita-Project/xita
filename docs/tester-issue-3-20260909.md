# Tester issue 3: campaign logs and shader capture

Reviewed September 9, 2026. [Issue 3](https://github.com/Xita-Project/xita/issues/3)
reports `xita-native-bounds-20260908.vpk` and attaches four logs. That is older than
the September 9 untraced gameplay release. The tester photos supplied separately
show the second level; their exact build is unknown.

## Evidence

The main log opens `a10.map` and then `a30.map`. Photos show the outdoor second
level with a nearly black sky in one view and flat gray sky in another, plus dark
HUD rectangles. This establishes a rendering report, not its cause or a regression
in the September 9 build. Later mission completion remains unverified.

All four logs together contain 709 complete render-time windows, with zero
ordinary `sceGxmFinish` calls and zero recorded busy-slot acquisition waits.
Parallel geometry jobs and a texture worker are active. This does not eliminate
GPU/API stalls or prove that additional CPU parallelism is safe.

Settings change during these recordings. The posted config is not sufficient to
label every sample: the main run changes from 544p to 480p, 400p and 360p. The
logged startup clock is CPU 444 MHz despite the requested 500 MHz. Tracing and
the 1 kHz sampler are enabled. Do not pool the sessions as a settings benchmark.

One late a30 window (`mesh 27420..27479`) records about 545 GXM calls/frame,
13.69 ms in submission, and a nearby caller-state preparation window records
139.45 ms/frame. Its frame report is 2.7 FPS with 189.0 ms/frame in draw HLE.
These nested/asynchronous metrics cannot simply be summed or treated as GPU
execution time. Map-loading profiles, including the large `AC320` sample, must
also be separated from gameplay before selecting native replacements.

## Confirmed diagnostic bug and correction

The main log has **12,103 `[psdef]` dumps for only 574 distinct hashes**; 11,529
are repeats. The first 512 entries are unique. `xd3d_ps_sync` remembers only 512
hashes, but previously continued dumping every unseen hash after the array filled.
Those hashes could never be remembered. Each dump also used 240 `snprintf` calls
to encode 240 bytes, inside the caller's state-preparation stage.

The correction stops diagnostic capture after 512 definitions, reports that limit
once, and directly encodes hex digits for the definitions that are captured.
Shader identity, program key, color constants and dirty-state processing remain
before the diagnostic limit. The limit does not cap the number of usable shaders.
Later definitions will not be dumped in that session; this is an explicit
bounded-capture tradeoff. The uploaded raw logs retain the additional definitions
for investigating the reported rendering bugs.

## Validation and deployment

The regression fails against the prior code. It then passes with shader caching
both enabled and disabled, exercising 18,000 state updates, identity/color
correctness after saturation, unchanged hex output, collisions, reset and split
guest pages. NV2A method tests and 2,292 C/Python identity checks also pass.

The CPU-preparation suite passes in the private native stage with its generated
shader table. Its clean-source aggregate initially fails against the placeholder
table's missing fields; the focused identity regression works in the clean
checkout. No unrelated placeholder-table change is included.

Native compilation and packaging pass. Only `eboot.bin` changes compared with the
September 9 gameplay VPK; all other package payloads match. This follow-up is
**built locally, not released or installed**. The current download remains the
September 9 gameplay release. Emulator gameplay and physical frame-time comparison
for this follow-up are pending; no FPS improvement is claimed.

Candidate VPK SHA-256: `0706baa4277a9eddd6059f90e560965c79d241026db4e60ffc009c5eebbef0bb`.

The immediate next hardware comparison should keep a30 scene/settings fixed and
check bounded `[psdef]` output, state-preparation time, draw-HLE time and frame
rate. [Wii reference review](wii-reference-20260909.md) identifies the subsequent
LOD, visibility and asset-preparation investigations.
