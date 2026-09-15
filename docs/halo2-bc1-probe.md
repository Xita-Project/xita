# Original BC1 pass: isolated pixel validation

Native184 still stops at `03B7BA18`. This milestone prepares and tests that
original 320×240 pass in a separate GPU utility. It accepts no new game draw
and produces no displayed menu. The earlier Microsoft intro remains the last
verified original screen; last game scanout135 remains black.

The same seven-slot vertex program is compiled with a 320×240 screen-space
output conversion. Four PROJECT2D samplers use the original normalized
coordinates, BC1 8×8 one-level blocks, repeat addressing and linear filtering.
Three decoded combiner stages and the final combiner use the shared arithmetic
generator. Constants come from the retained hardware banks, including final
C0/C1 at slots16/17. The H2 wrapper rejects other texture modes, unbound input
registers and unsupported final-combiner inputs. It restores generator globals.
Original shader words, Cg/GXP outputs, texture bytes and fixtures stay private.

The private compiler produced 596-byte vertex, 892-byte effect and 264-byte
point-copy programs, with no compile failures. Warnings concern unused
combiner intermediates. The existing BC2 block helper now has a separate BC1
wrapper: 32-byte rows of four 8-byte blocks become GXM's Y-first block layout.
Internal endpoint/selectors stay unchanged. Synthetic tests cover all 63
partially overlapping BC1 spans and all 127 BC2 spans, wrong sizes, wrapping
pointers, and rejection without writes. The actual GPU point-decode fixtures
validate this BC1 layout independently.

## Precision controls

The initial comparison used ideal normalized RGB565 decoding and ideal linear
filtering. Eight final output fixtures contain 614,400 pixels. The captured
pass has 83 pixels differing by two UNORM8 levels; two stress fixtures have
682/722 such pixels. All remaining differences are at most one level. The
initial one-level acceptance check failed; its report is retained. No vertices,
texture bytes, color coefficients or original shader equations were changed to
remove that difference.

A second utility draws each sample into a higher precision target, preserving
32 separate 320×240 input images. The resulting checks separate three parts:

- Point BC1 decoding against the S3TC equations: at most one rounded UNORM8
  level, including both endpoint orders, all selectors and transparent texels.
- Repeat/linear sampling from the measured decoded texels: maximum about1.55
  UNORM8 levels from ideal interpolation across these fixtures (accepted bound2).
- CPU combiner equations using those separately measured samples: the captured
  pass is exact; every stress fixture is within one final UNORM8 level.

The final comparison retains both original error counts and these controls.
It requires all complete-output fixtures within two levels, all point controls
within one, all sampler controls within two, and all combiner controls within
one. Opposite winding is byte-identical. This is a measured precision limit for
the supported route, not bit-exact NV2A hardware equivalence. The controlled
sampling comparison uses measured decoded texels, and the combiner comparison
uses measured samples; neither is represented as an independent full-pipeline
oracle. The independent complete-output comparison remains in the report.

The tests exercise negative and repeated coordinates, distinct per-unit
textures, BC1 transparency, destination-independent output, and saturated dot
products. Original texcoords vary between native183 and native184 even though
retained channel state matches: a future consumer must use real command
coordinates, with checked bounds, rather than replay the private fixture.

## Diagnostic readback encoding

The utility requests an F16F16F16F16 surface. In the existing Vita3K build
`4074-496939b6`, the pinned [OpenGL color-readback table](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/gl/renderer.cpp)
uses `GL_UNSIGNED_SHORT` for its ABGR/ARGB F16x4 entries. The captured bytes
therefore encode normalized unsigned16 channels, not IEEE half floats. An
initial half-float interpretation produced NaNs and was rejected; the source
table and point controls establish the explicit `vita3k-unorm16` decoding.
The tool also supports explicit `half` decoding for a correctly encoded target.
No shared Vita3K binary, CE configuration or physical hardware was changed.
The game renderer continues to use ARGB8 and is unaffected by this utility's
higher precision readback issue.

Pinned [Vita3K BC1 format translation](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/gl/texture_formats.cpp)
selects the native GL S3TC RGBA format. The [S3TC specification](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_texture_compression_s3tc.txt)
defines BC1's endpoint-order-dependent palette/transparency. The observed host
is llvmpipe LLVM22.1.8, Mesa26.1.7. No claim is made about identical rounding on
physical Vita or original Xbox hardware.

## Reproduction and evidence

Use the private owned native184 snapshots and existing shader compiler utility:

```sh
python3 games/halo2_5849/prepare_bc1_shaders.py "$OWNED_XBE" \
  "$PRIVATE/native-184-artifacts/channel-at-stop.json" \
  "$PRIVATE/native-184-artifacts/push-at-stop.bin" \
  "$PRIVATE/native-184-artifacts/texture0-dxt1-at-stop.bin" \
  --out "$PRIVATE/bc1-pass/prepared"
# Compile the three emitted Cg files with the isolated existing xv_shadercomp.
make -C games/halo2_5849 -j4 bc1-probe bc1-sample-probe \
  BUILD="$PRIVATE/bc1-pass/tracked-build" BC1_SHADERS="$PRIVATE/bc1-pass/prepared"
# Run the two packages sequentially in the owned compiler/probe lab on :111.
python3 games/halo2_5849/check_bc1_probe.py \
  --prepared "$PRIVATE/bc1-pass/prepared" \
  --results "$PRIVATE/bc1-pass/probe04-tracked" \
  --sample-results "$PRIVATE/bc1-pass/probe05-sample-tracked" \
  --sample-encoding vita3k-unorm16
```

Both diagnostic packages embed owned game shader/input data and must not be
uploaded as distributable releases. Private `bc1-pass/` contains all inputs,
failed controls, primary-source copies, launches, binaries and outputs. Some
utility runs encounter the existing emulator exit/relaunch crash after all
outputs and the explicit completion marker; that is preserved separately from
completed pixel validation. No game success is inferred from the utility.

All 46 host executables, six new preparation/codec tests, eight previous screen
preparation tests, and BC1/BC2 layout ASan/UBSan pass. Both packages were built
through the tracked Makefile and replayed. Final comparison SHA256:
`c2cc63e82c5229e89718e7d38201c41e9746fd3d993c1790bee9bdfead06f50a`.

| Utility | ELF SHA256 | EBOOT SHA256 |
| --- | --- | --- |
| Full pass | `606d08f4a83637493be19805ea883c832a320b826d54173a1e696613499e0945` | `bfe72b003ea683c87e28d19f9a54de07403a5c4dbe5d836bca204d44faebca00` |
| Sample controls | `f7b57e402198643298c07b17536f15a814d8aa4dfe9c43cd2058e5b7d8383523` | `c99efde08ffe70b84c6b33cb172a15216e09e47d29a7a6d9b2378e10aed2b572` |

Next: a strictly checked consumer for the original 320×240 pass, using live
coordinates, validated DMA resources and completed GXM output before guest
memory is committed; then replay original startup toward the main menu.
