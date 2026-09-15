# Original 160×120 averaging filter: GPU validation

Native191 completes the original threshold pass and reaches BEGIN7 at
`03B7CE50`. The next target is `02B31800`, 160×120 ARGB/pitch640. All four
linear/clamp samplers read the completed threshold image at `02B1B000`.
The original main menu remains absent; last presented frame135 is black.
This isolated probe adds no accepted game command.

The same seven-slot vertex program and 24-word immediate packet preserve the
oversized 640×480 window quad, clipped to 160×120. The texture-coordinate slope
is now1, with corner offsets ±0.5 texels. The pinned
[xemu output conversion](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/vsh-prog.c#L753)
retains the original window coordinates while using the actual surface size.
Four bilinear samples average RGB through three original combiner stages.
For one shared source, this is the separable `[1,2,1]/4` kernel, with clamped
edges. Alpha is `(t0.a+t1.a+t2.a+t3.a)/2 * c1[2].a`, then clamped; the original
constant is178/255. The generated shader preserves those stages and final
outputs. Its unused final E/F zero product remains in the translated source.

Preparation pins the owned XBE and complete native191 channel, ring and image
captures, including the input header and every original vertex word. The
captured input contains exactly 76,800 zero bytes because it is the preceding
threshold result. It is never substituted for future live input. SceShaccCg
compiles the original vertex/fragment programs to596/1092 bytes, with zero
failures and one warning for the unused final product. A separate344-byte
copy shader exists only for the sampler diagnostic.

The tracked normal GPU probe executes eight fixtures,153,600 output pixels:
actual input, shared/independent gradients, block patterns, opposite winding,
fractional offsets, reduced alpha gain and saturation. Actual zero input and
the constant-color saturation fixture are exact. The other full results have
at most one UNORM8 level of RGB error and two alpha levels against ideal
bilinear sampling and independent averaging/gain arithmetic. Opposite windings
are identical. The initial strict one-level comparison failed on amplified
alpha and remains preserved; it was not replaced by a success claim.

A second tracked probe measures each of the four samplers separately for all
eight fixtures,614,400 sampled pixels. Its measured differences from ideal
bilinear sampling are at most approximately1.063/1.000/0.563/1.125 UNORM8 levels
for RGBA. Applying the original combiner equations to those measured samples
matches every normal fixture within one UNORM8 level. The validation therefore
requires the full RGB≤1/alpha≤2 bound, separate sampler≤2 bounds and the
measured-sample combiner≤1 bound. Both the original strict counts and exact
measured maxima remain in the report. This explains the observed compounded
precision bound; it does not establish bit-exact NV2A filtering.

The RGBA16 sampler readback requires an explicit encoding. The unchanged
Vita3K build4074-496939b6 uses `GL_UNSIGNED_SHORT` for its F16x4 ABGR/ARGB
[OpenGL readback entries](https://github.com/Vita3K/Vita3K/blob/496939b6/vita3k/renderer/src/gl/renderer.cpp).
These captured bytes are decoded as UNORM16, not IEEE half. The checker also
supports explicit half captures for other environments and rejects malformed,
nonfinite or unrecognized encodings. Both compilations and both normal probes completed and wrote their outputs
before emulator faults during utility exit; their complete outputs and console
logs are preserved. The sampler probe completed without that exit fault. Final
normal pixels are byte-identical to the first probe. No emulator binary changed.

Seven synthetic Python tests cover producer admission, shader/texture limits,
clamped kernel equivalence, alpha gain/saturation, generator-state restoration,
readback encodings and wrong-owned-input rejection. Existing threshold and
composition tests also pass. Both optional probe targets compile with `-Werror`:

```sh
make -C games/halo2_5849 -j4 blur-probe blur-sample-probe \
  BUILD=/private/build BLUR_SHADERS=/private/prepared
python3 games/halo2_5849/check_blur_probe.py \
  --prepared /private/prepared --results /private/normal-results \
  --sample-results /private/sample-results --sample-encoding vita3k-unorm16
```

These packages embed owned shaders/image data and must not be uploaded as
distributable releases. Inputs, generated code, packages and captures stay
private and outside Git. Only the owned `:111` lab was used; CE, shared Vita3K
and physical Vita remain unchanged. No owned emulator remains running.

Private evidence: `blur-pass/compile01`, `compile02-sampler`, preserved failed
`probe01-tracked/comparison.json` and `sample01-tracked/comparison.json`, final
`probe02-tracked/comparison.json`, plus the32 sampler captures. Final normal
ELF SHA256 `3ccc548a69a10010286637e1c83144a7dffea810ab1461ebde4add1470f00e9a`;
EBOOT `d92c4d674107e368e9ebdc19773f4620b449bbb5ee2b691dd03c1d9729dabdd0`.
Sampler ELF `9de2eda23e4c4a661d0333be301e32c2936fc2e7b49214ad26fea7b55be4cde8`;
EBOOT `03a8c5b32cb36679c5bbd923aea548a9fcf2ba610fda88f9a49b5cb6634423d2`.
Final comparison `a618cdcd53cff47d04cf867685bc7666406dea14f7f6828868a4371f93ea9966`.

The owned ring also contains three subsequent averaging passes with identical
decoded pixel state and offsets±0.625, ±0.78125 and approximately±0.96875.
Those packets are read-only evidence, not yet executed game draws. Next is
bounded original-command integration with complete sampler/output ownership,
followed by native replay toward the original menu.
