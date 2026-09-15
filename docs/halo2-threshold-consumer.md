# Original clipped 160×120 threshold consumer

`THRESHOLD_RENDER=1` enables the exact original pass captured in native190.
It requires `COMPOSITION_RENDER=1` and defaults off. The earlier movie, screen,
BC1 and composition consumers retain exclusive ownership of active primitives;
an unsupported command never falls through to a different active renderer.
The dispatcher restores native FPSCR on every path. Rejection diagnostics run
only after all eligible consumers reject BEGIN.

Preparation pins the owned XBE and complete native190 channel, push-buffer and
texture captures. Its private 9016-byte version1 contract stores the complete
retained setup/validity map, seven program slots and four expanded vertices.
The consumer reads each original 24-word packet in order: v5, v4, v3, v2, v1,
then position. Every accepted word must match the captured contract. The unused
v6 is zero in staging and has no live fragment consumer. The contract supplies
no commands or replacement vertices.

Independent guards require the observed ARGB target, 160×120 clip, pitch640,
full RGBA mask, four 640×480 linear/clamp ARGB samplers and disabled blending,
depth/stencil, alpha testing, fog and stipple. Every BEGIN and END validates
all active resource spans through checked DMA descriptors. Read-only inputs
may overlap one another. The output must have write permission, pass its
attachment check and be disjoint from every input in both physical and returned
host address spaces. The inactive zeta slot is never read. Failed rendering,
truncated mappings, overflow and returned staging aliases preserve guest RAM.
Only successful END commits a completed independent 160×120 RGBA buffer.

GXM reuses the existing context, patcher and geometry buffers with a separate
160×120 target and tiled private depth/mask storage. It uploads actual guest
textures, consumes the actual vertex words, binds the tested original shaders,
then waits for completion. The oversized original 640×480 window quad remains
clipped, preserving its 4:1 texture-coordinate slope. Its four bilinear samples
and threshold arithmetic are validated in [the GPU probe](halo2-threshold-probe.md).
The captured input correctly produces black at this intermediate stage; bright
synthetic probes demonstrate actual nonzero filter output. This is no substitute
for the original menu or its presentation, and adds no flip call.

All 50 host executables pass, including complete-packet rejection, retained
state mutation, DMA permissions, physical and host aliases, inactive-depth
isolation, failed staging and exact guest-byte commit checks. The actual
five-consumer dispatcher is tested with bounded success/rejection oracles.
The consumer and dispatcher also pass ASan/UBSan; seven threshold and six
composition shader tests pass. With threshold rendering disabled, `quad_gxm.o`
is byte-identical to native190. The dispatcher object differs because its
rejection diagnostic moved; exclusive ownership and rejection remain tested.

The diagnostic package embeds owned game code, image and shader data and must
not be uploaded as a distributable release. All assets, generated code, private
contracts, packages and captured outputs remain outside Git. This work uses
only the isolated Halo 2 lab; CE, the shared emulator and physical Vita are
unchanged.

Native191 executes the original threshold END at `03B7CAC0` and commits 19,200
black RGBA pixels to `02B1B000`, matching the independent filter result. The
preceding composition produced 307,200 nonblack pixels (first `00776857`);
its changing BC1 input remains live. The next BEGIN is `03B7CE50`, targeting
`02B31800` at 160×120/pitch640. Four linear/clamp samplers now read the completed
threshold image at `02B1B000`. This three-stage averaging filter is unsupported
and rejects. PUT remains `03B80158`, EIP `003FAC58`. The next texture capture
contains exactly 76,800 zero bytes, proving the committed output is the next
original input. It is not invented fixture data.

The original Microsoft Game Studios intro was visually verified in this run.
Last presented frame135 remains black, with unchanged SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
**The original main menu has not appeared.** The normal Start input was sent
only after the original complete 59,670,016-byte map copy. The owned emulator
was stopped after the terminal capture; its X server remains available.

All 176 dependency targets were checked before launch. Native191 ELF SHA256:
`ea62fc35eee6d283d5e7f7b8cf21fe737b9e070591590091257098993399f5e2`;
EBOOT `986975a1a05444315ce58806c226b62543ce25a52d7606f2cd887bfc27189066`;
trace `ebee024bbe3f456ea7408a12c70f2ce9834e91864f95727dc8e109aee805f560`;
channel `ab4ae308a1c527b6cf20646181585849a11cd59aa869f04761040eefc1d8140a`;
push `af216587e0020f4adcd473c83836cbda855ddadf512e44bc3b0d24263c6372ac`;
next texture0 `19fd0888a56f31e71263408a2b7e81896a6e7c1f43c81227da968d9cfb09013a`.
Build, preparation and test records are private in `threshold-consumer/`;
matching captures are `native-191-artifacts/` and `native-191-view/`. The exact
archived package can be replayed from the private handoff directory:

```sh
python3 preserve_fresh_cache.py native191-replay
python3 capture_run.py 191-replay native-191-artifacts
python3 drive_startup.py 191-replay native-191-artifacts
```

The cache helper preserves the prior private cache and allows original game
formatting/copying. Next is the observed 160×120 averaging filter, including
its half-texel offsets, alpha gain and original input/output ownership.
