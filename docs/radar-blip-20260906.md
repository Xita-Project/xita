# Radar blip alpha — September 6, 2026

The black spot reported during movement also reproduced while charging the
plasma pistol in Blood Gulch. A traced frame showed ten expanding radar blip
quads using VS04 and combiner `C2D57121`. They went through the generic font/UI
path, which substitutes texture alpha and ordinary source-alpha blending.

The captured Xbox program instead outputs `texture.rgb * vertex.rgb`, **zero
alpha**, and uses additive one/one blending. The later radar composite uses
the render target's alpha. The substituted alpha left a black opaque spot.

The HUD recognizer now identifies the complete active blip program and routes
its observed VS04 pair through normal draw recording. The generated fragment
program is embedded under canonical key `44D896D9`; original blend state and
write masks are retained. Recognition rejects active-state changes and leaves
unsupported vertex-program pairs on their existing path.

Validation:

- Recognition regression fails before the change and passes afterward,
  including changed unused stages/constants and rejected active-stage changes.
- Native shader compilation succeeds with zero failures. Draw/cache tests and
  4,392 shader identity checks plus 2,019 source-equivalence checks pass.
- The Vita build succeeds. The ordinary solo Split Screen path loads Blood
  Gulch. Plasma charge and lateral movement now show a small yellow blip with
  no black spot. The captured charge frame retains 199 draws with no geometry
  data changes before GPU completion.

Hardware validation and radar contacts from other characters remain pending.
