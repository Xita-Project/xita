# Original 640×480 composition consumer

`COMPOSITION_RENDER=1` enables only the composition pass captured after the
original screen-effect and 320×240 BC1 draws. It requires `BC1_RENDER=1` and
remains off by default. The existing movie, screen and BC1 consumers retain
exclusive ownership of an active primitive: a rejected method cannot fall
through to another renderer. All dispatch paths restore native FPSCR.

Preparation pins the owned XBE, complete native186 state/push captures and
matching generated-image capture. It emits a private 9016-byte version1
contract containing the complete retained setup/validity map, seven original
program slots and four immediate vertices. Native185 and native186 have
identical composition vertex words. The contract validates incoming words;
it never supplies substitute vertices. Every BEGIN and END validates the
pipeline; END also checks all collected input bits before rendering.

The backend independently requires the observed 640×480 target, full clip,
ARGB pitch2560, disabled depth/stencil/alpha-test/fog/stipple, full RGBA mask,
and the measured constant-color / inverse-source-alpha ADD blend. Texture0 is
linear 640×480 ARGB, texture2 exactly 8×8 BC2, and texture3 linear 320×240 ARGB.
Their point/linear clamp samplers and texture-shader modes are checked. The
retained DOTPRODUCT unit1 descriptor is not sampled. Unsupported state remains
a strict rejection, even if a supplied contract attempted to authorize it.

All three input spans and the readable/writable destination must resolve
fully through validated DMA objects. The color target must pass its attachment check. Depth/stencil operations are
disabled and the original depth slot is unbound; no depth DMA is read. Texture0
reads the earlier depth image through its own validated sampled-resource view. Every pair of physical and
returned host spans must be disjoint, with overflow checks. Input addresses
for sampled resources may relocate; their complete formats and layout remain
fixed. A failed renderer or overlapping returned staging buffer leaves all
guest bytes untouched. Only a completed independent 640×480 RGBA buffer is
copied into the original destination.

GXM shares the existing H2 context, patcher, 640×480 target and private depth
mask. It uploads actual guest inputs, converts only the four BC2 block positions,
copies the actual original destination through the GPU, then runs the original
composition shader and waits for completion before returning staging pixels.
The source-alpha precision bounds and primary-reference limitations remain
those in [the isolated GPU comparison](halo2-composition-probe.md). This change
adds no presentation call: the original game must still submit its later draw
and flip commands. Neither fixture pixels nor a replacement menu enters the
runtime.

Synthetic tests exercise exact method order, all retained-state mutations,
complete resource validation, failed renders, readonly output, truncated and
misaligned views, physical aliasing and distinct physical spans mapped to
overlapping host pointers. The inactive depth slot and DOTPRODUCT descriptor are deliberately unmapped
in the successful fixture to verify it is not fetched. A separate test uses
the actual dispatch function and four bounded consumer oracles to verify
exclusive active ownership and FPSCR restoration on both success and failure.
All 49 host executables pass, as do both new ASan/UBSan tests and six shader
preparation tests. With the option disabled, both `quad_gxm.o` and
`host_channel_runtime.o` are byte-identical to native186.

The diagnostic package embeds owned game image/code and shader data and must
not be uploaded as a distributable release. Game assets, generated source,
contracts, packages and captures remain private and outside Git. No CE or
physical Vita changes are involved.

Native187/188 did not advance: they preserved the prior stop at BEGIN
`03B7BF34`. The admission diagnostic verified a loaded contract and matching
pipeline, then reported resource rejection. Native189 read the original
texture/color/zeta DMA objects: each is class `B03D`, limit `07FFAFFF`, base0.
The retained zeta offset is0, unlike the first screen pass's `037BC000`.
Requiring the inactive zeta slot to equal texture0 was an integration error.
The correction removes that unused dependency while preserving all sampled
input and color checks; the synthetic successful fixture now makes the inactive
zeta descriptor invalid and asserts that no depth attachment callback occurs.
Depth/stencil operations remain explicitly rejected when enabled. The separate
sampled texture0 still reads the earlier depth image at `037BC000`.
The pinned [xemu surface implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/gl/surface.c#L1211)
likewise distinguishes depth/stencil surface updates from color updates; the
read-only original descriptors establish this pass's actual binding state.
All three failed attempts and their exact build/source records remain private.

Native190 completes the original composition. GXM stages 307,200 nonblack
pixels (first ARGB word `007A6D5B`), then END at `03B7C0CC` commits RGBA to
`038E8000`. The next original BEGIN7 at `03B7C8D8` selects a 160×120 target at
`02B1B000`, pitch640, with four linear samplers of that just-composed 640×480
image. Its captured texture0 has the same first word and nonblack count. The
seven-slot program is unchanged, but the four-stage threshold/downsample
pipeline is unsupported and rejects. PUT remains `03B80158`, EIP `003FAC58`.
This is actual original command progress, not a presented menu: the last
scanout is still black frame135 with SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
The original Microsoft Game Studios intro was visually checked again.

Native190 verifies 175 dependency targets. ELF SHA256:
`86055a6e65e60b512c10be98bbe2179e2ba7dcdf73e6c7fc29966389bdf8f768`;
EBOOT `c16cdef0ead8cdfaefc85f7c812fa5b163c20d4f91e24625c836b98d71521da6`;
trace `706d4c39a27a5cb94cae426afdcdb041294efa9585cffa67a02ef9c1169b2d1a`;
channel `664175f4f52fb6f0026a81d273c4b2f1d405fff36756eb0b7114dce41df63605`;
push `0284aa9eac41034b9620edae740db95e200cc876afc683fc80f065829584eec0`;
next texture0 `d6d4fcc7b0fd4754445fefa6a70b37745b77aa892e303e201f03de9a34b43143`.
Private build/source records are in `composition-unused-depth/`; matching
captures are `native-190-artifacts/` and `native-190-view/`. The owned emulator
was stopped after capture. Replaying that exact archived build from the private
handoff directory uses the existing isolated helpers:

```sh
python3 preserve_fresh_cache.py native190-replay
python3 capture_run.py 190-replay native-190-artifacts
python3 drive_startup.py 190-replay native-190-artifacts
```

The cache helper preserves the old private cache and allows the original game
to format/copy its map again; it does not fabricate cache contents. Next is the
actual captured 160×120 four-sample threshold/downsample, including its filter
coordinates, output pitch and generated-image ownership.
