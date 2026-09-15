# Original 160×120 threshold draw: GPU validation

Native190 reaches BEGIN7 at `03B7C8D8` after completing the original composition.
The next target is `02B1B000`, width160, height120, pitch640. All four samplers
read the just-generated 640×480 ARGB image at `038E8000`, with linear/clamp
sampling. Blending, depth/stencil, alpha test, fog and stipple are disabled.
The original main menu is still absent; last presented frame135 remains black.
This probe accepts no additional game command.

The seven-slot vertex program is unchanged, but the input packet now writes
v5, v4, v3, v2, v1, then position: 24 words per vertex. Its quad spans window
coordinates0..640 and0..480, with a different constant positive W. It must be
clipped to the 160×120 target, **not resized to fit**. The pinned
[xemu programmable-output conversion](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/vsh-prog.c#L753)
also preserves shader window coordinates while converting them using the
surface size and undoing the later W divide. The new wrapper uses160/120 for
that conversion and preserves every original instruction and vertex word.

Within the clipped region, the four texture coordinates have slope4 and
corner offsets(-1,-1), (1,-1), (-1,1), (1,1). At each output pixel center, four
bilinear samples average the corresponding 4×4 source block. The first three
combiner stages form pairwise averages; stage3 subtracts its RGB constant and
scales by4. Final unsigned RGB clamps the result, and final alpha is zero.
The original alpha intermediates and final E/F product remain translated,
although neither changes the final output in this captured shader. Component
producer checks reject unbound or premature reads, including circular E/F
product reads. Their existing composition defaults still exclude texture1 and
an implicit final product; the new options apply only to this explicit route.

Preparation pins the owned XBE and complete native190 state/push/image captures.
The source image capture SHA256 is
`d6d4fcc7b0fd4754445fefa6a70b37745b77aa892e303e201f03de9a34b43143`.
Its texture data is real output from the earlier composition, never a fabricated
menu image. SceShaccCg compiles the translated vertex/fragment programs to
596/1012 bytes with zero failures.

The tracked isolated GXM package completes eight fixtures, 153,600 output
pixels: actual captured input; shared and independent synthetic gradients;
block/color/alpha patterns; opposite winding; fractional coordinate offsets;
alternate threshold constants; and constant-color saturation. The original
captured input produces black **exactly**, because it is below the threshold.
Synthetic bright inputs produce nonzero output. Seven fixtures match the
independent NumPy reference exactly. The strengthened fractional fixture is
within one UNORM8 level (R/B), with no pixels exceeding that bound. Opposite
windings are identical. The earlier fractional fixture stayed inside uniform
blocks; that evidence is preserved, and the final fixture uses varying samples.
A separate host check proves the four original bilinear samples equal a 4×4
box average. These are emulator GPU results, not a claim of bit-exact Xbox
hardware or visible menu progress.

Seven new synthetic Python tests and the six existing composition tests pass.
The optional package builds with `-Werror`:

```sh
make -C games/halo2_5849 -j4 threshold-probe \
  BUILD=/private/build THRESHOLD_SHADERS=/private/prepared
python3 games/halo2_5849/check_threshold_probe.py \
  --prepared /private/prepared --results /private/results
```

The package embeds owned shader/image data and must not be uploaded as a
redistributable release. Game data, generated Cg/GXP, packages and captured
outputs remain private and outside Git. The probe uses only the owned `:111`
lab and its emulator process was stopped after completion. CE, the shared
Vita3K binary and physical Vita remain unchanged.

Private evidence: `threshold-pass/compile01` and `probe02-fractional`.
Probe ELF SHA256:
`03c94ffb7f2a3367cb1df63986c8d9383ddf68e6b21a8dd381daacb0e8e564fc`;
EBOOT `ae11c53aaaa175f46f73cb0a5176b72e719142ee7fea95e29a85f9d08c9dd9af`;
comparison `bb16576db691ece42548561174262035e95059f2c8722d3a27cedc8d4a021461`.
Next is a bounded original-command consumer with 24-word vertices, complete
read-only sampler views, an isolated160×120 output and no inactive-depth read,
followed by actual game replay.
