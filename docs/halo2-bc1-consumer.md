# Original 320×240 BC1 command consumer

`BC1_RENDER=1` adds a Halo2-only consumer for the original pass captured at
native184. It requires `SCREEN_RENDER=1` and remains disabled by default.
Existing movie/screen consumers retain their own contracts. Once a consumer
accepts BEGIN, it owns the active sequence: rejection cannot fall through into
another renderer or generic state acceptance.

The new consumer requires the full retained setup/validity banks and seven-slot
program contract, plus independent restrictions for the framebuffer format,
320×240 clip, pitch1280, four enabled one-level BC1 samplers, repeat/linear
filtering, disabled blending/depth/stencil/alpha test/fog/stipple, and complete
RGBA writes. The private contract supplies acceptance criteria only. Every
vertex is read from the original ordered 80-word immediate sequence.

The screen positions must match the bounded captured quad. Animated normalized
texture xy values come from live commands, with finite bounds [-4,4], zero z,
and projective divisor1. Signed zero bits are preserved. Different source
coordinates are tested independently of the contract's texture-coordinate
values. Other coordinate forms remain unsupported. END validates every stored
component again, along with the current pipeline and all resource spans.

All four textures resolve their complete 32-byte original read-only DMA spans.
They may overlap one another, as the original four references to one allocation
do. The writable 307,200-byte color attachment must be disjoint from every
texture in both physical and returned host address ranges. The optional
attachment validator runs before rendering. No guest zeta allocation is mapped
for this pass because its depth and stencil operations are disabled.

The GXM backend shares the existing H2 context, patcher and geometry buffers,
and allocates a separate 320×240 target and private mask storage. It binds all
of this pass's state, four real texture uploads, original vertices and actual
combiner constants. The compiled shader/BC1 layout and measured precision
limits are documented in [the independent probe](halo2-bc1-probe.md). No blend
destination initialization is needed for this destination-independent pass.
Only completed GPU output with a validated independent span is copied into
original guest color memory. Failure does not commit any partial framebuffer.
Staging/commit logs explicitly distinguish them from presentation.

Host checks cover the exact method order, every retained setup/program/validity
mutation, active-command rejection, changed live coordinates, signed zeros,
nonfinite/out-of-bound values, END revalidation, absent/invalid DMA resources,
color/source aliases, shared source views, mapping/attachment failures,
read-only destination rejection and bad/aliasing backend results. All 47 host
executables, BC1 consumer ASan/UBSan, and six BC1 plus eight existing screen
preparation tests pass. Generated guest C is unchanged.

Build with the earlier native184 inputs and private shader outputs:

```sh
# Existing owned boot flags/paths from the prior screen-consumer build also apply.
make -C games/halo2_5849 -j4 HOST_CHANNEL=1 QUAD_RENDER=1 SCREEN_RENDER=1 \
  BC1_RENDER=1 BC1_SHADERS="$PRIVATE/bc1-consumer/prepared" \
  AUDIO_HOST=1 AUDIO_DSP=1 AUDIO_SPATIAL_MODEL=1 AUDIO_FILTER_MODEL=1 \
  AUDIO_MULTIBIN_UNAVAILABLE=1 GUEST_OPT=-O0 \
  GENERATED="$PRIVATE/scalar-compare-boot/generated" \
  IMAGE="$PRIVATE/scalar-compare-boot/halo2_image.bin" \
  BUILD="$PRIVATE/bc1-consumer/build" \
  QUAD_SHADERS="$PRIVATE/quad-shaders/prepared" \
  SCREEN_SHADERS="$PRIVATE/screen-consumer/prepared" \
  DSP_ASSET="$PRIVATE/dsp-bringup/halo2-dsp.bin"
```

The three additional private assets are `bc1.vert.gxp`, `bc1.frag.gxp` and
`bc1.contract.bin`; the preparation tool emits the contract from the pinned
owned state and push capture. This diagnostic package embeds owned game
image/code/shader data and must not be uploaded as a distributable release.
All assets, generated C, packages, traces and captures remain outside Git.

Native185 verifies all 174 dependency targets and the packaged shader/contract
bytes. It displays the original Microsoft Game Studios intro, performs the
complete original mainmenu-map copy and normal Start path, and completes the
BC1 pass at END `03B7BBB0`. The original color target is `02B48000`. Staging
contains 76,532 nonblack pixels; first ARGB word is `00000A25`. The trace's
first UV words `3F21C44B/3F22C1D1` match this run's archived original pushbuffer
and differ from the prepared probe fixture, demonstrating live input use.

The next strict stop is another original BEGIN7 at `03B7BF34`, PUT `03B80158`,
EIP `003FAC58` during FIFO submission. Its target returns to 640×480,
`038E8000`, and the pipeline now has six combiner stages. The BC1 result has
not been presented: last scanout135 remains black, with the same SHA256 as
native184. No original main menu is visible. The next task is to validate this
composition pass and its additional texture inputs.

Native185 private ELF SHA256:
`92427b8d15902bc3d92fc0a7bf8354bb86c41dfdf75a6d7d524c04a5d386cfe8`;
EBOOT `366916262a5b8acf0b609a041c3205e825530718d6c6c567a030af4a8272a233`;
trace `3e8cff22dfceeb48ba87df73c56dce337c06cea453b24cafce7be6aede4355fd`;
channel `01c41cab9b4892c0831c5ff35f8682a3d991ece0ebba78f049cabf01f3196fef`;
push `f317f1f6373ae15d7a8dbb521cd4b6cf49ebc9872406e8f64ceee97e792c86a3`.
Last scanout SHA256:
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.

Private evidence is under `native-185-artifacts/`, `native-185-view/` and
`bc1-consumer/`. The latter includes the frozen source diff/new files, build
identity, host/sanitizer checks and inputs. A feature-disabled `quad_gxm.o`
is byte-identical to native184; the disabled dispatcher compiles differently
but its original selection semantics are preserved. No CE source or shared
emulator binary was modified. The owned native185 process was stopped after
capture. The separate :111 X server remains available.

Replay the frozen package from the private base, with its owned emulator
stopped, using the same reviewed local helpers:

```sh
python3 preserve_fresh_cache.py native185-replay
python3 capture_run.py 185-replay native-185-artifacts
python3 drive_startup.py 185-replay native-185-artifacts
```
