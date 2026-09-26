# Halo rendering reference

User requested applying the decompilation reference to rendering bugs as well
as performance. A shallow clone of https://github.com/bnunu/halo-1 is available
outside the source repository at `../private-references/bnunu-halo-1`.
Initial cloned HEAD: `8bad9d8d5ee4307270a2dab6b1ed71c6eaa96f78` (2026-09-26).
This differs from the earlier pinned collision web review; record the exact
revision for each later comparison. No vendor code has been merged into Xita.

Initial inspection targets, to cross-check against retail 3925 behavior:

| Bug family | Reference files | Question |
| --- | --- | --- |
| Vehicle/model lighting and shadows | `source/render/render_objects.c` | Are lighting-cache validity, child effect inheritance and shadow thresholds preserved? |
| Visor/reflections | `source/rasterizer/xbox/rasterizer_xbox_models.c` | Which model material stages and constants select reflection behavior? |
| Tree/glass transparency | `source/rasterizer/xbox/rasterizer_xbox_transparent_geometry.c` and transparent shader preprocessors | Are alpha test, blending, fog and stage ordering translated correctly? |
| Bloom/screen effects | `source/rasterizer/xbox/rasterizer_xbox_screen_effect.c` | Which passes and render-target dependencies are required? |
| Ammo HUD transparency | `source/interface/hud_weapon.c` | Is the rectangle caused by HUD input state or its downstream shader/blend state? |

These are investigation targets, not diagnoses. Use a reproduction/captured
draw from the installed build to distinguish engine-state errors from GXM
translation errors before editing. A matching function name or reconstructed
implementation does not prove compatibility with the retail Xbox build.
Retain the existing bug reports and performance stack; do not claim rendering
fixes from merely cloning this reference.
