# Halo 2 color-write masks

The GXM menu renderer previously set `blend.colorMask` to ALL for every draw.
The command consumer already validates and stores `NV097_SET_COLOR_MASK`
(method 0x358), but the renderer ignored it. The existing CE register definition
identifies the B/G/R/A enables at bits 0/8/16/24; VitaSDK defines a different
four-bit GXM mask. The renderer now translates those enables explicitly.

The fragment-program cache includes the effective color mask and checks the
underlying shader hash and vertex-program identity before reusing a variant.
Variants share the compiled shader registration, but each patched GXM program
retains its own mask. This applies with blending enabled or disabled. A zero
color mask still submits the draw, preserving depth and visibility work.

`tools/test_halo2_color_mask.py` extracts the production mask setup and full
fragment lookup/creation function. The old setup fails the channel-mask oracle;
the corrected version passes all 16 combinations, repeated bindings, revisiting
earlier variants, and shader-registration sharing. Normal and ASan/UBSan runs
pass. GXM calls in these tests are host doubles. A subsequent isolated Vita3K
run reached the profile screen and main menu with normal Start/Cross input,
without a guest guard stop. Background striping and overlapping geometry
remained visible: the mask correction alone does not fix that corruption.
The software fallback's separate channel-write behavior is not changed here.

## Investigation evidence

The private `visual-decoded` experiment retained the coordinate fix and set
`XV_UBC_NATIVE=0`, confirmed by its boot configuration log. Background corruption
remained visible in `decoded-profile.png`, so native compressed-texture upload
is not sufficient to explain that corruption. This is not a proof that every
compressed texture or mip layout is correct.

That run later stopped at the existing texture-write guard:
`texture source changed without a tracked write address=01096000 bytes=1228800
fn=003FAC30`. It is not a successful stability test. The saved channel snapshot
has color mask `0x00010101` (RGB only), demonstrating that restricted masks are
used by this startup/menu workload. It does not prove this particular draw
caused the visible background corruption.

The candidate has not been deployed to hardware. Do not attribute the user's
black first-launch screen to color masks: hardware startup logs are still
needed to identify that separate failure.
