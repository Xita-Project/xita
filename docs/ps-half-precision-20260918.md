# Half-precision combiner programs (candidate)

September 18, 2026. A GPU-side candidate motivated by ordinary Blood Gulch
gameplay on the installed cumulative build (`0.2.0-test.1 / ed9af19`, native
960×544, standard settings, GPU 222 MHz). No FPS gain is claimed here; the
hardware result section records what was measured.

## Why the GPU, not stream preparation

Per-60-frame `[xv/ui] frame time` windows from the acceptance log:

| View | Draws/frame | Game ms | FPS | Draw-HLE ms | `[frame-retire]` completion ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cliff wall, close | 42 | 49.5 | 20 | 2.2 | 46 |
| Cliff wall, shadow side | 46 | 53.9 | 18 | 2.7 | 50 |
| Red base | 66 | 60 | 16.4 | 4.4 | 60 |
| Red base, heavy | 192 | 66 | 15 | 14.2 | 78 |

In the 46-draw cliff view `[frame-query-boundary]` reports the world-scene
query notification 49.2 ms after submission with 1.2 ms of GPU work after it,
and `[depth-store]` shows one backbuffer scene per frame. Four BSP draws
covering the screen therefore cost about 48 ms of GPU time. All three CPU
cores sit at 20–30 %, the guest fiber runs about 29 % of one core, and
`[flare-defer]` shows it parked 23.5 ms per frame at the flare brightness
barrier waiting for that result. Stream preparation is 1.4 ms per frame in
light views and 6.7 ms in the heaviest; `[vertex-capture-reuse]` hits 100 %.
Removing the remaining CPU stream work cannot reach a 50 ms frame while the
GPU needs 46–78 ms.

## What changes

Generated fragment programs emulate NV2A register combiners in `float`.
`recompiler/pixelshader_recomp_gen.py` already had an experimental
`XV_PS_PRECISION=half` mode, but it only renamed types: Cg promotes any
expression that mixes a `half` operand with an unsuffixed literal back to
`float`, so most arithmetic stayed at full precision. The mode now lowers the
combiner body's types and literals together, keeps the mux parity test
(`fmod(floor(a * 255 + 0.5), 2)`) at float so its integer rounding is exact,
and leaves the alpha-test block and the final return untouched so
`tools/specialize_ps_alpha.py` and `specialize_ps_cutout.py` still match by
exact text. NV2A combiner arithmetic is 9-bit signed fixed point; half has an
11-bit significand over the same range. Texture coordinates remain float:
they come from the vertex program's varyings, which are not part of the body.

`tools/ps_pipeline.py` applies the requested precision per program and keeps
`ps_28CF808C_07` at full precision because `tools/test_depth_shader.py` pins
the exact bytes of its alpha-disabled variant for the depth-prepare path.

## Regeneration and compilation

From the frozen qualified inputs (`capture-reuse-20260918/build`), staged as
a copy of the release staging tree:

```sh
XV_PS_PRECISION=half python3 tools/ps_pipeline.py      # no new logs: same psdefs and pairs
python3 tools/specialize_ps_cutout.py
```

`xv_ps_table.h` is byte-identical before and after (same pairs, names and
keys). 1,158 fragment sources changed; the depth program and the four
hand-written fallbacks did not. The changed sources were compiled with the
console's own Cg compiler (`tools/shadercomp`, `libshacccg.suprx`) running
inside Vita3K, the same back end that produced the frozen programs: -O3, no
fast-math, no fast-precision. This is compiler hosting, not game validation.

Instruction counts read from the compiled `SceGxmProgram` headers:

| Program | Primary instructions float → half | Secondary |
| --- | ---: | ---: |
| `ps_154066FD_7F` (8 stages, 4 textures, environment) | 197 → 179 | 21 → 25 |
| `ps_154066FD_7F_na` | 100 → 90 | 23 → 25 |
| `ps_1667DAC2_3F` (mux) | 212 → 203 | 18 → 21 |
| `ps_0019A1BA_3F_t8` (cube-on-2D) | 111 → 113 | 8 → 8 |

Instruction counts are not cycle counts; USSE executes half arithmetic at a
higher rate than float, and texture sampling is unchanged. The counts do show
that most instructions remain: the saturate/clamp operations on every combiner
input and output are the next reduction candidates, and need range proofs
before removal.

## Runtime update

Fragment programs referenced by the table travel inside the executable and
take priority over the packaged files, so the change ships as a runtime-only
update: the existing package members stay byte-for-byte, `game-a.self` is
replaced and `boot-game.txt` regenerated with `package_vpk.update_record`.
The device's asset contract is unchanged. The current cumulative runtime
remains in the other slot for rollback.

## Hardware result

Runtime `b2e4333f917e522dc9f2d6f4ff92cbf39b055470a08e29ef94a6242f0ef94e48`
(`0.2.0-test.1 / e21e948`, 32,008,402 bytes) was uploaded as a runtime-only
update, booted from slot 1 and verified by `/status` and `/update`; the
qualified runtime stayed in slot 0. Only five objects differ from the
qualified build: the four version-stamped objects and `xv_shader.o`; the other
95 are byte-identical. `tools/test_depth_shader.py` passed against the new
embedded table.

The same Blood Gulch route (dashboard → main menu → split screen → Blood
Gulch, Slayer) rendered identically: menus, terrain, sky, HUD, weapon model,
plasma pistol charge and bolt, movement. No shader load errors appeared in
the log. Frame windows were not faster:

| Content | Float build | Half build |
| --- | ---: | ---: |
| Main menu, 7 draws (game ms / completion ms, medians) | 35.0 / 25.6 (n 197) | 34.1 / 24.3 (n 24) |
| 40–49 draws/frame (game / completion / query ms) | 49.8 / 46.7 / 45.3 (n 33) | 56.7 / 53.5 / 52.2 (n 8) |
| 60–69 draws/frame | 60.2 / 57.0 / 55.5 (n 30) | 58.3 / 55.1 / 53.6 (n 3) |
| 80–89 draws/frame | 63.8 / 62.0 / 54.5 (n 1) | 62.8 / 59.3 / 57.9 (n 12) |

The camera paths diverged because stick integration depends on frame rate, so
the gameplay classes compare similar rather than identical views; the menu
rows are fixed content. Within that limit the change is between −5 % and
+15 % with no consistent direction. The device was rolled back to slot 0 and
the qualified runtime `bd502b1d…` confirmed booted.

## Conclusion

Half-precision combiner arithmetic is not a lever on this compiler and GPU:
the compiled instruction count fell only 2.5 % overall and GPU completion did
not improve. Together with the September 14 result that 640×360 rendering
(59 ms) was no faster than today's 960×544 (60 ms) in comparable base views,
and the 25 ms completion latency of a seven-draw menu frame, the GPU-side
cost is not proportional to shaded pixels or to fragment instruction count.
The next attribution step is the existing `XV_GPU_PACKET_TIMING` diagnostic
in the same views, to separate GPU execution from submission and notification
observation delays, before any further shader work. The generator change is
retained on its branch because it is correct and default-off; the regenerated
programs are not adopted.
