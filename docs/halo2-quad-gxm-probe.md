# Halo 2: original shader, synthetic GXM pixel validation

The standalone `XH2T00001` utility now executes the observed Halo 2 vertex
program through GXM and checks its output against synthetic textures. This is
not yet connected to Xbox command execution. Actual Halo 2 remains at native73:
the original intro movie reaches `BEGIN QUADS` (`17FC=7`, GET `03B4438C`), and
the checked consumer stops before emitting geometry. There is no visible movie
or main menu yet.

## Supported shader experiment

`prepare_quad_shaders.py` accepts only the owned XBE SHA-256
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
It fingerprints the complete 340-byte XVS at `43F2B0` with SHA-256
`2e4131cffc816b0306d02238b90d3e4fb0c31ae4ce982ffa2c9980f9368260da`.
All 21 arithmetic slots remain, including the RCC dual-issued with a texture
coordinate calculation in slot 11 and the viewport operations in slots 16/20.

The existing arithmetic generator is reused without shared source changes.
Only this H2 preparation path initializes output-register W to one and replaces
the CE depth workaround with the post-shader screen-to-clip transformation.
Screen X/Y are truncated to 1/16 pixel, then mapped to the GXM clip rectangle;
screen Z is normalized by the supplied depth range. The probe uses W=1, a
640×480 viewport and a 24-bit depth range. It does not validate general W,
clipping, depth testing, or arbitrary shader programs.

The explicit expanded input binding is v0/position, v3/pixel texture coordinates,
v9/normalized diffuse color, each represented as FLOAT4 in a 48-byte host vertex.
Those are register bindings observed in the immediate command stream, not a
recovered Xbox vertex declaration or CE input layout.

The fragment specialization requires the exact observed two-stage combiner:
alpha ICW `18111912,1A111814`, color ICW `08010902,0A010804`, both output
word pairs `89,AC`, control `11102`, final words `C,1C00`, and white stage-0
C0. Its live result is texture0 times diffuse color. The t1/t2 results have no
path to the final result, so they introduce no sampled resources. This is not a
general combiner translator. A future command consumer must check this state
before choosing the specialization.

Texture0 is one-level linear X8R8G8B8, 640×480, pitch 2560. The probe uses clamp
U/V and linear sampling, normalizes pixel texture coordinates, and uses projected
sampling. The original texture format/address/control/filter must be validated
before guest integration. Source texture alpha is one; white diffuse alpha is
one. Other alpha, destination blending, texture layouts and geometry remain
unsupported by this experiment.

The primary implementation references are pinned xemu
[`vsh.c`](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/vsh.c)
(output defaults and subpixel precision),
[`vsh-prog.c`](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/vsh-prog.c)
(post-program screen conversion), and
[`psh.c`](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/glsl/psh.c)
(PROJECT2D and linear-texture coordinate normalization).

## Reproduction and observed results

Generate into a private directory, supplying the archived native73 channel JSON
to obtain its constants. The tool checks that the captured program matches the
owned XVS; it does not turn arbitrary captured state into a supported pipeline.

```sh
python games/halo2_5849/prepare_quad_shaders.py /owned/halo2/default.xbe \
  --snapshot /private/native-73-artifacts/channel-at-stop.json \
  --out /private/h2-quad-shaders
```

Compile the two generated `.cg` files with the existing shader compiler in an
isolated Vita3K lab. Place the resulting GXP files beside `constants.bin`, then:

```sh
make -C games/halo2_5849 quad-probe \
  BUILD=/private/h2-quad-probe QUAD_SHADERS=/private/h2-quad-shaders
```

**This diagnostic package contains owned game shader code/constants. Do not
upload it as a distributable release.** Keep generated sources, binaries and
captures outside Git. Install only in the isolated synthetic lab, which needs
`ux0:data/xita-halo2/` for its three raw output files. It uses its own title ID,
not the Halo 2 startup app or Halo CE.

On Vita3K `0.2.1 4074-496939b6`, OpenGL llvmpipe, the lab uses synchronous shader
compilation (`async-pipeline-compilation: false`). Each scene waits on an explicit
fragment completion notification before CPU readback. Earlier short probes with
asynchronous compilation and/or only `sceGxmFinish` returned empty surfaces;
those failures are preserved privately. No conclusion about hardware GXM bugs
is drawn from these emulator-only observations.

The final probe performs three independent checks of the RGB staging image:

1. Four quadrant colors: all 307,200 pixels match exactly.
2. A coordinate-derived RGB hash texture: all 307,200 pixels match exactly,
   checking orientation and pixel-center sampling across the whole image.
3. Reversed winding with the same cull state: the previous GPU image remains
   unchanged at all 307,200 pixels.

The two prepared programs compile with zero failures to 1,112-byte vertex GXP
(SHA `57bd91c49f579283601fa19a85048fedc07635b6c5601affd0f4b7ebaadfed88`)
and 400-byte fragment GXP
(`2317c20e48dd67be27bbbc5e274d3c3765c31ce134acb5d1c6d29034f9fd7cdc`).
The packaged probe ELF SHA is
`84755fce0802801ec29d89e9aebd70cd50c1053e4d869d8a01a7991495d148d4`,
EBOOT `2025d91bb59ed8b7542fb65758b29c90eb67e51657136bd3d652a8e6911a7810`,
and VPK `21c238d7169a80747bc118fce5469e065d7d6fe401f5704942059f300706af08`.
Private evidence is `quad-shaders/attempt12-tracked-package/` under the Halo 2
artifact root. The quadrant readback SHA is
`b46dc9b3295e21f00728bd13636d88d0ad1739714ad497e40205cd073549291c`;
both later readbacks are
`db65edbfb228556c2d9ccea8b7fea69ae42d278f19850cad3faab48798b62db4`.

The probe deliberately checks RGB only. OpenGL surface creation does not load
CPU-written destination pixels, so preloading destination alpha is not a valid
preservation strategy in this lab. The next integration must stage the GPU
result and preserve guest alpha explicitly, restricted to a validated opaque
full-surface quad, or implement a properly validated destination-load path.
No guest framebuffer has been mutated by this utility.
