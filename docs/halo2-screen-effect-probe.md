# First post-intro screen-effect GPU validation

Native181 still stops before the original quad begin at `03B7B424`. The
[resource capture](halo2-screen-effect-resources.md) supplies its actual
program, state and two texture inputs. This checkpoint prepares and tests a
GPU translation in a separate utility; **it does not accept a game draw, show
a menu or change game control flow.** The latest verified game screen remains
the original Microsoft Game Studios intro, followed by black frame 135.

## Exact scope

The preparer checks the owned XBE, complete channel snapshot and seven-slot
vertex program hashes. It translates all original vertex slots with explicit
register bindings, then adapts the final window-coordinate position to the
640×480 GXM viewport and original depth maximum. There is no inferred CE
vertex declaration or CE viewport-epilogue removal.

The fragment route is H2-only: point-sampled linear ARGB texture0 supplies
unsigned HILO coordinates, a dependent lookup reads texture2, and the shared
combiner emitter translates the four original stages. RGB and alpha products
read their pre-stage operands before writes. The final combiner is checked as
an identity output of r0. Missing/live unbound inputs and other mappings,
texture modes, outputs or final-combiner behavior reject. Shared generator
settings are restored even if emission raises an exception; CE defaults and
generation are unchanged.

The unsigned HILO dot product follows NVIDIA's
[NV_texture_shader specification](https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_shader.txt).
The specific ARGB packing, `(A<<8)|R` and `(G<<8)|B`, follows the pinned
[xemu shader implementation](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/psh.c).
This packing remains emulator-reference based, not a hardware NV2A oracle;
the reference itself flags mappings above 3 as incomplete. Another emulator's
unverified alternative packing is not treated as corroborating evidence.
The supported input is point-sampled UNORM8, so rounding reconstructs original
bytes without defining filtered HILO behavior. Unsigned high/low values are
divided by 65535; the third dot component is one.

The original shader samples 8×8 DXT23, one level. BC2 retains all color and
explicit alpha information, irrespective of a premultiplied-alpha authoring
convention. No unpremultiplication is introduced. The standard
[S3TC format definition](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt)
provides normalized RGB565 endpoints, four-entry color interpolation regardless
of endpoint ordering, and independent four-bit alpha.

The native GPU probe established that GXM's swizzled BC2 layout orders these
four blocks as top-left, bottom-left, top-right, bottom-right. The checked
64-byte conversion swaps the two middle blocks from the original row order;
it preserves every byte inside each block and handles overlapping spans.
Other dimensions, mip layouts and compression formats are outside this helper.

## Controlled findings and correction

The first probe's captured-input result matched the independent CPU equations,
but later fixtures failed. Two concrete causes were isolated:

- CPU writes to a reused GXM target did not initialize its cached blend
  destination. Both GLSL and SPIR-V reproduced this. An explicit GPU copy of
  all destination RGBA channels, before the effect draw, fixes repeated draws
  and alpha preservation. It does not modify guest memory.
- The direct compressed texture upload placed the two middle blocks in the
  wrong quadrants. Distinct block colors and alpha patterns exposed this;
  the bounded block-layout conversion fixes it.

The blend route is ADD with original constant color `00FFFFFF`: source RGB
factor one, source alpha factor zero, and destination factors
`ONE_MINUS_SRC_ALPHA`. The original destination is therefore material to both
RGB and alpha. Culling and depth/stencil tests/writes are disabled for this
captured effect. A private depth/mask surface remains attached to initialize
GXM correctly; it is never committed as guest depth.

## Validation

All 45 host executables and eight synthetic preparation tests pass. The
layout conversion also passes ASan/UBSan across 127 overlapping spans, with
invalid sizes/pointers preserving output. Repreparation from pinned Native181
captures reproduces every tested input byte. The probe's zero values for dead
v5/v6 color outputs are synthetic, not claims about original current color
registers; neither output is read by this fragment pipeline.

SceShaccCg compiles the translated vertex program to 596 bytes, effect fragment
to 1,316 bytes and staging-copy fragment to 344 bytes, with zero failures.
The effect produces only unused-temporary warnings. The eight GPU fixtures
compare 2,457,600 pixels against independent normalized BC2, dot-product,
combiner and blend equations. The captured-input effect and direct original
texture sample match exactly; other fixtures differ by at most one UNORM8
level per channel. Opposite winding results are byte-identical. Fixtures cover
ARGB gradients, both dependent coordinates, compressed block placement, all
alpha nibbles, endpoint order, selectors and a nonzero checkerboard destination.
These are host GPU precision checks, not pixel-exact Xbox hardware claims.

The final tracked Makefile package replay reproduces all eight prior corrected
outputs byte-for-byte. Private evidence is in `screen-effect/probe-06-tracked/`;
failed controls remain in `probe-01` through `probe-04-dxt`. Early utility
replays also recorded Vita3K faults during app exit after their completion
markers and output files; those are preserved separately from pixel results.
The shared emulator binary and CE configuration were not changed.

| Final private artifact | SHA-256 |
| --- | --- |
| Probe ELF | `4867fa1284e8a7fe0137c98ef993d2462110334591e5746d956511cbf980dbd6` |
| Probe EBOOT | `1065d630dd5a6b972864436a194f47284547892e7cb76495728c377c9de148f4` |
| Comparison JSON | `37a7108a9d654e8b670b2d85725bdb559d13cd0a3ba38879b22dc59a87a6ac36` |
| Captured-input output | `7d05babe72e4412299c92a8ba7691ca8049ebeb7a8138abca970a773c85f1428` |

## Reproduction and next task

From this repository, prepare into a private directory:

```sh
python3 games/halo2_5849/prepare_screen_shaders.py "$OWNED_XBE" "$CHANNEL_JSON" \
  --out "$PRIVATE_SCREEN" --push-capture "$PUSH_CAPTURE" \
  --texture-capture "$DXT23_CAPTURE"
```

Use the pinned Native181 captures. Compile the three generated Cg files through
`tools/shadercomp` in the isolated lab and place the resulting GXP files in
that private directory. Build the separate utility:

```sh
make -C games/halo2_5849 -j4 screen-probe \
  SCREEN_SHADERS="$PRIVATE_SCREEN" BUILD="$PRIVATE_PROBE_BUILD"
```

The utility title is `XH2T00001` and writes eight `screen-probe-N.bin` files in
its own lab's `ux0:data/xita-halo2`. Validate those files with:

```sh
python3 games/halo2_5849/check_screen_probe.py \
  --prepared "$PRIVATE_SCREEN" --results "$PRIVATE_PROBE_RESULTS"
```

The diagnostic package embeds owned shader code and captured game data and
must not be uploaded as a distributable release. All generated files, images,
packages and traces stay private. No hardware deployment is part of this test.

The next task is to connect the proven operations to a strict original-command
consumer, share the existing H2 GXM context safely, validate complete mapped
resource spans and commit RGBA only after GPU completion. The first post-intro
quad remains unconsumed until that integration is validated.
