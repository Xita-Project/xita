# Screen composite routing — September 6, 2026

The Snipers zoom trace exposed a gap after adding the captured fragment assets.
One VS38/74D0C65E pass entered the mesh renderer because a texture aliased the
backbuffer. Another pass with the same combiner and ordinary texture bindings
still entered `xv_ui_gxm_quads`, which binds only one texture. Its four-input
filtered composite therefore lost its other inputs, fragment constants and
original blend/alpha behavior.

The immediate bridge now recognizes the exact canonical VS38 programs
74D0C65E (four-input composite) and 1E073CA3 (two-input alpha composite).
It records their existing 20-byte UI vertices through the mesh path regardless
of whether an input aliases the backbuffer. All textures and constants are then
captured by the existing draw recorder. Recognition is restricted to these two
compiled pairs; dynamic colors and inactive combiner words do not change the
route, while active alpha, final-output and texture-mode changes reject it.

VS38 computes each texture coordinate through its own pair of affine rows.
For linear inputs, the bridge now divides both complete output rows by that
texture's dimensions, including their offsets. Swizzled/normalized inputs and
the guest constant array stay intact. This is required for the complete pass:
without the conversion, pixel coordinates clamped the source to an edge texel
and made the zoom view flat brown. The conversion is restricted to VS38's
known row layout and is not a global vertex or shader-generator change.

The requested mesh dump also uses the actual packed immediate stride, matching
the geometry-lifetime checker. Previously its header and vertex payload used
the preceding vertex buffer's stride (often 32 instead of 20 bytes for UI or
40 for flares). This is a diagnostic correction; the GPU vertex layout already
used the correct stride.

Host routing, mixed normalized/linear transform, package, shader lookup and
canonical-source checks pass, including ASan/UBSan checks. The native Vita build
passes. Vita3K unzoom/2x/10x captures retain the scene, scope border and filtered
surroundings. Both traced passes now enter the mesh renderer with their original
blend factors; neither uses the single-texture UI fallback. Stages 1–3 sample
approximately 0–1 UV coordinates, with the captured blur offsets intact.
The 20-byte mesh records have the expected 3,484-byte length for four vertices
and six indices. Three traced frames checked 186, 94 and 226 draws with zero
data mutations and no frame-storage shortages. Hardware rendering/performance
and broader cinematic effect checks remain open.
