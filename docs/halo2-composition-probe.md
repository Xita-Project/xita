# Original composition shader: isolated GPU validation

Native186 stops before the six-stage 640×480 composition draw described in
[the captured-input report](halo2-composition-input.md). This milestone prepares
its original program, constants and four immediate vertices, then runs them in
an isolated GXM utility. It accepts no additional guest draw and does not show
the main menu. The last game capture remains the Microsoft Game Studios intro,
followed by black presented frame135.

The wrapper binds the actual linear 640×480 texture0, 8×8 BC2 texture2 and the
preceding pass's generated linear 320×240 texture3. It implements the observed
PROJECT2D / DOTPRODUCT / DOT_ST / PROJECT2D route. Unit1 is not fetched.
Preparation checks RGB and alpha producers separately before emitting any
combiner: parallel stage inputs must have an earlier producer. The observed
v0/v1 components are written before use; initializing dead components does
not provide fabricated vertex colors. Unsupported modes, component reads and
final-combiner inputs reject preparation. HILO packing retains the primary
reference and hardware-verification limitation documented for the first
[screen probe](halo2-screen-effect-probe.md).

The independent NumPy reference spells out all six stages, including signed
bias/double, unsigned input clamps, replicated RGB dots, parallel alpha
products, two-color interpolation, and final alpha from intermediate blue
before final RGB scaling. Blending uses source RGB plus destination RGB times
one minus source alpha; destination alpha has the same multiplier. Original
constant-color blending has RGB factor1 and alpha factor0.

Eight fixtures cover captured inputs with a **synthetic zero destination**,
synthetic depth gradients, full lookup coverage, synthetic BC2 selectors and
alpha, generated-image sampling, nonzero RGBA destination, reversed winding,
and separate point decodes. No live destination was captured by native186.
The compiler produces 596-byte vertex, 1528-byte fragment and 344-byte copy
programs with zero failures. A companion utility keeps the same original
shader and disables blending to measure its source RGBA independently.

Both tracked packages complete all eight fixtures (2,457,600 pixels each).
The first strict one-level comparison failed and is preserved privately.
The captured-input blended fixture is within one UNORM8 level. Stress fixtures
4/5 have 194 pixels each outside one level, with a maximum of two. The source
control has RGB error at most one and unscaled alpha error at most two:
one pixel exceeds one alpha level in the captured fixture; 293 do so in the
synthetic stress fixtures. Computing the blend from that **measured** source
RGBA matches all six effect outputs exactly. Opposite windings are identical;
point ARGB readback is exact and BC2 decode is within one level. The checked
limits retain these counts explicitly; this is not bit-exact NV2A hardware
validation. No shader output is substituted into the game's inputs.

Six synthetic Python tests pass, alongside the six BC1 and eight earlier
screen preparation tests. Both optional targets compile with `-Werror`:

```sh
make -C games/halo2_5849 -j4 composition-probe composition-source-probe \
  BUILD=/private/build COMPOSITION_SHADERS=/private/prepared
python3 games/halo2_5849/check_composition_probe.py \
  --prepared /private/prepared --results /private/probe \
  --source-results /private/source-probe
```

The diagnostic packages embed owned shader/image data and must not be uploaded
as distributable releases. All preparation output, Cg/GXP, game captures and
probe packages remain private and outside Git. The utility runs only in the
owned `:111` lab. No shared emulator, CE or hardware changes were made.

Private evidence is `composition-pass/compile01`, `probe03-tracked` and
`probe04-source-tracked`. Normal ELF SHA256:
`1fc8a13012d875834703959ae37d489b02f918e3367c07e3dd7e3ede918709fb`;
EBOOT `a3c60834d573bcb36b8d36d862ad2d0309d0166718cf7a7ac0ae0fa1b7d8076e`.
Source-control ELF:
`c18781c326553bcbc2d8644acb53f371a50cd5dd074cdbd56b207b020478cb77`;
EBOOT `01bd374a4b5ca5089092769dfc81af256de62bea685316d647271bf65662856a`.
Final comparison JSON SHA256:
`572674b35379c348cffcc7a8053c7ce84d2b573221349386568af9a209eeed44`.
Both owned utility processes stopped after complete output. The next step is
an opt-in original-command consumer with complete resource validation and
staged commit, followed by actual game replay.
