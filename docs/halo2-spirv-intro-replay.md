# Halo 2: controlled SPIR-V intro replay

Native145 visibly renders the original blue Microsoft intro animation in the
isolated Vita3K lab. Normal Start then reaches the original menu sound setup
and stops at the unsupported `SetEffectData` entry `37B60D`, caller `191294`.
The main menu is not displayed. No guest shader, draw input, executable control
flow or sound result was changed for this rendering experiment.

## Controls and scope

The exact archived native106 package, which previously displayed the intro,
now produced black output on the current OpenGL/GLSL lab path. The generated
movie input was nonblack but GXM output remained black. Recompiling its GLSL
cache and focusing its own SDL window did not repair the output. The archived
synthetic quad probe reproduced this independently: its quadrant and hash
textures each had 307,200 pixel mismatches, while the reverse-winding case
preserved the black target. The same probe with fresh SPIR-V shaders passed
all three cases with zero mismatches. The probe's later utility-exit/relaunch
crash is preserved separately from those completed render/readback results.

Merely enabling `spirv-shader` was insufficient in native144 because a previous
GLSL program was preloaded. In pinned Vita3K `496939b6`, `pre_compile_program`
loads cached GLSL, while normal compilation returns a cached program before
checking the requested shader path. This explains why that particular replay
did not exercise SPIR-V. The fresh path selects SPIR-V in
`get_or_compile_shader`. See the pinned [Vita3K implementation](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/gl/compile_program.cpp).

Native145 preserved and moved only this private title's shader cache, disabled
shader-cache preload, and used `spirv-shader: true` with OpenGL. Its emulator
log records new fragment and vertex `.spv.txt` generation, with no GLSL program
preload. Vita3K binary SHA-256 is
`038bc9d488f5e3484e0d12cb264067955625ad4a688fc0b4287c00895c7a4d6f`;
the renderer is Mesa 26.1.7 llvmpipe, LLVM 22.1.8. This is a bounded lab
workaround, not a diagnosis of every GLSL path or a hardware rendering test.
Neither the shared emulator binary nor Halo CE configuration was modified.

## Observed original output

Draw 60 has 307,200 nonblack input and output pixels. The private first input
and output snapshots are byte-identical, including their headers, SHA-256
`fa48be270c689ce661bf3bbc89ca47c6a531420d0a5750a1d88aaf5a0d073491`.
Draw 120 has 290,331 nonblack pixels on both sides; draws 180 and 240 each
have 307,200. The actual window capture at 22:27 UTC shows the original blue
intro animation, with Vita3K's GUI overlaid. It is not a clean full-screen
capture or a substitute main menu. Capture SHA-256 is
`8347bbe1ee9d0210e260988e0e51053661dfe6fac72322b712fc117938bdc7ef`.

The original map copy completed at 59,670,016 bytes before normal Start.
After movie cleanup, the final presented frame 286 is black. The sound setter
remains strict; the read-only probe confirms the exact boundary documented in
[the effect-write audit](halo2-effect-write-boundary.md).

## Private replay and next task

All paths below are relative to the existing private Halo 2 directory beside
this source worktree. Preserve a populated `cache4` directory and raw file
with `preserve_fresh_cache.py` before replay: the current raw-volume bridge
does not support coherently reusing that populated cache. An empty private
cache allows the original formatter and map copy to run; it does not supply
fabricated map data or success results. Use a unique label for every replay.

The saved `native145-config.yml` selects OpenGL, SPIR-V and no shader preload;
it is also the current own-lab configuration. The previous title cache and
configuration are retained under `native145-prior-shader-cache` and
`native145-config-before.yml`. With no own emulator running:

```sh
python3 preserve_fresh_cache.py native146
python3 run_lab.py 146 native-145-artifacts/halo2-boot.vpk
python3 focus_game.py
```

Wait for the original movie output and completed map copy, then use the normal
input path (the script only presses/release Enter, mapped to Start):

```sh
H2_START_SECONDS=8 python3 movie-skip-audit/send_start.py 146-focused-long
python3 run_lab.py stop
python3 archive_native.py 146 native-145-artifacts
```

Allow the original setter stop and terminal snapshots to complete before the
last two commands. Evidence includes `native-145-artifacts`,
`native-145-view/window-live-2227.png`, `native-milestone-145.json`,
`quad-shaders/current-control`, and `quad-shaders/spirv-control`. The exact
ELF/EBOOT/VPK hashes are in the linked effect-write audit and match native144.
All 170 build dependency targets were validated; all 44 host executables pass.
The owned emulator has exited, with no native build left running.

Next implement only the audited immediate effect-data write: preserve both
image-shadow and live GP state under the mixer lock, test actual DSP
consumption and rejected cases, then replay the original menu startup.
There is no setter bypass in this checkpoint. Packages embed owned game code
and assets and must not be uploaded or distributed. All generated code,
packages, traces, images and owned coefficients remain private and outside Git.
