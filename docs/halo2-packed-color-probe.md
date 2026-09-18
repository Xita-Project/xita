# Halo 2 packed-color quad GPU validation

Native196 still stops before the original inline quad at `03B7F048`; no game
draw is accepted by this checkpoint. The last presented frame is black and the
original main menu is absent. The owned Microsoft intro remains verified.

The captured 21-slot program reads float2 position, float2 UV and normalized
BGRA vertex color. Preparation expands only the defined absent components
(z=0, w=1) and preserves coordinate bits. All 21 instructions remain, including
the dual-issued texture multiply and reciprocal-clamp operation at slot 11.
The original DPH projection and c10/c11 viewport operations run before the
final conversion of window coordinates to the 640×480 GXM target. No CE
viewport-stripping rule is applied. Texture-output W starts at one, following
the already audited H2 output-register convention.

The two combiner stages write t0/t1, then t2/r0; only t0 and vertex color feed
final r0. The exact validated live expression is the stage-0 texture times its
factor, clamped, then vertex color multiplication, clamped, then the final
unsigned output. Neither t1 nor t2 can reach that result. Preparation checks
their dead branches and the two specific NONE-stage decoder warnings, rejects
any changed live dependency, and never supplies invented texels. This is a
bounded live-code elimination, not general unsupported-stage suppression.

The captured pipeline uses SRC_ALPHA / ONE_MINUS_SRC_ALPHA / ADD with RGB
writes and destination alpha preserved. Back faces are culled with clockwise
front winding. Sixteen GPU fixtures cover the actual 1024×8 strip, full images,
different packed colors, interpolated color, fractional UVs, original constant
translations, reversed faces with/without culling, zero alpha, copy-only
initialization and rectangular/sub-block textures. The original strip's
vertex alpha is 1/255 and its blend produces no byte-visible change in the
fixture destination. An opaque diagnostic version changes pixels; it does not
replace the original game alpha.

The new BC2 layout helper reorders complete compressed blocks into GXM's
Y-first rectangular Morton order. It accepts one power-of-two level with
logical axes 1–1024, validates both complete disjoint spans, and changes no
compressed texel data. The pinned
[Vita3K layout implementation](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/texture/format.cpp#L761)
establishes the block order. Host tests independently walk recursive spatial
quadrants for all 121 shapes, verify guard bytes and preserve rejection
isolation. Existing 8×8 BC1/BC2 helper semantics remain unchanged.

SceShaccCg compiles the vertex, fragment and staging-copy programs to
1048/392/344 bytes with zero warnings or failures. The first probe package
failed before launch because the ELF lacked space for SCE metadata; the
existing boot-target linker headroom resolves that packaging issue. Its failed
ELF/log remain private. The first strict one-level numerical comparison also
fails and is preserved: some synthetic compressed cases differ by three
UNORM8 levels. It is not relabeled as exact rendering.

Separate complete point-sampled texture captures identify the precision split:

* All 8192 texels of the owned texture match the normalized BC2 reference
  exactly, including alpha. Across 28,681 captured texels, synthetic RGB
  decoding differs by at most two levels; decoded alpha is exact.
* Using those measured texels, independent bilinear sampling, the original
  live shader equations and vertex transforms match all 16 unblended fixtures
  within one level per channel.
* Using independently measured unblended source values, destination blending
  matches all 16 fixtures within one RGB level. Destination alpha and pixels
  outside geometry remain byte-exact. Zero alpha, copy-only and culled cases
  preserve the destination exactly; reversed unculled output is identical.

The full ideal comparison remains bounded by three levels for synthetic data;
the actual owned strip is exact at original alpha and within one level at
opaque diagnostic alpha. These are measured GXM/Vita3K limits, not proof of
NV2A hardware identity. Main and source probes each cover 4,915,200 pixels.
The validator retains the failed strict-one reports and requires all three
independent measurements for the staged pass.

Initial point-probe attempts used small transient render targets. One had a
null depth/mask surface and returned black; adding the explicit initialized
depth surface fixed that first image. Creating another target still returned
`805B0017`, even when the first remained alive. Those failed attempts remain
in `probe03-point` through `probe05-point`. The completed `probe06-point` uses
the established 640×480 staging target and copies complete point-sampled tiles
to its diagnostic output. Shared Vita3K is unchanged. The compiler and
completed normal/source utilities hit the previously observed exit-time
SIGSEGV after finishing their outputs; the captures precede that fault.

All 53 host executables, BC2 layout ASan/UBSan and 40 focused Python tests pass.
Private files are under `sprite-pass`, including `prepared`, `compile01`,
`probe01`, `probe02-source`, `probe06-point` and `staged-validation.json`.
Validation report SHA256:
`b9c9e4163c6a4b7823fd0005e12618b4be267c6fce3bb962791335548f65d5e9`.
Normal probe ELF/EBOOT:
`4ad9b6ee8854c090db2350540b1454a9096840b2cddd45d47b16a0aed2201547` /
`85244294a4b5be016e28832dd8564cc1f70ec56844c449b7ff3fd1526cd51fda`.
Source probe:
`06ca2141513d276d9677bcb86792b5d316be460b8dd47dbc4a5fab09b52ba12a` /
`41eb240b9348e7240c60b6fa224c1c5f0a03a87b5fc2293ea8d0ab25b4bb6a06`.
Point probe:
`20670fb344bcb014ec8f8d77709bfe6447818fb57222fd82b575db409051d0ff` /
`3eec47082fbf6b465d09ea7e6eec2fa4756995769302c5ac6090c429216c8890`.

Reproduce the staged comparison from the source directory, with the private
paths substituted for these placeholders:

```sh
python3 games/halo2_5849/check_sprite_probe.py \
  --prepared PRIVATE/sprite-pass/prepared \
  --results PRIVATE/sprite-pass/probe01 \
  --source-results PRIVATE/sprite-pass/probe02-source \
  --point-results PRIVATE/sprite-pass/probe06-point
```

Next is bounded original inline-quad admission and complete input/output
ownership in the H2 renderer. Owned code, texture bytes, shaders, contracts,
packages and captures stay private and outside Git. Diagnostic packages embed
owned game content and must not be uploaded as distributable releases. Only
the isolated H2 `:111` lab is used; CE and physical Vita are untouched.
