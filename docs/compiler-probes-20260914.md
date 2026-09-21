# Compiler and gameplay replay probes — September 14

Two private compiler experiments preserved the tested results but did not establish
a useful hardware speedup. Neither changes the ordinary build configuration.

## Clipping compilation

A private build compiled only `xk_clip.c` with `-O3 -funroll-loops`, retaining
`-ffp-contract=off`. Two linked ARM entry points passed 3,584 finite and 3,808
exceptional-input comparisons across seven native floating-point modes. Tests
compare the complete guest context, 2 MiB arena and native FPSCR byte for byte.
They include overlapping buffers, unaligned accesses and split guest pages.

`tools/test_arm_math_runtime.py` now supports clipping inputs with `--float-edges`
and `--random-floats`. Vertex, plane and epsilon inputs are stressed separately
and together; control fields remain valid. The recorded clipping experiment used
the finite and edge modes. Random mode is available but is not claimed as tested
for this candidate.

The finite fixtures reduced counted ARM instructions by approximately 0.026%;
the edge fixtures by 0.014%. Three captured gameplay clipping calls changed from
14,984 / 44,602 / 14,984 instructions to 14,979 / 44,624 / 14,979. This is too small
and mixed to justify a default compiler change. Instruction counts are not cycles.

## Guest link-time optimization

A separate build used `-flto=4 -fno-fat-lto-objects` on generated guest units and
`-flto=4` on the game link only. Existing optimization and floating-point rules
were retained. The immutable updater helper was built with its ordinary flags.
The final package preserves its contract and changes only the game executable
and boot manifest.

The final game ELF is
`70bda6abb9a04c3f50314852a7f0a232e6d5373f2d24a8b9424ffe24e8ab1d66`.
It passed 5,376 linked ARM math comparisons and nine private gameplay replays.
Executable text decreased from 25,660,976 to 25,217,268 bytes, approximately 1.7%.
Three captured object-pose calls used 0.4–1.3% fewer counted instructions; the
captured clipping callers changed by less than 0.2%. Optional NEON and empty-object
scan code was also excluded from this private build, so its instruction totals
are not an isolated measurement of LTO alone. No hardware FPS benefit is claimed.

An isolated Vita3K run booted the final package through the updater, opened the
normal main menu and profile/level/difficulty screens, and entered the Pillar of
Autumn cryo sequence. This is a startup smoke test, not complete gameplay
validation or a physical performance result. The emulator was subsequently
restored to the ordinary compiler configuration for query-overlap tests.

An earlier package unintentionally applied LTO to the updater helper. Contract
verification rejected it before deployment. A fresh link with game-only flags
produced the final compatible package and the same validated game ELF.

## Private replay evidence

An isolated emulator capture build recorded three calls each from object-pose,
clipping and an additional gameplay routine. Each capture includes the full guest
arena, page table, context and native FPSCR. The replay executes actual linked ARM
code, models explicit firmware copies/environment queries, and rejects unexpected
firmware calls or scheduling escapes. Candidate context, arena and FPSCR must
match the baseline. These are sampled paths, not a proof for every game state.

The three pose samples wrote separate object pages plus the guest stack. That is
useful evidence for a future worker boundary, but does not establish that all
objects, parent dependencies or animation callbacks can execute concurrently.

Captures, executables, logs and replay prototypes remain outside Git under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z`.


## September 20: scoped rendering code-size experiment

`XV_RENDER_GUEST_SIZE=1` is an optional Halo CE 3925 build setting. Add it to
an otherwise unchanged `RECOMP=1` build command. The default is `0`.
It selects generated units containing functions `70110` and `7E530` and compiles
those entire units with `-Os` instead of `-O2`. Other functions in the selected
units also change compilation; this is not a function-only optimization.
Native HLE/math flags and guest code generation remain unchanged.

The build finds function definitions rather than assuming shard numbers, rejects
missing/duplicate definitions, and rebuilds the selected objects when the option
changes in either direction. Changing unrelated compiler flags still requires
the usual build hygiene; this stamp tracks only this option.

On the retained perf77 build, the two units shrink from 2,693,036 to 1,898,824
bytes of object text (29.5%). Function `70110` shrinks from 30,450 to 19,672 bytes,
but its static call sites increase from 268 to 627. Those counts are not dynamic
instruction counts or frame-time estimates. Reduced instruction footprint may
help, while extra helper calls may hurt. Hardware results are pending; do not
consider this a proven optimization or enable it by default on size evidence.
